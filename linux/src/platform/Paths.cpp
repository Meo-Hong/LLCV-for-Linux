#include "platform/Paths.h"

#include <pwd.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace llcv::platform {
namespace {

std::filesystem::path EnvironmentPath(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return {};
    std::filesystem::path path(value);
    return path.is_absolute() ? path : std::filesystem::path{};
}

std::filesystem::path XdgBase(const char* variable, const std::filesystem::path& fallback) {
    auto base = EnvironmentPath(variable);
    return base.empty() ? HomeDirectory() / fallback : base;
}

std::string Unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool Exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::filesystem::path FirstExisting(const std::vector<std::filesystem::path>& candidates) {
    for (const auto& candidate : candidates) {
        if (Exists(candidate)) return candidate;
    }
    return {};
}

}

std::filesystem::path HomeDirectory() {
    if (auto home = EnvironmentPath("HOME"); !home.empty()) return home;
    if (const passwd* entry = getpwuid(getuid()); entry && entry->pw_dir) {
        return entry->pw_dir;
    }
    return "/tmp";
}

std::filesystem::path ConfigDirectory() {
    return XdgBase("XDG_CONFIG_HOME", ".config") / "llcv";
}

std::filesystem::path StateDirectory() {
    return XdgBase("XDG_STATE_HOME", std::filesystem::path(".local") / "state") / "llcv";
}

std::filesystem::path LogDirectory() {
    return StateDirectory() / "logs";
}

std::filesystem::path PicturesDirectory() {
    std::ifstream file(XdgBase("XDG_CONFIG_HOME", ".config") / "user-dirs.dirs");
    std::string line;
    constexpr std::string_view key = "XDG_PICTURES_DIR=";
    constexpr std::string_view homeToken = "$HOME";
    while (std::getline(file, line)) {
        if (line.rfind(key, 0) != 0) continue;
        std::string value = Unquote(line.substr(key.size()));
        if (value.rfind(homeToken, 0) == 0) {
            value = HomeDirectory().string() + value.substr(homeToken.size());
        }
        if (!value.empty() && value.front() == '/') return value;
    }
    return HomeDirectory() / "Pictures";
}

std::filesystem::path ScreenshotDirectory() {
    return PicturesDirectory() / "LLCV";
}

std::filesystem::path ExecutableDirectory() {
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path();
}

std::filesystem::path SettingsFilePath() {
    return ConfigDirectory() / "settings.ini";
}

std::filesystem::path FindFont(const std::string& fileName) {
    std::vector<std::filesystem::path> candidates;
    const auto executable = ExecutableDirectory();
    if (!executable.empty()) {
        candidates.push_back(executable.parent_path() / "share" / "llcv" / "fonts" / fileName);
    }
    candidates.push_back(std::filesystem::path(LLCV_DATA_DIR) / "fonts" / fileName);
#ifdef LLCV_SOURCE_DIR
    candidates.push_back(std::filesystem::path(LLCV_SOURCE_DIR) / "third_party" / "pretendard" / fileName);
#endif
    return FirstExisting(candidates);
}

std::filesystem::path FindAppIcon() {
    const std::string fileName = std::string(LLCV_APP_ID) + ".png";
    std::vector<std::filesystem::path> candidates;
    const auto executable = ExecutableDirectory();
    if (!executable.empty()) {
        candidates.push_back(executable.parent_path() / "share" / "icons" / "hicolor" /
                             "256x256" / "apps" / fileName);
    }
    candidates.push_back(std::filesystem::path(LLCV_ICON_DIR) / "256x256" / "apps" / fileName);
#ifdef LLCV_SOURCE_DIR
    candidates.push_back(std::filesystem::path(LLCV_SOURCE_DIR) / "data" / "icons" / "256x256" / fileName);
#endif
    return FirstExisting(candidates);
}

bool EnsureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return std::filesystem::is_directory(path, error);
}

std::string FileUrl(const std::filesystem::path& path) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string url = "file://";
    for (const unsigned char c : path.string()) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == '~' || c == '/';
        if (unreserved) {
            url.push_back(static_cast<char>(c));
        } else {
            url.push_back('%');
            url.push_back(kHex[c >> 4]);
            url.push_back(kHex[c & 0x0F]);
        }
    }
    return url;
}

}
