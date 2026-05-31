#include "wisdom-core/http_client.h"
#include <curl/curl.h>
#include <cstring>
#include <sstream>

namespace weasel {

namespace {

// libcurl 写回调
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total);
    return total;
}

// 解析 URL 获取主机名和路径（简化版，不处理复杂 URL）
struct ParsedUrl {
    std::string scheme;
    std::string host;
    int port;
    std::string path;
};

bool ParseUrl(const std::string& url, ParsedUrl& parsed) {
    // 简单 URL 解析
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;
    parsed.scheme = url.substr(0, scheme_end);

    size_t host_start = scheme_end + 3;
    size_t host_end = url.find('/', host_start);
    size_t port_colon = url.find(':', host_start);

    if (host_end == std::string::npos) host_end = url.length();

    if (port_colon != std::string::npos && port_colon < host_end) {
        parsed.host = url.substr(host_start, port_colon - host_start);
        parsed.port = std::stoi(url.substr(port_colon + 1, host_end - port_colon - 1));
    } else {
        parsed.host = url.substr(host_start, host_end - host_start);
        parsed.port = (parsed.scheme == "https") ? 443 : 80;
    }

    if (host_end < url.length()) {
        parsed.path = url.substr(host_end);
    } else {
        parsed.path = "/";
    }

    return true;
}

} // anonymous namespace

HttpClient::HttpClient()
    : m_curl(nullptr) {
    m_curl = curl_easy_init();
}

HttpClient::~HttpClient() {
    Close();
}

bool HttpClient::Post(const std::string& url,
                      const std::string& headers,
                      const std::string& body,
                      std::string& response_body,
                      unsigned int timeout_ms) {
    if (!m_curl) {
        m_curl = curl_easy_init();
        if (!m_curl) return false;
    }

    CURL* curl = static_cast<CURL*>(m_curl);

    // 重置 curl 句柄
    curl_easy_reset(curl);

    // 设置 URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // POST 请求
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());

    // 设置响应回调
    response_body.clear();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    // 设置超时
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)timeout_ms);

    // 设置自定义 headers
    struct curl_slist* header_list = nullptr;
    if (!headers.empty()) {
        std::istringstream iss(headers);
        std::string line;
        while (std::getline(iss, line)) {
            // 去除 \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                header_list = curl_slist_append(header_list, line.c_str());
            }
        }
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    // 对于 HTTPS 跳过证书验证（与 WinHTTP 默认行为一致）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // 执行请求
    CURLcode res = curl_easy_perform(curl);

    // 清理 headers
    if (header_list) {
        curl_slist_free_all(header_list);
    }

    return (res == CURLE_OK);
}

void HttpClient::Close() {
    if (m_curl) {
        curl_easy_cleanup(static_cast<CURL*>(m_curl));
        m_curl = nullptr;
    }
}

} // namespace weasel
