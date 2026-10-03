#pragma once

#include <string>
#include <vector>

namespace healthtracker {

// A subset of the data types documented at
// https://developers.google.com/health/data-types. The URL path segment
// must stay kebab-case (Google's convention for this API).
struct DataTypeInfo {
    std::string path_segment;  // e.g. "heart-rate" (used in the URL)
    std::string label;         // e.g. "Heart Rate" (used in terminal output)
    std::string scope;         // OAuth scope required to read this data type
};

const std::vector<DataTypeInfo>& AllKnownDataTypes();

// Looks up a data type by its path segment (case-sensitive). Throws
// std::invalid_argument if unknown, listing the valid options.
const DataTypeInfo& FindDataType(const std::string& path_segment);

}  // namespace healthtracker
