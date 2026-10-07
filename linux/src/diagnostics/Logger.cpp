#include "diagnostics/Logger.h"

#include "platform/Paths.h"

#include <time.h>

#include <system_error>

namespace llcv::diagnostics {
namespace {

void Timestamp(char* buffer, size_t size) {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    tm local{};
    localtime_r(&now.tv_sec, &local);
    std::snprintf(buffer, size, "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min,
                  local.tm_sec, now.tv_nsec / 1000000L);
}

}

Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    if (file_) std::fclose(file_);
}

void Logger::SetFileLogging(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled) {
        if (file_) std::fclose(file_);
        file_ = nullptr;
        return;
    }
    if (file_) return;
    const auto directory = platform::LogDirectory();
    if (!platform::EnsureDirectory(directory)) return;
    path_ = directory / "llcv.log";
    std::error_code error;
    if (std::filesystem::exists(path_, error)) {
        std::filesystem::rename(path_, directory / "llcv.log.1", error);
    }
    file_ = std::fopen(path_.c_str(), "w");
}

bool Logger::FileLogging() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_ != nullptr;
}

std::filesystem::path Logger::FilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_.empty() ? platform::LogDirectory() / "llcv.log" : path_;
}

void Logger::WriteV(const char* format, va_list arguments) {
    char message[2048];
    std::vsnprintf(message, sizeof(message), format, arguments);
    char stamp[32];
    Timestamp(stamp, sizeof(stamp));
    std::lock_guard<std::mutex> lock(mutex_);
    std::fprintf(stderr, "[%s] %s\n", stamp, message);
    if (file_) {
        std::fprintf(file_, "[%s] %s\n", stamp, message);
        std::fflush(file_);
    }
}

void Log(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    Logger::Instance().WriteV(format, arguments);
    va_end(arguments);
}

}
