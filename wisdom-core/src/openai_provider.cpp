#include "wisdom-core/llm_provider.h"
#include "wisdom-core/http_client.h"
#include "wisdom-core/string_utils.h"
#include <sstream>
#include <algorithm>

namespace weasel {

OpenAICompatibleProvider::OpenAICompatibleProvider()
    : m_enabled(false),
      m_max_tokens(10),
      m_temperature(0.7),
      m_top_p(1.0),
      m_presence_penalty(0.0),
      m_frequency_penalty(0.0),
      m_has_seed(false),
      m_seed(0),
      m_http(std::make_unique<HttpClient>()) {
}

OpenAICompatibleProvider::~OpenAICompatibleProvider() {
    CloseConnection();
}

void OpenAICompatibleProvider::CloseConnection() {
    m_http->Close();
    m_cached_url.clear();
}

void OpenAICompatibleProvider::Log(const std::string& msg) {
    if (m_log_cb) m_log_cb(msg);
}

bool OpenAICompatibleProvider::LoadConfig(const std::string& config_name) {
    // 简化版：从环境变量或硬编码配置加载
    // 在完整实现中，这里会通过 librime API 读取 weasel.yaml
    // 对于 Linux 守护进程，配置将从独立的 YAML 文件读取

    // 默认使用环境变量配置
    const char* env_url = std::getenv("WEASEL_LLM_API_URL");
    const char* env_key = std::getenv("WEASEL_LLM_API_KEY");
    const char* env_model = std::getenv("WEASEL_LLM_MODEL");

    if (env_url) {
        m_api_url = env_url;
        m_enabled = true;
    } else {
        // 默认值
        m_api_url = "http://localhost:11434/v1/chat/completions";
        m_enabled = true;
    }

    if (env_key) m_api_key = env_key;
    if (env_model) m_model = env_model;
    else m_model = "qwen3:8b";

    Log("[LLM] LoadConfig: api_url = " + m_api_url);
    Log("[LLM] LoadConfig: model = " + m_model);

    return true;
}

std::vector<std::wstring> OpenAICompatibleProvider::PredictCandidates(
    const std::wstring& context,
    const std::wstring& current_input,
    size_t max_candidates) {
    std::vector<std::wstring> candidates;

    if (!IsAvailable()) {
        return candidates;
    }

    // 构建 prompt
    std::wstring prompt = L"你是一个智能中文输入法，请根据以下上下文和当前输入，预测接下来最可能出现的" +
        std::to_wstring(max_candidates) + L"个候选词。\n\n"
        L"要求：\n"
        L"1. 只返回候选词，不要任何解释或标点\n"
        L"2. 候选词之间用单个空格分隔\n"
        L"3. 按可能性从高到低排列\n"
        L"4. 如果上下文为空或无关，仅基于当前输入预测\n"
        L"5. 确保候选词都是有效的中文词汇或常用短语\n"
        L"6. 返回词数严格不超过" + std::to_wstring(max_candidates) + L"个\n\n"
        L"上下文：\"" + context + L"\"\n"
        L"当前输入：\"" + current_input + L"\"\n"
        L"候选词：";

    std::string prompt_utf8 = wstring_to_utf8(prompt);
    std::string escaped_prompt = escape_json_string(prompt_utf8);

    // 构建 JSON 请求体
    std::ostringstream json;
    json << "{"
         << "\"model\":\"" << m_model << "\","
         << "\"messages\":["
         << "{\"role\":\"user\",\"content\":\"" << escaped_prompt << "\"}"
         << "],"
         << "\"max_tokens\":" << m_max_tokens << ","
         << "\"temperature\":" << m_temperature;

    json << ",\"top_p\":" << m_top_p
         << ",\"presence_penalty\":" << m_presence_penalty
         << ",\"frequency_penalty\":" << m_frequency_penalty;

    if (m_has_seed) {
        json << ",\"seed\":" << m_seed;
    }

    if (!m_extra_body_json.empty()) {
        size_t start = m_extra_body_json.find_first_not_of(" \t\r\n");
        size_t end = m_extra_body_json.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos &&
            m_extra_body_json[start] == '{' && m_extra_body_json[end] == '}') {
            std::string inner = m_extra_body_json.substr(start + 1, end - start - 1);
            if (!inner.empty()) {
                json << "," << inner;
            }
        }
    }

