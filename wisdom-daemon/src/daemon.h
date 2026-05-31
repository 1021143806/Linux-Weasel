#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>

#include "wisdom-core/llm_provider.h"
#include "wisdom-core/context_history.h"
#include "wisdom-core/memory_compressor.h"

namespace weasel {

// Wisdom-Weasel Linux 守护进程
// 负责：
// 1. 通过 librime API 监听用户输入
// 2. 调用 LLM 预测候选词
// 3. 通过 Unix Domain Socket 与 fcitx5 插件通信
class WisdomDaemon {
public:
    WisdomDaemon();
    ~WisdomDaemon();

    // 初始化并运行守护进程
    int Run(int argc, char* argv[]);

    // 停止守护进程
    void Stop();

private:
    // 初始化组件
    bool InitializeRime();
    bool InitializeLLM();
    bool InitializeIPC();
    bool InitializeLogging();

    // Rime 事件处理
    void RimeEventLoop();

    // IPC 处理
    void IPCEventLoop();

public:
    // 触发 LLM 预测（公开，供回调使用）
    void TriggerPrediction(const std::wstring& context, const std::wstring& current_input);

    // 获取上下文历史（公开，供回调使用）
    ContextHistory* GetContextHistory() { return m_context_history.get(); }

    // 日志（公开，供回调使用）
    void Log(const std::string& msg);

private:

    // 组件
    std::unique_ptr<LLMProvider> m_llm_provider;
    std::unique_ptr<ContextHistory> m_context_history;
    std::unique_ptr<MemoryCompressor> m_memory_compressor;

    // 线程控制
    std::atomic<bool> m_running;
    std::thread m_rime_thread;
    std::thread m_ipc_thread;

    // IPC socket 路径
    std::string m_socket_path;

    // 当前 LLM 候选词
    std::vector<std::wstring> m_current_candidates;
    std::mutex m_candidate_mutex;
};

} // namespace weasel
