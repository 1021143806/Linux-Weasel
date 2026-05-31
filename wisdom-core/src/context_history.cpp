#include "wisdom-core/context_history.h"
#include "wisdom-core/memory_compressor.h"
#include <algorithm>
#include <sstream>
#include <thread>

namespace weasel {

ContextHistory::ContextHistory(size_t max_size)
    : m_max_size(max_size > 0 ? max_size : 50),
      m_memory_compressor(nullptr),
      m_compressing(false) {
    m_history.reserve(m_max_size);
}

ContextHistory::~ContextHistory() {
    Clear();
}

void ContextHistory::Log(const std::string& msg) {
    if (m_log_cb) m_log_cb(msg);
}

void ContextHistory::AddText(const std::wstring& text) {
    if (text.empty()) return;

    std::vector<std::wstring> words = SplitIntoWords(text);
    size_t words_added = 0;
    size_t current_size = 0;
    bool should_trigger_compression = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& word : words) {
            if (word.empty()) continue;
            m_history.push_back(word);
            words_added++;
        }

        if (!m_memory_compressor || !m_memory_compressor->IsAvailable()) {
            size_t batch = GetCompressWordCount();
            if (batch == 0) batch = 1;
            while (m_history.size() > m_max_size) {
                size_t erase_count = (std::min)(batch, m_history.size());
                m_history.erase(m_history.begin(), m_history.begin() + erase_count);
            }
        } else {
            while (m_history.size() > m_max_size) {
                m_history.erase(m_history.begin());
            }
        }

        current_size = m_history.size();
        if (current_size >= m_max_size && m_memory_compressor &&
            m_memory_compressor->IsAvailable() && !m_compressing &&
            m_history.size() >= GetCompressWordCount()) {
            should_trigger_compression = true;
        }
    }

    Log("[Context] Added text, current size: " + std::to_string(current_size));

    if (should_trigger_compression) {
        TryTriggerCompression();
    }
}

std::wstring ContextHistory::GetRecentContext(size_t count) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_history.empty()) return L"";
    size_t actual_count = (std::min)(count, m_history.size());
    size_t start = m_history.size() - actual_count;
    std::wstringstream ss;
    for (size_t i = start; i < m_history.size(); ++i) {
        if (i > start) ss << L" ";
        ss << m_history[i];
    }
    return ss.str();
}

std::vector<std::wstring> ContextHistory::GetAllHistory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

void ContextHistory::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
    m_compressing = false;
    Log("[Context] History cleared");
}

size_t ContextHistory::GetSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history.size();
}

size_t ContextHistory::GetMaxSize() const {
    return m_max_size;
}

void ContextHistory::SetMemoryCompressor(MemoryCompressor* compressor) {
    m_memory_compressor = compressor;
}

void ContextHistory::SetCompressionCompletedCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_compression_completed_callback = std::move(callback);
}

void ContextHistory::TryTriggerCompression() {
    size_t compress_count = GetCompressWordCount();
    std::vector<std::wstring> to_compress;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_compressing || m_history.size() < compress_count ||
            !m_memory_compressor || !m_memory_compressor->IsAvailable()) {
            return;
        }
        to_compress = GetOldestWords(compress_count);
        m_compressing = true;
    }

    Log("[Memory] Triggering async compression of " + std::to_string(compress_count) + " words");

    m_memory_compressor->CompressAsync(to_compress, [this](std::vector<std::wstring> compressed) {
        if (compressed.empty()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_compressing = false;
            Log("[Memory] Compression failed, keeping original words");
            return;
        }
        ReplaceOldestWithCompressed(compressed);
    });
}

std::vector<std::wstring> ContextHistory::GetOldestWords(size_t count) const {
    std::vector<std::wstring> result;
    size_t n = (std::min)(count, m_history.size());
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.push_back(m_history[i]);
    }
    return result;
}

void ContextHistory::ReplaceOldestWithCompressed(
    const std::vector<std::wstring>& compressed) {
    std::function<void()> on_compression_completed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t compress_count = GetCompressWordCount();
        if (m_history.size() < compress_count) return;

        m_history.erase(m_history.begin(), m_history.begin() + compress_count);
        m_history.insert(m_history.begin(), compressed.begin(), compressed.end());

        while (m_history.size() > m_max_size) {
            m_history.erase(m_history.begin());
        }

        m_compressing = false;
        Log("[Memory] Compression completed, current history: " + std::to_string(m_history.size()));

        on_compression_completed = m_compression_completed_callback;
    }

    if (on_compression_completed) {
        std::thread([on_compression_completed]() {
            on_compression_completed();
        }).detach();
    }
}

std::vector<std::wstring> ContextHistory::SplitIntoWords(const std::wstring& text) const {
    std::vector<std::wstring> words;
    if (text.empty()) return words;

    std::wstring current_word;
    for (wchar_t ch : text) {
        if (IsSeparator(ch)) {
            if (!current_word.empty()) {
                words.push_back(current_word);
                current_word.clear();
            }
        } else {
            current_word += ch;
        }
    }

    if (!current_word.empty()) {
        words.push_back(current_word);
    }

    return words;
}

bool ContextHistory::IsSeparator(wchar_t ch) const {
    if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r')
        return true;
    if (ch == L'，' || ch == L'。' || ch == L'、' || ch == L'；' ||
        ch == L'：' || ch == L'？' || ch == L'！' || ch == L'…' ||
        ch == L'—' || ch == L'–' || ch == L'（' || ch == L'）' ||
        ch == L'【' || ch == L'】' || ch == L'《' || ch == L'》')
        return true;
    if (ch == L',' || ch == L'.' || ch == L';' || ch == L':' ||
        ch == L'?' || ch == L'!' || ch == L'-' || ch == L'_' ||
        ch == L'(' || ch == L')' || ch == L'[' || ch == L']' ||
        ch == L'{' || ch == L'}' || ch == L'"' || ch == L'\'' ||
        ch == L'/' || ch == L'\\')
        return true;
    return false;
}

} // namespace weasel
