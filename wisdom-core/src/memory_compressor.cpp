#include "wisdom-core/memory_compressor.h"
#include "wisdom-core/http_client.h"
#include "wisdom-core/string_utils.h"
#include <sstream>
#include <future>

namespace weasel {

namespace {

std::wstring WordsToSpaceSeparated(const std::vector<std::wstring>& words) {
    std::wstringstream ss;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) ss << L" ";
        ss << words[i];
    }
    return ss.str();
}

} // anonymous namespace

MemoryCompressor::MemoryCompressor()
    : m_enabled(false),
      m_max_tokens(100),
      m_http(std::make_unique<HttpClient>()) {
}

MemoryCompressor::~MemoryCompressor() {
    CloseConnection();
}

void MemoryCompressor::CloseConnection() {
    m_http->Close();
    m_cached_url.clear();
}

void MemoryCompressor::Log(const std::string& msg) {
    if (m_log_cb) m_log_cb(msg);
}

bool MemoryCompressor::LoadConfig(const std::string& config_name) {
    // 从环境变量加载配置
    const char* env_url = std::getenv("WEASEL_MEMORY_API_URL");
    const char* env_key = std::getenv("WEASEL_MEMORY_API_KEY");
    const char* env_model = std::getenv("WEASEL_MEMORY_MODEL");

    if (env_url) {
        m_api_url = env_url;
        m_enabled = true;
    } else {
        m_enabled = false;
        Log("[Memory] Not configured (set WEASEL_MEMORY_API_URL to enable)");
        return true;
    }

    m_api_key = env_key ? env_key : "";
    m_model = env_model ? env_model : "gpt-3.5-turbo";

    Log("[Memory] LoadConfig: api_url = " + m_api_url);
    return true;
}

void MemoryCompressor::CompressAsync(
    const std::vector<std::wstring>& words,
    std::function<void(std::vector<std::wstring>)> callback) {
    if (!IsAvailable() || words.empty()) {
        if (callback) callback(std::vector<std::wstring>());
        return;
    }

    std::wstring words_str = WordsToSpaceSeparated(words);
    std::string prompt_utf8 = wstring_to_utf8(
        L"请将以下用户输入历史的词序列压缩为更短的摘要，保留关键信息。"
        L"只输出压缩后的词，词语间用单个空格分隔，不要任何解释或标点，不超过10个词。\n\n词序列：\"" +
        words_str + L"\"");

    std::string escaped_prompt = escape_json_string(prompt_utf8);

    std::ostringstream json;
    json << "{\"model\":\"" << m_model << "\","
         << "\"messages\":[{\"role\":\"user\",\"content\":\"" << escaped_prompt << "\"}],"
         << "\"max_tokens\":" << m_max_tokens << "}";

    std::string request_body = json.str();
    std::string api_url = m_api_url;
    std::string api_key = m_api_key;

    auto future = std::async(std::launch::async, [this, request_body, api_url, api_key, callback]() {
        std::string response_body;
        if (!ExecuteRequest(api_url, request_body, response_body)) {
            if (callback) callback(std::vector<std::wstring>());
            return;
        }
        std::vector<std::wstring> result = ParseResponse(response_body);
        if (callback) callback(result);
    });
    (void)future; // 忽略返回值，异步执行
}

bool MemoryCompressor::ExecuteRequest(const std::string& url,
                                       const std::string& request_body,
                                       std::string& response_body) {
    std::string headers = "Content-Type: application/json\r\n";
    if (!m_api_key.empty()) {
        headers += "Authorization: Bearer " + m_api_key + "\r\n";
    }
    return m_http->Post(url, headers, request_body, response_body, 15000);
}

std::vector<std::wstring> MemoryCompressor::ParseResponse(const std::string& json_response) {
    std::vector<std::wstring> words;
    size_t content_pos = json_response.find("\"content\"");
    if (content_pos == std::string::npos) return words;
    size_t colon_pos = json_response.find(':', content_pos);
    if (colon_pos == std::string::npos) return words;
    size_t quote_start = json_response.find('"', colon_pos);
    if (quote_start == std::string::npos) return words;
    size_t quote_end = json_response.find('"', quote_start + 1);
    if (quote_end == std::string::npos) return words;
    std::string content = json_response.substr(quote_start + 1, quote_end - quote_start - 1);
    std::wstring content_w = utf8_to_wstring(content);
    std::wstringstream ss(content_w);
    std::wstring word;
    while (ss >> word) {
        if (!word.empty()) words.push_back(word);
    }
    return words;
}

} // namespace weasel
