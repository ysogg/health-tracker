#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "healthtracker/http_client.hpp"

namespace healthtracker {

struct OAuthTokens {
    std::string access_token;
    std::string refresh_token;  // only populated on the initial code exchange
    int expires_in_seconds = 0;
};

// Builds the URL a user visits in a browser to grant consent, using the
// manual copy/paste redirect (a terminal app has no redirect listener).
std::string BuildAuthorizationUrl(const std::string& client_id,
                                   const std::vector<std::string>& scopes,
                                   const std::string& redirect_uri);

// One-time exchange of an authorization code (obtained by the user pasting
// it from the browser redirect) for an access + refresh token pair.
OAuthTokens ExchangeAuthorizationCode(IHttpClient& http, const std::string& client_id,
                                       const std::string& client_secret,
                                       const std::string& redirect_uri,
                                       const std::string& code);

// Holds a refresh token and hands out short-lived access tokens, refreshing
// on demand (and caching until close to expiry).
class OAuthClient {
public:
    OAuthClient(IHttpClient& http, std::string client_id, std::string client_secret,
                std::string refresh_token);

    // Returns a valid access token, refreshing first if the cached one is
    // missing or about to expire.
    std::string GetAccessToken();

private:
    IHttpClient& http_;
    std::string client_id_;
    std::string client_secret_;
    std::string refresh_token_;

    std::string cached_access_token_;
    std::chrono::steady_clock::time_point expires_at_;
};

}  // namespace healthtracker
