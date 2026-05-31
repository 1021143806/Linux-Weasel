// Wisdom-Weasel fcitx5 插件
// 通过 Unix Domain Socket 与守护进程通信，获取 LLM 候选词并注入到候选窗

#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/instance.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/event.h>

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace fcitx;

namespace {

// 通过 Unix Domain Socket 向守护进程发送请求
std::string SendRequest(const std::string& socket_path, const std::string& request) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return "";

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "";
    }

    write(sock, request.c_str(), request.length());

    char buffer[4096] = {0};
    ssize_t n = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);

    if (n > 0) {
        return std::string(buffer, n);
    }
    return "";
}

} // anonymous namespace

class WisdomWeasel final : public AddonInstance {
public:
    WisdomWeasel(Instance* instance)
        : m_instance(instance),
          m_socket_path("/tmp/wisdom-weasel.sock") {

        // 监听输入上下文事件
        instance->inputContextManager().registerWatcher(
            [this](EventType type, InputContext& ic) {
                if (type == EventType::InputContextFocusIn) {
                    // 焦点进入时，可以触发上下文更新
                } else if (type == EventType::InputContextFocusOut) {
                    // 焦点离开时，清空候选词
                    ClearCandidates();
                }
            });

        FCITX_INFO() << "Wisdom-Weasel plugin loaded";
    }

    ~WisdomWeasel() override = default;

    // 获取 LLM 候选词列表
    std::vector<std::string> GetLLMCandidates() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_candidates;
    }

    // 清空 LLM 候选词
    void ClearCandidates() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_candidates.clear();
    }

    // 从守护进程获取候选词
    void FetchCandidates() {
        std::string response = SendRequest(m_socket_path, "GET_CANDIDATES");
        if (response.empty()) {
            ClearCandidates();
            return;
        }

        std::vector<std::string> candidates;
        size_t pos = 0;
        while (pos < response.length()) {
            size_t newline = response.find('\n', pos);
            if (newline == std::string::npos) break;
            std::string candidate = response.substr(pos, newline - pos);
            if (!candidate.empty()) {
                candidates.push_back(candidate);
            }
            pos = newline + 1;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_candidates = std::move(candidates);
        }
    }

    // 触发 LLM 预测
    void TriggerPrediction(const std::string& context, const std::string& input) {
        std::string request = "TRIGGER_PREDICTION:" + context + ":" + input;
        SendRequest(m_socket_path, request);
    }

    // 清空上下文
    void ClearContext() {
        SendRequest(m_socket_path, "CLEAR_CONTEXT");
    }

private:
    Instance* m_instance;
    std::string m_socket_path;
    std::vector<std::string> m_candidates;
    std::mutex m_mutex;
};

// 注册插件
FCITX_ADDON_FACTORY(WisdomWeasel, addon::AddonPriority::Default);
