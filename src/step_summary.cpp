#include "healthtracker/step_summary.hpp"

#include <cstdint>
#include <string>

namespace healthtracker {

namespace {

std::optional<long long> ToLongLong(const nlohmann::json& value) {
    if (value.is_number_integer()) return value.get<long long>();
    if (value.is_number_unsigned()) return static_cast<long long>(value.get<uint64_t>());
    if (value.is_number_float()) return static_cast<long long>(value.get<double>());
    if (value.is_string()) {
        try {
            size_t consumed = 0;
            const long long parsed = std::stoll(value.get<std::string>(), &consumed);
            if (consumed > 0) return parsed;
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<long long> ExtractDailyStepCount(const nlohmann::json& rollup_response) {
    if (!rollup_response.contains("rollupDataPoints") ||
        !rollup_response["rollupDataPoints"].is_array()) {
        return std::nullopt;
    }
    // An empty array is the API's normal response for a day with nothing
    // accumulated yet
    if (rollup_response["rollupDataPoints"].empty()) return 0;
    const auto& point = rollup_response["rollupDataPoints"][0];
    // After local midnight the day's point can omit steps until the next sync lands
    // Instead hold on to whatever the last known value was instead of displaying an error
    // If that's 0 then it remains 0 which avoids midnight display erroring out until next sync
    if (!point.contains("steps") || !point["steps"].contains("countSum")) {
        throw MissingDataPointError("Daily rollup data point is missing steps.countSum");
    }
    return ToLongLong(point["steps"]["countSum"]);
}

std::optional<long long> ExtractDailyZoneMinutes(const nlohmann::json& rollup_response) {
    if (!rollup_response.contains("rollupDataPoints") ||
        !rollup_response["rollupDataPoints"].is_array()) {
        return std::nullopt;
    }
    // Same reasoning as ExtractDailyStepCount: empty means nothing
    // for the day so far so it's not a parse/fetch failure.
    if (rollup_response["rollupDataPoints"].empty()) return 0;
    const auto& point = rollup_response["rollupDataPoints"][0];
    
    if (!point.contains("activeZoneMinutes")) {
        throw MissingDataPointError("Daily rollup data point is missing activeZoneMinutes");
    }
    const auto& zones = point["activeZoneMinutes"];

    long long total = 0;
    for (const char* field :
         {"sumInFatBurnHeartZone", "sumInCardioHeartZone", "sumInPeakHeartZone"}) {
        if (zones.contains(field)) {
            if (auto value = ToLongLong(zones[field])) total += *value;
        }
    }
    return total;
}

}  // namespace healthtracker
