#include "platform/Strings.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>

namespace llcv::platform {

std::string Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string result;
    if (length > 0) {
        result.resize(static_cast<size_t>(length) + 1);
        std::vsnprintf(result.data(), result.size(), format, arguments);
        result.resize(static_cast<size_t>(length));
    }
    va_end(arguments);
    return result;
}

std::string Trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

std::string ToLower(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return false;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

std::vector<std::string> Words(std::string_view text) {
    std::vector<std::string> words;
    std::string current;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!current.empty()) {
            words.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) words.push_back(std::move(current));
    return words;
}

}
