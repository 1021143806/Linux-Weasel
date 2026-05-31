#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

namespace weasel {

class MemoryCompressor;

// 用户输入上下文历史记录类
// 维护最近 N 个用户输入的词，用于 LLM 预测候选词
class ContextHistory {
public:
    explicit ContextHistory(size_t max_size = 50);
    ~ContextHistory();

    // 添加用户提交的文本到历史记录
    void AddText(const std::wstring& text);

    // 获取最近 N 个词作为上下文
    std::wstring GetRecentContext(size_t count) const;

    // 获取所有历史记录
    std::vector<std::wstring> GetAllHistory() const;

    // 清空历史记录
    void Clear();

    // 获取当前记录数量
    size_t GetSize() const;

    // 获取最大记录数量
    size_t GetMaxSize() const;

    // 设置记忆压缩器
    void SetMemoryCompressor(MemoryCompressor* compressor);
    void SetCompressionCompletedCallback(std::function<void()> callback);

    // 设置日志回调
    void SetLogCallback(std::function<void(const std::string&)> cb) { m_log_cb = std::move(cb); }

private:
    std::vector<std::wstring> SplitIntoWords(const std::wstring& text) const;
    bool IsSeparator(wchar_t ch) const;
    void TryTriggerCompression();
    size_t GetCompressWordCount() const { return m_max_size / 2; }
    std::vector<std::wstring> GetOldestWords(size_t count) const;
    void ReplaceOldestWithCompressed(const std::vector<std::wstring>& compressed);

    void Log(const std::string& msg);

    mutable std::mutex m_mutex;
    std::vector<std::wstring> m_history;
    size_t m_max_size;
    MemoryCompressor* m_memory_compressor;
    bool m_compressing;
    std::function<void()> m_compression_completed_callback;
    std::function<void(const std::string&)> m_log_cb;
};

} // namespace weasel
