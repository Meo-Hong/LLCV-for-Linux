#include "recording/RecordingStorage.h"

#include "platform/Paths.h"

#include <sys/statvfs.h>
#include <time.h>

#include <cstdio>
#include <string>
#include <system_error>

namespace llcv::recording {

std::filesystem::path RecordingDirectory(const Options& options) {
    if (options.folder.empty()) return platform::DefaultRecordingDirectory();
    return std::filesystem::path(options.folder);
}

std::optional<uint64_t> FreeBytes(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::path probe = directory;
    while (!probe.empty() && !std::filesystem::exists(probe, error)) {
        const auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    if (probe.empty()) return std::nullopt;
    struct statvfs info {};
    if (statvfs(probe.c_str(), &info) != 0) return std::nullopt;
    return static_cast<uint64_t>(info.f_bavail) * static_cast<uint64_t>(info.f_frsize);
}

std::filesystem::path NewRecordingPath(const std::filesystem::path& directory) {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    tm local{};
    localtime_r(&now.tv_sec, &local);
    char name[64];
    std::snprintf(name, sizeof(name), "LLCV_%04d%02d%02d_%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    std::filesystem::path path = directory / (std::string(name) + ".mp4");
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(path, error) ||
                         std::filesystem::exists(SegmentPath(path, 2), error);
         ++suffix) {
        path = directory / (std::string(name) + "_" + std::to_string(suffix) + ".mp4");
    }
    return path;
}

std::filesystem::path SegmentPath(const std::filesystem::path& first, int segment) {
    if (segment <= 1) return first;
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_part%02d", segment);
    return first.parent_path() / (first.stem().string() + suffix + first.extension().string());
}

}
