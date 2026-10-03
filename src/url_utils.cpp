#include "healthtracker/url_utils.hpp"

#include <cstdio>

namespace healthtracker {

std::string UrlEncode(const std::string& value) {
    std::string encoded;
    encoded.reserve(value.size());

    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

}  // namespace healthtracker
