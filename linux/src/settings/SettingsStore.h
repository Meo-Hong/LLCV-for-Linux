#pragma once

#include "settings/AppSettings.h"

#include <filesystem>

namespace llcv::settings {

AppSettings LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const AppSettings& settings);

}
