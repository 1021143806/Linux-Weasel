#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace weasel {

// LLM 提供者抽象基类
class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    // 从配置文件加载配置
    virtual bool LoadConfig(const std::string& config_name) = 0;

    // 预测候选词
    virtual std::vector<std::wstring> PredictCandidates(
        const std::wstring& context,
        const std::wstring& current_input,
        size_t max_candidates) = 0;

    // 检查 LLM 是否可用
    virtual bool IsAvailable() const = 0;

    // 获取提供者名称
    virtual std::string GetProviderName() const = 0;
};

// 日志回调类型
using LogCallback = std::function<void(const std::string&)>;

// OpenAI 兼容接口提供者（平台无关版本，使用 libcurl）
class OpenAICompatibleProvider : public LLMProvider {
public:
    OpenAICompatibleProvider();
    ~OpenAICompatibleProvider() override;

    bool LoadConfig(const std::string& config_name) override;
    std::vector<std::wstring> PredictCandidates(
        const std::wstring& context,
        const std::wstring& current_input,
        size_t max_candidates) override;
    bool IsAvailable() const override;
    std::string GetProviderName() const override { return "OpenAI Compatible"; }

    // 设置日志回调
    void SetLogCallback(LogCallback cb) { m_log_cb = std::move(cb); }

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
    double m_temperature;
    double m_top_p;
    double m_presence_penalty;
    double m_frequency_penalty;
    bool m_has_seed;
    int m_seed;
    std::string m_extra_body_json;

    // HTTP 客户端
    std::unique_ptr<class HttpClient> m_http;
    std::string m_cached_url;

    // 日志回调
    LogCallback m_log_cb;
};

// llama.cpp 本地推理提供者（平台无关版本）
class LlamaCppProvider : public LLMProvider {
public:
    LlamaCppProvider();
    ~LlamaCppProvider() override;

    bool LoadConfig(const std::string& config_name) override;
    std::vector<std::wstring> PredictCandidates(
        const std::wstring& context,
        const std::wstring& current_input,
        size_t max_candidates) override;
    bool IsAvailable() const override;
    std::string GetProviderName() const override { return "llama.cpp Local"; }

    void SetLogCallback(LogCallback cb) { m_log_cb = std::move(cb); }

private:
    bool InitializeModel();
    void Cleanup();
    std::string GenerateText(const std::string& prompt, size_t max_tokens);
    std::vector<std::string> GenerateCandidatesBatch(
        const std::string& system_prompt_utf8,
        const std::string& user_prompt_utf8,
        size_t n_parallel, int max_new_tokens);
    bool PrepareSystemPrompt(const std::string& system_prompt_utf8);

    void Log(const std::string& msg);

    bool m_enabled;
    std::string m_model_path;
    int m_n_ctx;
    int m_n_gpu_layers;
    int m_max_tokens;
    double m_temperature;
    int m_top_k;
    double m_top_p;
    double m_repeat_penalty;
    double m_presence_penalty;
    double m_frequency_penalty;
    int m_mirostat;
    double m_min_p;
    double m_typical_p;
    int m_n_threads;
    bool m_instruct_model;

    // llama.cpp 对象（前向声明）
    void* m_model;      // llama_model*
    void* m_context;    // llama_context*
    void* m_sampler;    // llama_sampler*
    void* m_memory;     // llama_memory_t
    void* m_vocab;      // const llama_vocab*
    int m_ctx_size;
    std::string m_system_prompt_utf8;
    std::vector<uint8_t> m_system_state;
    size_t m_system_state_size;
    bool m_system_prompt_ready;
    bool m_model_loaded;

    LogCallback m_log_cb;
};

// HF Constraint 接口提供者
class HFConstraintProvider : public LLMProvider {
public:
    HFConstraintProvider();
    ~HFConstraintProvider() override;

    bool LoadConfig(const std::string& config_name) override;
    std::vector<std::wstring> PredictCandidates(
        const std::wstring& context,
        const std::wstring& current_input,
        size_t max_candidates) override;
    bool IsAvailable() const override;
    std::string GetProviderName() const override { return "HF Constraint"; }

    void SetLogCallback(LogCallback cb) { m_log_cb = std::move(cb); }

private:
    bool ExecuteRequest(const std::string& url,
                        const std::string& request_body,
                        std::string& response_body);
    std::vector<std::wstring> ParseResponse(const std::string& json_response);
    void CloseConnection();

    void Log(const std::string& msg);

    bool m_enabled;
    std::string m_api_url;
    std::unique_ptr<class HttpClient> m_http;
    std::string m_cached_url;
    LogCallback m_log_cb;
};

// 创建 LLM Provider 的工厂函数
std::unique_ptr<LLMProvider> CreateLLMProvider(const std::string& provider_type);

} // namespace weasel
