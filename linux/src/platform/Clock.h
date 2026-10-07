#pragma once

#include <time.h>

#include <cstdint>

namespace llcv::platform {

inline uint64_t MonotonicNs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ull + static_cast<uint64_t>(now.tv_nsec);
}

inline uint64_t MonotonicMs() {
    return MonotonicNs() / 1000000ull;
}

}
