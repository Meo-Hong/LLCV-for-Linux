#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace llcv::audio {

// One audio producer (WASAPI owner or ASIO callback), one UI consumer.
// Backend handover happens only after ASIO Stop has joined its callbacks.
// Never allocate, lock, or perform I/O on the producer. If the UI is busy,
// discard new diagnostics rather than delaying audio, and report that loss.
class RealtimeLogQueue {
public:
    static constexpr size_t kCapacity = 16;
    static constexpr size_t kMessageCharacters = 2048;
    using Message = std::array<wchar_t, kMessageCharacters>;

    bool Push(const wchar_t* message) noexcept {
        if (!message) return true;
        const size_t write = write_.load(std::memory_order_relaxed);
        const size_t next = (write + 1) % messages_.size();
        if (next == read_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        auto& destination = messages_[write];
        size_t count = 0;
        while (count + 1 < destination.size() && message[count]) {
            destination[count] = message[count];
            ++count;
        }
        destination[count] = L'\0';
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool Pop(Message& message) noexcept {
        const size_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        message = messages_[read];
        read_.store((read + 1) % messages_.size(), std::memory_order_release);
        return true;
    }

    uint64_t TakeDropped() noexcept {
        return dropped_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::array<Message, kCapacity + 1> messages_{};
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
    std::atomic<uint64_t> dropped_{0};
};

} // namespace llcv::audio
