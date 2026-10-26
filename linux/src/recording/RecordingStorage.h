#pragma once

#include "recording/RecordingOptions.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace llcv::recording {

std::filesystem::path RecordingDirectory(const Options& options);
std::optional<uint64_t> FreeBytes(const std::filesystem::path& directory);
std::filesystem::path NewRecordingPath(const std::filesystem::path& directory);
std::filesystem::path SegmentPath(const std::filesystem::path& first, int segment);

class FreeSpaceProbe {
public:
    bool Poll(const std::filesystem::path& directory, std::optional<uint64_t>& freeBytes);

private:
    struct State {
        std::mutex mutex;
        bool pending = false;
        bool completed = false;
        std::optional<uint64_t> freeBytes;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
};

}
