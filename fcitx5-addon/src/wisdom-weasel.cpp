// Wisdom-Weasel fcitx5 插件
// 通过 Unix Domain Socket 与守护进程通信，获取 LLM 候选词

#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>

#include <string>
#include <vector>
#include <mutex>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace fcitx;

namespace {

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
    }

    ~WisdomWeasel() override = default;

    std::vector<std::string> GetLLMCandidates() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_candidates;
    }

    void ClearCandidates() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_candidates.clear();
    }

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

    void TriggerPrediction(const std::string& context, const std::string& input) {
        std::string request = "TRIGGER_PREDICTION:" + context + ":" + input;
        SendRequest(m_socket_path, request);
    }

    void ClearContext() {
        SendRequest(m_socket_path, "CLEAR_CONTEXT");
    }

private:
    Instance* m_instance;
    std::string m_socket_path;
    std::vector<std::string> m_candidates;
    std::mutex m_mutex;
};

// 注册插件（fcitx5 5.1.17 版本只接受一个参数）
class WisdomWeaselFactory : public AddonFactory {
public:
    AddonInstance* create(AddonManager* manager) override {
        auto* instance = manager->instance();
        return new WisdomWeasel(instance);
    }
};

FCITX_ADDON_FACTORY(WisdomWeaselFactory);
