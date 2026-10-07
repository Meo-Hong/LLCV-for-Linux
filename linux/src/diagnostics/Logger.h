#pragma once

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace llcv::diagnostics {

class Logger {
public:
    static Logger& Instance();

    void SetFileLogging(bool enabled);
    bool FileLogging() const;
    std::filesystem::path FilePath() const;
    void WriteV(const char* format, va_list arguments);

private:
    Logger() = default;
    ~Logger();

    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::filesystem::path path_;
};

void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));

}
