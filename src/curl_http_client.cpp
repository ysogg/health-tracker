#include "healthtracker/curl_http_client.hpp"

#include <curl/curl.h>

#include <stdexcept>

namespace healthtracker {

namespace {

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

curl_slist* BuildHeaderList(const std::vector<std::string>& headers) {
    curl_slist* list = nullptr;
    for (const auto& header : headers) {
        list = curl_slist_append(list, header.c_str());
    }
    return list;
}

HttpResponse Perform(CURL* curl, curl_slist* header_list) {
    std::string body;
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (header_list) curl_slist_free_all(header_list);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("HTTP request failed: ") +
                                  curl_easy_strerror(res));
    }

    HttpResponse response;
    response.body = std::move(body);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    return response;
}

}  // namespace

CurlHttpClient::CurlHttpClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }

CurlHttpClient::~CurlHttpClient() { curl_global_cleanup(); }

HttpResponse CurlHttpClient::Get(const std::string& url,
                                  const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    HttpResponse response = Perform(curl, BuildHeaderList(headers));
    curl_easy_cleanup(curl);
    return response;
}

HttpResponse CurlHttpClient::PostForm(const std::string& url,
                                       const std::vector<std::string>& headers,
                                       const std::string& form_body) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                      static_cast<long>(form_body.size()));

    HttpResponse response = Perform(curl, BuildHeaderList(headers));
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace healthtracker
