#include "healthtracker/data_types.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace healthtracker {

namespace {
constexpr const char* ACTIVITY_SCOPE =
    "https://www.googleapis.com/auth/googlehealth.activity_and_fitness.readonly";
constexpr const char* METRICS_SCOPE =
    "https://www.googleapis.com/auth/"
    "googlehealth.health_metrics_and_measurements.readonly";
constexpr const char* SLEEP_SCOPE =
    "https://www.googleapis.com/auth/googlehealth.sleep.readonly";
}  // namespace

const std::vector<DataTypeInfo>& AllKnownDataTypes() {
    static const std::vector<DataTypeInfo> DATA_TYPES = {
        {"steps", "Steps", ACTIVITY_SCOPE},
        {"active-energy-burned", "Active Energy Burned", ACTIVITY_SCOPE},
        {"distance", "Distance", ACTIVITY_SCOPE},
        {"exercise", "Exercise Sessions", ACTIVITY_SCOPE},
        {"floors", "Floors Climbed", ACTIVITY_SCOPE},
        {"heart-rate", "Heart Rate", METRICS_SCOPE},
        {"daily-resting-heart-rate", "Resting Heart Rate (Daily)", METRICS_SCOPE},
        {"oxygen-saturation", "Oxygen Saturation", METRICS_SCOPE},
        {"weight", "Weight", METRICS_SCOPE},
        {"sleep", "Sleep", SLEEP_SCOPE},
        {"active-zone-minutes", "Active Zone Minutes", ACTIVITY_SCOPE},
    };
    return DATA_TYPES;
}

const DataTypeInfo& FindDataType(const std::string& path_segment) {
    const auto& types = AllKnownDataTypes();
    const auto it = std::find_if(types.begin(), types.end(),
                                  [&](const DataTypeInfo& info) {
                                      return info.path_segment == path_segment;
                                  });
    if (it != types.end()) return *it;

    std::ostringstream msg;
    msg << "Unknown data type '" << path_segment << "'. Known types: ";
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) msg << ", ";
        msg << types[i].path_segment;
    }
    throw std::invalid_argument(msg.str());
}

}  // namespace healthtracker
