#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "healthtracker/http_client.hpp"
#include "healthtracker/oauth_client.hpp"
#include "healthtracker/time_utils.hpp"

namespace healthtracker {

// Thin client for the Google Health API (https://health.googleapis.com/v4).
// Depends only on IHttpClient, so it works unmodified against whatever
// concrete HTTP implementation the platform provides.
class GoogleHealthClient {
public:
    GoogleHealthClient(IHttpClient& http, OAuthClient& oauth,
                        std::string base_url = "https://health.googleapis.com/v4");

    // Server-side aggregated total for `data_type_path` over the single
    // civil day `date`, via dataPoints:dailyRollUp. Pull the type-specific
    // value back out with the matching Extract* helper in step_summary.hpp.
    nlohmann::json DailyRollup(const std::string& data_type_path, CivilDate date);

private:
    IHttpClient& http_;
    OAuthClient& oauth_;
    std::string base_url_;
};

}  // namespace healthtracker
