#pragma once

#include "healthtracker/http_client.hpp"

// IHttpClient implementation that speaks raw HTTP/1.1 over WiFiClientSecure
// directly (no Arduino HTTPClient dependency) — the ESP32 counterpart to the
// desktop build's CurlHttpClient.
//
// Opens a fresh connection per call rather than reusing one: the ESP32
// WiFiClientSecure object doesn't tolerate being disconnected and
// reconnected in place, so reuse produced a hard abort in practice.
class Esp32HttpClient : public healthtracker::IHttpClient {
public:
    healthtracker::HttpResponse Get(
        const std::string& url, const std::vector<std::string>& headers) override;

    healthtracker::HttpResponse PostForm(const std::string& url,
                                          const std::vector<std::string>& headers,
                                          const std::string& form_body) override;
};
