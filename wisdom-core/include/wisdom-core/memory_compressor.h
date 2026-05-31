#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace weasel {

// 记忆压缩：将旧的上下文词序列通过 LLM 压缩为更短的摘要
class MemoryCompressor {
public:
    MemoryCompressor();
    ~MemoryCompressor();

    // 从配置加载
    bool LoadConfig(const std::string& config_name);

    // 是否启用且可用
    bool IsAvailable() const { return m_enabled && !m_api_url.empty(); }

    // 异步压缩
    void CompressAsync(const std::vector<std::wstring>& words,
                       std::function<void(std::vector<std::wstring>)> callback);

    // 设置日志回调
    void SetLogCallback(std::function<void(const std::string&)> cb) { m_log_cb = std::move(cb); }

private:
    bool ExecuteRequest(const std::string& url,
                        const std::string& request_body,
                        std::string& response_body);
    std::vector<std::wstring> ParseResponse(const std::string& json_response);
    void CloseConnection();

    void Log(const std::string& msg);

    bool m_enabled;
    std::string m_api_url;
    std::string m_api_key;
    std::string m_model;
    int m_max_tokens;
    std::unique_ptr<class HttpClient> m_http;
    std::string m_cached_url;
    std::function<void(const std::string&)> m_log_cb;
};

} // namespace weasel
