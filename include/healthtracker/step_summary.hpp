#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

namespace healthtracker {

// Thrown by the Extract* functions below when a dailyRollUp response parses
// fine and has a data point for the day, but that point is missing the
// specific total being asked for. Happens routinely right after local
// midnight, before the first sync of the new day has landed. Callers should
// catch this and keep their last known value rather than treating it as a
// fetch failure.
class MissingDataPointError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Pulls the "steps.countSum" value out of a GoogleHealthClient::dailyRollup
// response for the "steps" data type. No activity recorded for the day yet
// is a real zero, not a failure, so that case returns 0 rather than nullopt.
// Throws MissingDataPointError if a data point is present but missing
// "steps.countSum". Returns nullopt only if the top-level response doesn't
// match the expected shape.
std::optional<long long> ExtractDailyStepCount(const nlohmann::json& rollup_response);

// Sums the three "activeZoneMinutes.sumIn*HeartZone" fields out of a
// GoogleHealthClient::dailyRollup response for the "active-zone-minutes"
// data type. Same zero/missing/shape handling as ExtractDailyStepCount,
// throwing MissingDataPointError if "activeZoneMinutes" itself is absent
// from the day's data point.
std::optional<long long> ExtractDailyZoneMinutes(const nlohmann::json& rollup_response);

}  // namespace healthtracker
