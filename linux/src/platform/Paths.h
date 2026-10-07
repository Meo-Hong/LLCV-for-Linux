#pragma once

#include <filesystem>
#include <string>

namespace llcv::platform {

std::filesystem::path HomeDirectory();
std::filesystem::path ConfigDirectory();
std::filesystem::path StateDirectory();
std::filesystem::path LogDirectory();
std::filesystem::path PicturesDirectory();
std::filesystem::path ScreenshotDirectory();
std::filesystem::path ExecutableDirectory();
std::filesystem::path SettingsFilePath();
std::filesystem::path FindFont(const std::string& fileName);
std::filesystem::path FindAppIcon();
bool EnsureDirectory(const std::filesystem::path& path);
std::string FileUrl(const std::filesystem::path& path);

}
