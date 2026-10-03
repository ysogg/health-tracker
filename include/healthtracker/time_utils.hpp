#pragma once

#include <string>

namespace healthtracker {

// Current UTC time as RFC3339, e.g. "2026-08-14T18:30:00Z".
std::string NowUtcIso8601();

// UTC time `hours_ago` hours before now, same format. Takes a fractional
// hour count since truncating to an int would silently shift the window.
std::string HoursAgoUtcIso8601(double hours_ago);

// A plain calendar date, with no time-of-day or timezone/UTC offset.
struct CivilDate {
    int year;
    int month;  // 1-12
    int day;    // 1-31
};

// Today's date in the device's configured local time zone.
CivilDate LocalToday();

// `date` shifted forward by `days` (handles month/year rollover).
CivilDate AddDays(CivilDate date, int days);

}  // namespace healthtracker
