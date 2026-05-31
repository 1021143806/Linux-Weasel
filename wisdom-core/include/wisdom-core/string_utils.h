#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <cstring>

namespace weasel {

// UTF-8 与 wstring 互转（Linux 兼容实现，使用 mbstowcs/wcstombs）
inline std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    size_t len = wcstombs(nullptr, wstr.c_str(), 0);
    if (len == static_cast<size_t>(-1)) return {};
    std::string result(len, '\0');
    wcstombs(&result[0], wstr.c_str(), len);
    return result;
}

inline std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return {};
    size_t len = mbstowcs(nullptr, str.c_str(), 0);
    if (len == static_cast<size_t>(-1)) return {};
    std::wstring result(len, L'\0');
    mbstowcs(&result[0], str.c_str(), len);
    return result;
}

// 转义 JSON 字符串中的特殊字符
inline std::string escape_json_string(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    return result;
}

// 按空格分割字符串
inline std::vector<std::string> split_by_space(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        if (!word.empty()) result.push_back(word);
    }
    return result;
}

inline std::vector<std::wstring> split_by_space_w(const std::wstring& text) {
    std::vector<std::wstring> result;
    std::wistringstream iss(text);
    std::wstring word;
    while (iss >> word) {
        if (!word.empty()) result.push_back(word);
    }
    return result;
}

} // namespace weasel
