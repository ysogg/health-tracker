#pragma once

#include <string>

namespace healthtracker {

// Percent-encodes a string for safe use in a URL query component or an
// application/x-www-form-urlencoded body.
std::string UrlEncode(const std::string& value);

}  // namespace healthtracker
