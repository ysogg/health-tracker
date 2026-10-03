#include "healthtracker/oauth_client.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "healthtracker/url_utils.hpp"

namespace healthtracker {

namespace {

constexpr const char* TOKEN_URI = "https://oauth2.googleapis.com/token";
constexpr const char* AUTH_URI = "https://accounts.google.com/o/oauth2/v2/auth";

// Refresh a bit before the token actually expires to avoid racing a request.
constexpr int EXPIRY_LEEWAY_SECONDS = 60;

std::string JoinScopes(const std::vector<std::string>& scopes) {
    std::string joined;
    for (size_t i = 0; i < scopes.size(); ++i) {
        if (i > 0) joined += " ";
        joined += scopes[i];
    }
    return joined;
}

OAuthTokens ParseTokenResponse(const HttpResponse& response) {
    if (!response.Ok()) {
        throw std::runtime_error("OAuth token request failed (HTTP " +
                                  std::to_string(response.status_code) +
                                  "): " + response.body);
    }

    const auto json = nlohmann::json::parse(response.body);

    OAuthTokens tokens;
    tokens.access_token = json.value("access_token", "");
    tokens.refresh_token = json.value("refresh_token", "");
    tokens.expires_in_seconds = json.value("expires_in", 3600);

    if (tokens.access_token.empty()) {
        throw std::runtime_error("OAuth response did not contain an access_token: " +
                                  response.body);
    }
    return tokens;
}

}  // namespace

std::string BuildAuthorizationUrl(const std::string& client_id,
                                   const std::vector<std::string>& scopes,
                                   const std::string& redirect_uri) {
    std::ostringstream url;
    url << AUTH_URI << "?client_id=" << UrlEncode(client_id)
        << "&redirect_uri=" << UrlEncode(redirect_uri)
        << "&response_type=code"
        << "&access_type=offline"
        << "&prompt=consent"
        << "&scope=" << UrlEncode(JoinScopes(scopes));
    return url.str();
}

OAuthTokens ExchangeAuthorizationCode(IHttpClient& http, const std::string& client_id,
                                       const std::string& client_secret,
                                       const std::string& redirect_uri,
                                       const std::string& code) {
    std::ostringstream body;
    body << "code=" << UrlEncode(code) << "&client_id=" << UrlEncode(client_id)
         << "&client_secret=" << UrlEncode(client_secret)
         << "&redirect_uri=" << UrlEncode(redirect_uri)
         << "&grant_type=authorization_code";

    const std::vector<std::string> headers = {
        "Content-Type: application/x-www-form-urlencoded"};
    return ParseTokenResponse(http.PostForm(TOKEN_URI, headers, body.str()));
}

OAuthClient::OAuthClient(IHttpClient& http, std::string client_id,
                          std::string client_secret, std::string refresh_token)
    : http_(http),
      client_id_(std::move(client_id)),
      client_secret_(std::move(client_secret)),
      refresh_token_(std::move(refresh_token)),
      expires_at_(std::chrono::steady_clock::time_point::min()) {}

std::string OAuthClient::GetAccessToken() {
    const auto now = std::chrono::steady_clock::now();
    if (!cached_access_token_.empty() && now < expires_at_) {
        return cached_access_token_;
    }

    std::ostringstream body;
    body << "refresh_token=" << UrlEncode(refresh_token_)
         << "&client_id=" << UrlEncode(client_id_)
         << "&client_secret=" << UrlEncode(client_secret_)
         << "&grant_type=refresh_token";

    const std::vector<std::string> headers = {
        "Content-Type: application/x-www-form-urlencoded"};
    const OAuthTokens tokens =
        ParseTokenResponse(http_.PostForm(TOKEN_URI, headers, body.str()));

    cached_access_token_ = tokens.access_token;
    expires_at_ = now + std::chrono::seconds(tokens.expires_in_seconds -
                                              EXPIRY_LEEWAY_SECONDS);
    return cached_access_token_;
}

}  // namespace healthtracker
