#pragma once

#include "healthtracker/http_client.hpp"

namespace healthtracker {

// libcurl-backed IHttpClient. This is the desktop/terminal implementation;
// it is the only file in the project that includes <curl/curl.h>.
class CurlHttpClient : public IHttpClient {
public:
    CurlHttpClient();
    ~CurlHttpClient() override;

    HttpResponse Get(const std::string& url,
                      const std::vector<std::string>& headers) override;

    HttpResponse PostForm(const std::string& url,
                           const std::vector<std::string>& headers,
                           const std::string& form_body) override;
};

}  // namespace healthtracker
