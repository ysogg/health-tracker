#include "healthtracker/time_utils.hpp"

#include <chrono>
#include <ctime>
#include <ratio>

namespace healthtracker {

namespace {

std::string FormatUtc(std::chrono::system_clock::time_point tp) {
    const std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm utc_tm{};
    gmtime_r(&time, &utc_tm);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    return std::string(buf);
}

}  // namespace

std::string NowUtcIso8601() { return FormatUtc(std::chrono::system_clock::now()); }

std::string HoursAgoUtcIso8601(double hours_ago) {
    const auto offset = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::duration<double, std::ratio<3600>>(hours_ago));
    return FormatUtc(std::chrono::system_clock::now() - offset);
}

CivilDate LocalToday() {
    const std::time_t now = std::time(nullptr);
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    return {local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday};
}

CivilDate AddDays(CivilDate date, int days) {
    // mktime normalizes an out-of-range tm_mday into the correct following
    // month/year (handles leap years), avoiding hand-rolled calendar math.
    std::tm tm{};
    tm.tm_year = date.year - 1900;
    tm.tm_mon = date.month - 1;
    tm.tm_mday = date.day + days;
    tm.tm_hour = 12;  // noon, to stay clear of any DST-transition edge case
    std::mktime(&tm);
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

}  // namespace healthtracker
