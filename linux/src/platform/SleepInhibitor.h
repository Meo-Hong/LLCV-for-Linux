#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace llcv::platform {

class SleepInhibitor {
public:
    SleepInhibitor() = default;
    ~SleepInhibitor();
    SleepInhibitor(const SleepInhibitor&) = delete;
    SleepInhibitor& operator=(const SleepInhibitor&) = delete;

    void Acquire(bool keepDisplayOn, std::string reason);
    void Release();
    bool Active() const { return active_; }
    static bool BlocksSuspend();

private:
    void HoldSuspendInhibit(std::string reason);

    bool active_ = false;
    bool displayRequested_ = false;
    bool displayHeld_ = false;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool releasing_ = false;
};

}
