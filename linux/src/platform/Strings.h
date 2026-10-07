#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace llcv::platform {

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Trim(std::string_view text);
std::string ToLower(std::string_view text);
bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle);
std::vector<std::string> Words(std::string_view text);

}
