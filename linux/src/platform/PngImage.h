#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace llcv::platform {

bool EncodePngRgb(const std::vector<uint8_t>& rgba, int width, int height,
                  std::vector<uint8_t>& png, std::string& error);
bool LoadPngRgba(const std::filesystem::path& path, std::vector<uint8_t>& pixels,
                 int& width, int& height);

}
