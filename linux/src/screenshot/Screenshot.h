#pragma once

#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace llcv::screenshot {

struct Result {
    bool ok = false;
    std::filesystem::path path;
    std::string error;
    std::vector<uint8_t> png;
};

class Service {
public:
    ~Service();
    void Request(std::vector<uint8_t> rgba, int width, int height, std::filesystem::path directory,
                 bool keepPng);
    std::optional<Result> Poll();
    bool Busy() const { return !pending_.empty(); }

private:
    std::vector<std::future<Result>> pending_;
};

}