    json << "}";
    std::string request_body = json.str();

    Log("[LLM] Sending prediction request to: " + m_api_url);

    // 执行 HTTP 请求
    std::string response_body;
    std::string headers = "Content-Type: application/json\r\n";
    if (!m_api_key.empty()) {
        headers += "Authorization: Bearer " + m_api_key + "\r\n";
    }

    if (!ExecuteRequest(m_api_url, request_body, response_body)) {
        Log("[LLM] Request failed");
        return candidates;
    }

    // 解析响应
    candidates = ParseResponse(response_body);
    Log("[LLM] Got " + std::to_string(candidates.size()) + " candidates");

    return candidates;
}

bool OpenAICompatibleProvider::IsAvailable() const {
    return m_enabled && !m_api_url.empty();
}

bool OpenAICompatibleProvider::ExecuteRequest(const std::string& url,
                                               const std::string& request_body,
                                               std::string& response_body) {
    std::string headers = "Content-Type: application/json\r\n";
    if (!m_api_key.empty()) {
        headers += "Authorization: Bearer " + m_api_key + "\r\n";
    }
    return m_http->Post(url, headers, request_body, response_body, 10000);
}

std::vector<std::wstring> OpenAICompatibleProvider::ParseResponse(
    const std::string& json_response) {
    std::vector<std::wstring> candidates;

    // 简单的 JSON 解析（查找 content 字段）
    size_t content_pos = json_response.find("\"content\"");
    if (content_pos == std::string::npos) return candidates;

    size_t colon_pos = json_response.find(':', content_pos);
    if (colon_pos == std::string::npos) return candidates;

    size_t quote_start = json_response.find('"', colon_pos);
    if (quote_start == std::string::npos) return candidates;

    size_t quote_end = json_response.find('"', quote_start + 1);
    if (quote_end == std::string::npos) return candidates;

    std::string content = json_response.substr(quote_start + 1,
                                                quote_end - quote_start - 1);
    std::wstring content_w = utf8_to_wstring(content);

    // 按空格分割
    return split_by_space_w(content_w);
}

// ========== HFConstraintProvider ==========

HFConstraintProvider::HFConstraintProvider()
    : m_enabled(false),
      m_http(std::make_unique<HttpClient>()) {
}

HFConstraintProvider::~HFConstraintProvider() {
    CloseConnection();
}

void HFConstraintProvider::CloseConnection() {
    m_http->Close();
    m_cached_url.clear();
}

void HFConstraintProvider::Log(const std::string& msg) {
    if (m_log_cb) m_log_cb(msg);
}

bool HFConstraintProvider::LoadConfig(const std::string& config_name) {
    const char* env_url = std::getenv("WEASEL_HF_API_URL");
    if (env_url) {
        m_api_url = env_url;
        m_enabled = true;
    } else {
        m_api_url = "http://localhost:8000/v1/generate/completions";
        m_enabled = true;
    }
    Log("[HF] LoadConfig: api_url = " + m_api_url);
    return true;
}

std::vector<std::wstring> HFConstraintProvider::PredictCandidates(
    const std::wstring& context,
    const std::wstring& current_input,
    size_t max_candidates) {
    std::vector<std::wstring> candidates;
    if (!IsAvailable()) return candidates;

    std::string context_utf8 = wstring_to_utf8(context);
    std::string input_utf8 = wstring_to_utf8(current_input);

    // 构建请求体
    std::ostringstream json;
    json << "{\"prompt\":\"" << escape_json_string(context_utf8) << "\","
         << "\"pinyin_constraints\":[\"" << escape_json_string(input_utf8) << "\"]}";

    std::string response_body;
    std::string headers = "Content-Type: application/json\r\n";
    if (!m_http->Post(m_api_url, headers, json.str(), response_body, 10000)) {
        Log("[HF] Request failed");
        return candidates;
    }

    // 解析响应
    candidates = ParseResponse(response_body);
    return candidates;
}

bool HFConstraintProvider::IsAvailable() const {
    return m_enabled && !m_api_url.empty();
}

