#include "screenshot/Screenshot.h"

#include "platform/Paths.h"
#include "platform/PngImage.h"

#include <SDL3/SDL_thread.h>

#include <time.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace llcv::screenshot {
namespace {

std::filesystem::path UniquePath(const std::filesystem::path& directory) {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    tm local{};
    localtime_r(&now.tv_sec, &local);
    char name[64];
    std::snprintf(name, sizeof(name), "LLCV_%04d%02d%02d_%02d%02d%02d_%03ld", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
                  now.tv_nsec / 1000000L);
    std::filesystem::path path = directory / (std::string(name) + ".png");
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(path, error); ++suffix) {
        path = directory / (std::string(name) + "_" + std::to_string(suffix) + ".png");
    }
    return path;
}

Result Save(std::vector<uint8_t> rgba, int width, int height, std::filesystem::path directory,
            bool keepPng) {
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
    Result result;
    std::vector<uint8_t> png;
    if (!platform::EncodePngRgb(rgba, width, height, png, result.error)) return result;
    if (!platform::EnsureDirectory(directory)) {
        result.error = "cannot create " + directory.string();
        return result;
    }
    result.path = UniquePath(directory);
    std::ofstream file(result.path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    file.close();
    if (!file) {
        result.error = "cannot write " + result.path.string();
        return result;
    }
    result.ok = true;
    if (keepPng) result.png = std::move(png);
    return result;
}

}

Service::~Service() {
    for (auto& job : pending_) job.wait();
}

void Service::Request(std::vector<uint8_t> rgba, int width, int height, std::filesystem::path directory,
                      bool keepPng) {
    pending_.push_back(std::async(std::launch::async, Save, std::move(rgba), width, height,
                                  std::move(directory), keepPng));
}

std::optional<Result> Service::Poll() {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            Result result = it->get();
            pending_.erase(it);
            return result;
        }
    }
    return std::nullopt;
}

}
