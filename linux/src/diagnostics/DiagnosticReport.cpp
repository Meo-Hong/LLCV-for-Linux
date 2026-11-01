#include "diagnostics/DiagnosticReport.h"

#include "platform/Paths.h"
#include "platform/Strings.h"

#include <time.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <system_error>

namespace llcv::diagnostics {
namespace {

constexpr std::streamoff kMaximumLogBytes = 4 * 1024 * 1024;

constexpr const char* kReadMe =
    "LLCV for Linux diagnostic report\n"
    "\n"
    "Contents: report.txt (versions, system, devices, current selections and errors),\n"
    "settings.ini (saved settings) and the most recent LLCV logs. Recordings, screenshots\n"
    "and files of other apps are not included. Nothing was uploaded.\n"
    "\n"
    "The home folder, the recording folder and the USB serial numbers of detected\n"
    "devices are masked. Other personal text can remain, so read the files before\n"
    "sharing them. The original logs and settings are not changed.\n"
    "\n"
    "Report problems at https://github.com/Meo-Hong/LLCV-for-Linux/issues with the steps\n"
    "to reproduce, the time, the HDMI source's resolution, frame rate and HDR state, and\n"
    "any other app that was using the capture device.\n";

const std::array<uint32_t, 256>& CrcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            values[i] = c;
        }
        return values;
    }();
    return table;
}

uint32_t Crc32(const std::string& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) crc = CrcTable()[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void Put16(std::string& out, uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFu));
    out.push_back(static_cast<char>((value >> 8) & 0xFFu));
}

void Put32(std::string& out, uint32_t value) {
    Put16(out, value & 0xFFFFu);
    Put16(out, value >> 16);
}

class ZipWriter {
public:
    ZipWriter() {
        const time_t now = time(nullptr);
        tm local{};
        localtime_r(&now, &local);
        dosTime_ = static_cast<uint32_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
        dosDate_ = static_cast<uint32_t>(((std::max(local.tm_year, 80) - 80) << 9) | ((local.tm_mon + 1) << 5) |
                                         local.tm_mday);
    }

    void Add(const std::string& name, const std::string& data) {
        const uint32_t crc = Crc32(data);
        const uint32_t offset = static_cast<uint32_t>(body_.size());
        Header(body_, 0x04034b50u, name, data, crc, 0);
        body_ += name;
        body_ += data;
        Header(directory_, 0x02014b50u, name, data, crc, offset);
        directory_ += name;
        ++count_;
    }

    std::string Finish() const {
        std::string out = body_ + directory_;
        Put32(out, 0x06054b50u);
        Put16(out, 0);
        Put16(out, 0);
        Put16(out, count_);
        Put16(out, count_);
        Put32(out, static_cast<uint32_t>(directory_.size()));
        Put32(out, static_cast<uint32_t>(body_.size()));
        Put16(out, 0);
        return out;
    }

private:
    void Header(std::string& out, uint32_t signature, const std::string& name, const std::string& data,
                uint32_t crc, uint32_t offset) const {
        const bool central = signature == 0x02014b50u;
        Put32(out, signature);
        if (central) Put16(out, 20);
        Put16(out, 20);
        Put16(out, 0x0800);
        Put16(out, 0);
        Put16(out, dosTime_);
        Put16(out, dosDate_);
        Put32(out, crc);
        Put32(out, static_cast<uint32_t>(data.size()));
        Put32(out, static_cast<uint32_t>(data.size()));
        Put16(out, static_cast<uint32_t>(name.size()));
        Put16(out, 0);
        if (!central) return;
        Put16(out, 0);
        Put16(out, 0);
        Put16(out, 0);
        Put32(out, 0);
        Put32(out, offset);
    }

    std::string body_;
    std::string directory_;
    uint32_t count_ = 0;
    uint32_t dosTime_ = 0;
    uint32_t dosDate_ = 0;
};

bool ReadTail(const std::filesystem::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - kMaximumLogBytes);
    file.seekg(start);
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (start > 0) text = "[earlier part of this log omitted]\n" + text;
    return true;
}

std::string ReportName() {
    const time_t now = time(nullptr);
    tm local{};
    localtime_r(&now, &local);
    return platform::Format("LLCV-report-%04d%02d%02d-%02d%02d%02d.zip", local.tm_year + 1900, local.tm_mon + 1,
                            local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

}

std::string MaskText(std::string text, const std::vector<std::pair<std::string, std::string>>& masks) {
    for (const auto& [from, to] : masks) {
        if (from.empty()) continue;
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
    }
    return text;
}

ReportResult CreateReport(const ReportInput& input) {
    ReportResult result;
    if (!platform::EnsureDirectory(input.directory)) {
        result.error = "cannot create " + input.directory.string();
        return result;
    }
    ZipWriter zip;
    zip.Add("READ-ME.txt", kReadMe);
    zip.Add("report.txt", MaskText(input.summary, input.masks));
    std::string text;
    if (ReadTail(input.settingsFile, text)) zip.Add("settings.ini", MaskText(text, input.masks));
    for (const auto& log : input.logs) {
        if (!ReadTail(log, text)) continue;
        ++result.logs;
        zip.Add("logs/" + log.filename().string() + ".txt", MaskText(text, input.masks));
    }

    result.path = input.directory / ReportName();
    const std::filesystem::path partial = result.path.string() + ".part";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        const std::string data = zip.Finish();
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            result.error = "cannot write " + partial.string();
            return result;
        }
    }
    std::error_code error;
    std::filesystem::rename(partial, result.path, error);
    if (error) {
        result.error = "cannot write " + result.path.string() + ": " + error.message();
        return result;
    }
    result.ok = true;
    return result;
}

}
