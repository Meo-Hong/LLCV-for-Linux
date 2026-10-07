#pragma once

#include <algorithm>
#include <cstddef>

namespace llcv::audio {

class QueueDriftController {
public:
    void BeginWithLowReserve() { ppm_ = -1000.0; }

    double Update(double filteredFrames, double targetFrames, size_t frames) {
        const double seconds = (std::min)(static_cast<double>(frames) / 48000.0, 0.05);
        const double error = filteredFrames - (targetFrames + 16.0);
        const double proportional = error * 2.0;
        const double request = proportional + clockPpm_;
        if ((request < 1000.0 || error < 0.0) && (request > -1000.0 || error > 0.0)) {
            clockPpm_ = std::clamp(clockPpm_ + error * seconds * 0.1, -1000.0, 0.0);
        }
        const double desired = std::clamp(proportional + clockPpm_, -1000.0, 1000.0);
        ppm_ += (desired - ppm_) * seconds / (0.5 + seconds);
        return ppm_;
    }

private:
    double clockPpm_ = 0.0;
    double ppm_ = 0.0;
};

}
