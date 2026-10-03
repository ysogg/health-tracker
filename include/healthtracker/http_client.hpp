#pragma once

#include <string>
#include <vector>

namespace healthtracker {

// Response from an HTTP request.
struct HttpResponse {
    long status_code = 0;
    std::string body;
    bool Ok() const { return status_code >= 200 && status_code < 300; }
};

// Minimal HTTP client interface used by everything above it (OAuth + Health
// API calls). Each platform provides one implementation (see CurlHttpClient,
// Esp32HttpClient).
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    virtual HttpResponse Get(const std::string& url,
                              const std::vector<std::string>& headers) = 0;

    virtual HttpResponse PostForm(const std::string& url,
                                   const std::vector<std::string>& headers,
                                   const std::string& form_body) = 0;
};

}  // namespace healthtracker
