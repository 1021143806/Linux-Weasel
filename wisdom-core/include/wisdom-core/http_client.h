#pragma once

#include <string>
#include <memory>

namespace weasel {

// 简单的 HTTP 客户端封装，使用 libcurl
// 替代 Windows 上的 WinHTTP
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // 执行 POST 请求，返回 true 表示成功
    bool Post(const std::string& url,
              const std::string& headers,
              const std::string& body,
              std::string& response_body,
              unsigned int timeout_ms = 10000);

    // 关闭并清理连接
    void Close();

private:
    void* m_curl;  // CURL*
};

} // namespace weasel
