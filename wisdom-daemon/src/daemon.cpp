#include "daemon.h"
#include "wisdom-core/string_utils.h"

#include <iostream>
#include <fstream>
#include <csignal>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

// librime API
#include <rime_api.h>

namespace weasel {

namespace {

WisdomDaemon* g_daemon = nullptr;

void SignalHandler(int sig) {
    if (g_daemon) {
        std::cerr << "Received signal " << sig << ", shutting down..." << std::endl;
        g_daemon->Stop();
    }
}

// Rime 通知回调
void RimeNotificationHandler(void* context_object,
                              uintptr_t session_id,
                              const char* message_type,
                              const char* message_value) {
    auto* daemon = static_cast<WisdomDaemon*>(context_object);
    if (!daemon || !message_type || !message_value) return;

    // 处理 commit 事件
    if (strcmp(message_type, "commit") == 0) {
        std::string commit_text(message_value);
        if (!commit_text.empty()) {
            std::wstring commit_w = utf8_to_wstring(commit_text);
            daemon->Log("[Rime] User committed: " + commit_text);

            // 添加到上下文历史
            if (auto* history = daemon->GetContextHistory()) {
                history->AddText(commit_w);
            }

            // 触发 LLM 预测
            daemon->TriggerPrediction(commit_w, L"");
        }
    }
}

} // anonymous namespace

WisdomDaemon::WisdomDaemon()
    : m_running(false),
      m_socket_path("/tmp/wisdom-weasel.sock") {
}

WisdomDaemon::~WisdomDaemon() {
    Stop();
}

void WisdomDaemon::Log(const std::string& msg) {
    std::cerr << "[Wisdom-Weasel] " << msg << std::endl;
}

bool WisdomDaemon::InitializeLogging() {
    // 简单日志：输出到 stderr
    // 生产环境应使用 syslog 或文件日志
    Log("Starting Wisdom-Weasel daemon...");
    return true;
}

bool WisdomDaemon::InitializeRime() {
    Log("Initializing Rime...");

    RimeApi* rime = rime_get_api();
    if (!rime) {
        Log("ERROR: Failed to get Rime API");
        return false;
    }

    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = "/usr/share/rime-data";
    traits.user_data_dir = (std::string(std::getenv("HOME")) + "/.local/share/fcitx5/rime").c_str();
    traits.distribution_name = "Rime";
    traits.distribution_code_name = "wisdom-weasel";
    traits.distribution_version = "1.0.0";
    traits.app_name = "rime.wisdom-weasel";

    rime->setup(&traits);
    rime->initialize(nullptr);

    // 设置通知回调
    rime->set_notification_handler(&RimeNotificationHandler, this);

    Log("Rime initialized successfully");
    return true;
}

bool WisdomDaemon::InitializeLLM() {
    Log("Initializing LLM...");

    // 从环境变量读取 provider 类型
    const char* provider_type = std::getenv("WEASEL_PROVIDER_TYPE");
    if (!provider_type) provider_type = "openai";

    m_llm_provider = CreateLLMProvider(provider_type);
    if (!m_llm_provider) {
        Log("ERROR: Failed to create LLM provider");
        return false;
    }

    // 设置日志回调（通过 dynamic_cast 检查是否支持）
    // 所有 provider 都继承自 LLMProvider，基类不包含 SetLogCallback
    // 直接使用 Log 函数记录日志

    // 加载配置
    if (!m_llm_provider->LoadConfig("weasel")) {
        Log("WARNING: LLM provider loaded with default config");
    }

    // 初始化上下文历史
    m_context_history = std::make_unique<ContextHistory>(50);
    m_context_history->SetLogCallback([this](const std::string& msg) {
        Log(msg);
    });

    // 初始化记忆压缩
    m_memory_compressor = std::make_unique<MemoryCompressor>();
    m_memory_compressor->SetLogCallback([this](const std::string& msg) {
        Log(msg);
    });
    m_memory_compressor->LoadConfig("weasel");
    m_context_history->SetMemoryCompressor(m_memory_compressor.get());

    Log("LLM initialized: " + m_llm_provider->GetProviderName());
    return true;
}

bool WisdomDaemon::InitializeIPC() {
    Log("Initializing IPC at " + m_socket_path);

    // 删除旧的 socket 文件
    unlink(m_socket_path.c_str());

    // 创建 socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        Log("ERROR: Failed to create socket");
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Log("ERROR: Failed to bind socket");
        close(sock);
        return false;
    }

    // 设置权限
    chmod(m_socket_path.c_str(), 0666);

