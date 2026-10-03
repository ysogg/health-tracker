#include "esp32_http_client.h"

#include <WiFiClientSecure.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

// Talks raw HTTP/1.1 directly over WiFiClientSecure instead of the Arduino
// HTTPClient library. MAX_BODY_BYTES below is a backstop against the Google
// Health API's chunked pages occasionally running large (40KB+ seen live),
// so an oversized response fails cleanly instead of exhausting the heap.

namespace {

constexpr size_t MAX_BODY_BYTES = 40960;
// 15s: dailyRollUp for a densely-recorded type (e.g. active-zone-minutes)
// can take longer server-side than 8s consistently allowed for.
constexpr unsigned long IDLE_TIMEOUT_MS = 15000;

struct ParsedUrl {
    std::string host;
    std::string path;
};

// This project only ever talks to https:// URLs.
ParsedUrl ParseUrl(const std::string& url) {
    const std::string prefix = "https://";
    const size_t host_start = (url.rfind(prefix, 0) == 0) ? prefix.size() : 0;
    const size_t slash = url.find('/', host_start);

    ParsedUrl parsed;
    if (slash == std::string::npos) {
        parsed.host = url.substr(host_start);
        parsed.path = "/";
    } else {
        parsed.host = url.substr(host_start, slash - host_start);
        parsed.path = url.substr(slash);
    }
    return parsed;
}

bool WaitForData(WiFiClientSecure& client) {
    const unsigned long start = millis();
    while (!client.available()) {
        if (!client.connected()) return false;
        if (millis() - start > IDLE_TIMEOUT_MS) return false;
        delay(1);
    }
    return true;
}

int ReadByte(WiFiClientSecure& client) {
    if (!WaitForData(client)) return -1;
    return client.read();
}

// Reads one CRLF- or LF-terminated line (the terminator itself is
// discarded). Returns an empty string on timeout/disconnect.
std::string ReadLine(WiFiClientSecure& client) {
    std::string line;
    while (true) {
        const int c = ReadByte(client);
        if (c < 0) break;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            break;
        }
        line += static_cast<char>(c);
        if (line.size() > 512) break;  // a real status/header line is short
    }
    return line;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string ReadChunkedBody(WiFiClientSecure& client) {
    std::string body;
    while (body.size() < MAX_BODY_BYTES) {
        const std::string size_line = ReadLine(client);
        if (size_line.empty()) break;

        const long chunk_size = strtol(size_line.c_str(), nullptr, 16);
        if (chunk_size <= 0) {
            ReadLine(client);  // trailing CRLF after the terminal "0" chunk
            break;
        }

        const long to_read =
            std::min<long>(chunk_size, static_cast<long>(MAX_BODY_BYTES - body.size()));
        for (long i = 0; i < to_read; ++i) {
            const int c = ReadByte(client);
            if (c < 0) return body;
            body += static_cast<char>(c);
        }
        if (to_read < chunk_size) break;  // hit the cap mid-chunk

        ReadLine(client);  // trailing CRLF after this chunk's data
    }
    return body;
}

std::string ReadIdentityBody(WiFiClientSecure& client, long content_length) {
    std::string body;
    // content_length < 0 means the server didn't say — read until it closes
    // the connection (guaranteed by the "Connection: close" request header).
    while (body.size() < MAX_BODY_BYTES &&
           (content_length < 0 || static_cast<long>(body.size()) < content_length)) {
        const int c = ReadByte(client);
        if (c < 0) break;
        body += static_cast<char>(c);
    }
    return body;
}

healthtracker::HttpResponse SendRequest(const std::string& method, const std::string& url,
                                          const std::vector<std::string>& headers,
                                          const std::string& body_to_send) {
    const ParsedUrl parsed = ParseUrl(url);

    WiFiClientSecure client;
    // TODO: pin Google's root CA with client.setCACert(...) instead of
    // skipping certificate verification
    client.setInsecure();
    client.setTimeout(10);
    // 15s handshake timeout matches IDLE_TIMEOUT_MS and stays well under the
    // 90s hardware watchdog, which nothing feeds while connect() blocks.
    client.setHandshakeTimeout(15);
    if (!client.connect(parsed.host.c_str(), 443)) {
        throw std::runtime_error("Failed to connect to " + parsed.host);
    }

    std::ostringstream request;
    request << method << " " << parsed.path << " HTTP/1.1\r\n"
            << "Host: " << parsed.host << "\r\n"
            << "Connection: close\r\n";
    for (const auto& header : headers) {
        request << header << "\r\n";
    }
    if (!body_to_send.empty()) {
        request << "Content-Length: " << body_to_send.size() << "\r\n";
    }
    request << "\r\n" << body_to_send;
    client.print(request.str().c_str());

    const std::string status_line = ReadLine(client);
    if (status_line.empty()) {
        throw std::runtime_error("No response from " + parsed.host);
    }
    int status_code = 0;
    if (const auto first_space = status_line.find(' '); first_space != std::string::npos) {
        status_code = atoi(status_line.c_str() + first_space + 1);
    }

    long content_length = -1;
    bool chunked = false;
    for (;;) {
        const std::string header_line = ReadLine(client);
        if (header_line.empty()) break;  // blank line ends the headers
        const std::string lower = ToLower(header_line);
        if (lower.rfind("content-length:", 0) == 0) {
            content_length = atol(header_line.c_str() + 15);
        } else if (lower.rfind("transfer-encoding:", 0) == 0 &&
                   lower.find("chunked") != std::string::npos) {
            chunked = true;
        }
    }

    healthtracker::HttpResponse response;
    response.status_code = status_code;
    response.body = chunked ? ReadChunkedBody(client) : ReadIdentityBody(client, content_length);

    if (response.body.size() >= MAX_BODY_BYTES) {
        throw std::runtime_error("HTTP response body hit the " +
                                  std::to_string(MAX_BODY_BYTES) +
                                  "-byte safety cap — treating as a corrupted read");
    }
    return response;
}

}  // namespace

healthtracker::HttpResponse Esp32HttpClient::Get(const std::string& url,
                                                   const std::vector<std::string>& headers) {
    return SendRequest("GET", url, headers, "");
}

healthtracker::HttpResponse Esp32HttpClient::PostForm(const std::string& url,
                                                        const std::vector<std::string>& headers,
                                                        const std::string& form_body) {
    return SendRequest("POST", url, headers, form_body);
}
