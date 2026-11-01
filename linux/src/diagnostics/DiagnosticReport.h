#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace llcv::diagnostics {

struct ReportInput {
    std::filesystem::path directory;
    std::vector<std::filesystem::path> logs;
    std::filesystem::path settingsFile;
    std::string summary;
    std::vector<std::pair<std::string, std::string>> masks;
};

struct ReportResult {
    bool ok = false;
    std::filesystem::path path;
    std::string error;
    int logs = 0;
};

std::string MaskText(std::string text, const std::vector<std::pair<std::string, std::string>>& masks);
ReportResult CreateReport(const ReportInput& input);

}
