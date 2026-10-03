#include "healthtracker/google_health_client.hpp"

#include <stdexcept>

namespace healthtracker {

GoogleHealthClient::GoogleHealthClient(IHttpClient& http, OAuthClient& oauth,
                                        std::string base_url)
    : http_(http), oauth_(oauth), base_url_(std::move(base_url)) {}

nlohmann::json GoogleHealthClient::DailyRollup(const std::string& data_type_path,
                                                CivilDate date) {
    const CivilDate next_day = AddDays(date, 1);
    // CivilTimeInterval/CivilDateTime is timezone/UTC-offset-free, 
    // i.e. a plain calendar date, not an instant
    const nlohmann::json body = {
        {"range",
         {{"start", {{"date", {{"year", date.year}, {"month", date.month}, {"day", date.day}}}}},
          {"end",
           {{"date",
             {{"year", next_day.year}, {"month", next_day.month}, {"day", next_day.day}}}}}}},
    };

    const std::string url =
        base_url_ + "/users/me/dataTypes/" + data_type_path + "/dataPoints:dailyRollUp";
    const std::vector<std::string> headers = {
        "Authorization: Bearer " + oauth_.GetAccessToken(),
        "Content-Type: application/json",
        "Accept: application/json",
    };

    const HttpResponse response = http_.PostForm(url, headers, body.dump());
    if (!response.Ok()) {
        throw std::runtime_error("Google Health API dailyRollUp failed for '" + data_type_path +
                                  "' (HTTP " + std::to_string(response.status_code) +
                                  "): " + response.body);
    }
    return nlohmann::json::parse(response.body);
}

}  // namespace healthtracker
