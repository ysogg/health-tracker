#include <iostream>
#include <set>
#include <sstream>
#include <vector>

#include "healthtracker/curl_http_client.hpp"
#include "healthtracker/data_types.hpp"
#include "healthtracker/env_config.hpp"
#include "healthtracker/oauth_client.hpp"

namespace {

using healthtracker::CurlHttpClient;
using healthtracker::EnvConfig;
using healthtracker::FindDataType;

constexpr const char* ENV_PATH = ".env";
constexpr const char* REDIRECT_URI = "https://www.google.com";

std::vector<std::string> SplitAndTrim(const std::string& csv) {
    std::vector<std::string> parts;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto start = item.find_first_not_of(" \t");
        const auto end = item.find_last_not_of(" \t");
        if (start != std::string::npos) {
            parts.push_back(item.substr(start, end - start + 1));
        }
    }
    return parts;
}

std::vector<std::string> DefaultDataTypeIds() {
    return {"steps", "active-energy-burned", "heart-rate", "sleep", "exercise"};
}

// Extracts the ?code= parameter if the user pasted the full redirect URL,
// otherwise assumes they pasted just the bare code.
std::string ExtractAuthCode(const std::string& pasted) {
    const auto pos = pasted.find("code=");
    if (pos == std::string::npos) return pasted;

    const auto start = pos + 5;
    auto end = pasted.find('&', start);
    if (end == std::string::npos) end = pasted.size();
    return pasted.substr(start, end - start);
}

int RunAuthFlow(EnvConfig& env) {
    const std::string client_id = env.Require("GOOGLE_CLIENT_ID");
    const std::string client_secret = env.Require("GOOGLE_CLIENT_SECRET");

    const auto type_ids = SplitAndTrim(env.GetOr("DATA_TYPES", ""));
    const auto requested_ids = type_ids.empty() ? DefaultDataTypeIds() : type_ids;

    std::set<std::string> scopes;
    for (const auto& id : requested_ids) {
        scopes.insert(FindDataType(id).scope);
    }

    const std::string auth_url = healthtracker::BuildAuthorizationUrl(
        client_id, std::vector<std::string>(scopes.begin(), scopes.end()),
        REDIRECT_URI);

    std::cout << "1. Open this URL in a browser and sign in / grant consent:\n\n"
              << "   " << auth_url << "\n\n"
              << "2. Google will redirect you to " << REDIRECT_URI
              << "/?code=... (the page itself will look like an error — that's "
                 "expected).\n"
              << "3. Paste the full URL from your browser's address bar (or just "
                 "the code) here:\n\n> ";

    std::string pasted;
    std::getline(std::cin, pasted);
    const std::string code = ExtractAuthCode(pasted);
    if (code.empty()) {
        std::cerr << "No code provided.\n";
        return 1;
    }

    CurlHttpClient http;
    const auto tokens = healthtracker::ExchangeAuthorizationCode(
        http, client_id, client_secret, REDIRECT_URI, code);

    if (tokens.refresh_token.empty()) {
        std::cerr << "\nGoogle did not return a refresh token. This usually means "
                     "you've already granted this app consent before — revoke "
                     "access at https://myaccount.google.com/permissions and "
                     "re-run --auth.\n";
        return 1;
    }

    EnvConfig::UpsertFileValue(ENV_PATH, "GOOGLE_REFRESH_TOKEN", tokens.refresh_token);
    std::cout << "\nSaved GOOGLE_REFRESH_TOKEN to " << ENV_PATH << ". You can now run "
              << "the tool without --auth.\n";
    return 0;
}

}  // namespace

int main() {
    auto env = EnvConfig::LoadFromFile(ENV_PATH);
    if (!env) {
        std::cerr << "Could not find or read " << ENV_PATH
                  << ". Copy .env.example to .env and fill in your Google Cloud "
                     "OAuth client ID/secret first.\n";
        return 1;
    }

    try {
        return RunAuthFlow(*env);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