    if (listen(sock, 5) < 0) {
        Log("ERROR: Failed to listen on socket");
        close(sock);
        return false;
    }

    Log("IPC socket created at " + m_socket_path);
    return true;
}

void WisdomDaemon::TriggerPrediction(const std::wstring& context,
                                      const std::wstring& current_input) {
    if (!m_llm_provider || !m_llm_provider->IsAvailable()) {
        return;
    }

    // 获取上下文历史
    std::wstring history_context;
    if (m_context_history) {
        history_context = m_context_history->GetRecentContext(50);
    }

    // 异步调用 LLM 预测
    std::thread([this, history_context, current_input]() {
        auto candidates = m_llm_provider->PredictCandidates(
            history_context, current_input, 5);

        {
            std::lock_guard<std::mutex> lock(m_candidate_mutex);
            m_current_candidates = std::move(candidates);
        }

        Log("Prediction completed, got " +
            std::to_string(m_current_candidates.size()) + " candidates");
    }).detach();
}

void WisdomDaemon::RimeEventLoop() {
    Log("Rime event loop started");

    RimeApi* rime = rime_get_api();

    while (m_running) {
        // 保持 Rime 线程活跃
        // 实际事件通过 RimeNotificationHandler 回调处理
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    Log("Rime event loop ended");
}

void WisdomDaemon::IPCEventLoop() {
    Log("IPC event loop started");

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        Log("ERROR: Cannot create IPC socket");
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);
    unlink(m_socket_path.c_str());

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Log("ERROR: Cannot bind IPC socket");
        close(server_fd);
        return;
    }

    chmod(m_socket_path.c_str(), 0666);

    if (listen(server_fd, 5) < 0) {
        Log("ERROR: Cannot listen on IPC socket");
        close(server_fd);
        return;
    }

    // 设置 socket 为非阻塞，以便检查 m_running
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    while (m_running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            Log("ERROR: Accept failed");
            break;
        }

        // 读取请求
        char buffer[4096] = {0};
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            std::string request(buffer, n);

            // 处理请求
            std::string response;

            if (request == "GET_CANDIDATES") {
                std::lock_guard<std::mutex> lock(m_candidate_mutex);
                for (const auto& c : m_current_candidates) {
                    std::string c_utf8 = wstring_to_utf8(c);
                    response += c_utf8 + "\n";
                }
            } else if (request.find("TRIGGER_PREDICTION") == 0) {
                // 格式: TRIGGER_PREDICTION:<context>:<input>
                size_t ctx_start = request.find(':');
                if (ctx_start != std::string::npos) {
                    size_t input_start = request.find(':', ctx_start + 1);
                    if (input_start != std::string::npos) {
                        std::string ctx = request.substr(ctx_start + 1, input_start - ctx_start - 1);
                        std::string inp = request.substr(input_start + 1);
                        TriggerPrediction(utf8_to_wstring(ctx), utf8_to_wstring(inp));
                        response = "OK";
                    }
                }
            } else if (request == "CLEAR_CONTEXT") {
                if (m_context_history) {
                    m_context_history->Clear();
                }
                response = "OK";
            } else {
                response = "UNKNOWN_COMMAND";
            }

            write(client_fd, response.c_str(), response.length());
        }

        close(client_fd);
    }

    close(server_fd);
    unlink(m_socket_path.c_str());
    Log("IPC event loop ended");
}

int WisdomDaemon::Run(int argc, char* argv[]) {
    // 设置信号处理
    g_daemon = this;
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 初始化
    if (!InitializeLogging()) return 1;
    if (!InitializeRime()) return 1;
    if (!InitializeLLM()) return 1;
    if (!InitializeIPC()) return 1;

    m_running = true;

    // 启动线程
    m_rime_thread = std::thread(&WisdomDaemon::RimeEventLoop, this);
    m_ipc_thread = std::thread(&WisdomDaemon::IPCEventLoop, this);

    Log("Wisdom-Weasel daemon is running");
    Log("IPC socket: " + m_socket_path);
    Log("Press Ctrl+C to stop");

    // 等待线程结束
    if (m_rime_thread.joinable()) m_rime_thread.join();
    if (m_ipc_thread.joinable()) m_ipc_thread.join();

    return 0;
}

void WisdomDaemon::Stop() {
    m_running = false;
    Log("Stopping Wisdom-Weasel daemon...");
}

} // namespace weasel

// 主函数
int main(int argc, char* argv[]) {
    weasel::WisdomDaemon daemon;
    return daemon.Run(argc, argv);
}
