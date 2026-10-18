#pragma once

#include "recording/RecordingOptions.h"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace llcv::recording {

std::filesystem::path RecordingDirectory(const Options& options);
std::optional<uint64_t> FreeBytes(const std::filesystem::path& directory);
std::filesystem::path NewRecordingPath(const std::filesystem::path& directory);
std::filesystem::path SegmentPath(const std::filesystem::path& first, int segment);

}