bool HFConstraintProvider::ExecuteRequest(const std::string& url,
                                           const std::string& request_body,
                                           std::string& response_body) {
    std::string headers = "Content-Type: application/json\r\n";
    return m_http->Post(url, headers, request_body, response_body, 10000);
}

std::vector<std::wstring> HFConstraintProvider::ParseResponse(
    const std::string& json_response) {
    std::vector<std::wstring> candidates;
    size_t content_pos = json_response.find("\"responses\"");
    if (content_pos == std::string::npos) return candidates;
    size_t colon_pos = json_response.find(':', content_pos);
    if (colon_pos == std::string::npos) return candidates;
    size_t quote_start = json_response.find('"', colon_pos);
    if (quote_start == std::string::npos) return candidates;
    size_t quote_end = json_response.find('"', quote_start + 1);
    if (quote_end == std::string::npos) return candidates;
    std::string content = json_response.substr(quote_start + 1, quote_end - quote_start - 1);
    std::wstring content_w = utf8_to_wstring(content);
    return split_by_space_w(content_w);
}

// ========== LlamaCppProvider (stub) ==========

LlamaCppProvider::LlamaCppProvider()
    : m_enabled(false),
      m_n_ctx(2048),
      m_n_gpu_layers(-1),
      m_max_tokens(8),
      m_temperature(0.6),
      m_top_k(40),
      m_top_p(0.9),
      m_repeat_penalty(1.1),
      m_presence_penalty(0.0),
      m_frequency_penalty(0.0),
      m_mirostat(0),
      m_min_p(0.0),
      m_typical_p(1.0),
      m_n_threads(4),
      m_instruct_model(false),
      m_model(nullptr),
      m_context(nullptr),
      m_sampler(nullptr),
      m_memory(nullptr),
      m_vocab(nullptr),
      m_ctx_size(0),
      m_system_state_size(0),
      m_system_prompt_ready(false),
      m_model_loaded(false) {
}

LlamaCppProvider::~LlamaCppProvider() {
    Cleanup();
}

void LlamaCppProvider::Log(const std::string& msg) {
    if (m_log_cb) m_log_cb(msg);
}

bool LlamaCppProvider::LoadConfig(const std::string& config_name) {
    const char* env_path = std::getenv("WEASEL_LLAMACPP_MODEL_PATH");
    if (env_path) {
        m_model_path = env_path;
        m_enabled = true;
        Log("[llama.cpp] LoadConfig: model_path = " + m_model_path);
        return true;
    }
    Log("[llama.cpp] No model path configured (set WEASEL_LLAMACPP_MODEL_PATH)");
    return false;
}

bool LlamaCppProvider::IsAvailable() const {
    return m_enabled && m_model_loaded;
}

std::vector<std::wstring> LlamaCppProvider::PredictCandidates(
    const std::wstring& context,
    const std::wstring& current_input,
    size_t max_candidates) {
    // llama.cpp 集成需要链接 libllama，此处为 stub
    // 完整实现需要包含 llama.h 并调用 llama API
    Log("[llama.cpp] PredictCandidates called (stub)");
    return {};
}

bool LlamaCppProvider::InitializeModel() {
    Log("[llama.cpp] InitializeModel (stub)");
    return false;
}

void LlamaCppProvider::Cleanup() {
    Log("[llama.cpp] Cleanup (stub)");
}

std::string LlamaCppProvider::GenerateText(const std::string& prompt, size_t max_tokens) {
    return "";
}

std::vector<std::string> LlamaCppProvider::GenerateCandidatesBatch(
    const std::string& system_prompt_utf8,
    const std::string& user_prompt_utf8,
    size_t n_parallel, int max_new_tokens) {
    return {};
}

bool LlamaCppProvider::PrepareSystemPrompt(const std::string& system_prompt_utf8) {
    return false;
}

// ========== Factory ==========

std::unique_ptr<LLMProvider> CreateLLMProvider(const std::string& provider_type) {
    if (provider_type == "llamacpp") {
        return std::make_unique<LlamaCppProvider>();
    } else if (provider_type == "hf_constraint") {
        return std::make_unique<HFConstraintProvider>();
    } else {
        return std::make_unique<OpenAICompatibleProvider>();
    }
}

} // namespace weasel
