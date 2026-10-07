#pragma once

#include "audio/AudioMix.h"
#include "audio/PcmPipeline.h"
#include "audio/QueueDriftController.h"

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llcv::audio {

struct AudioDeviceEntry {
    SDL_AudioDeviceID id = 0;
    std::string name;
};

std::vector<AudioDeviceEntry> RecordingDevices();
std::vector<AudioDeviceEntry> PlaybackDevices();
std::string MatchCaptureAudioDevice(const std::vector<AudioDeviceEntry>& devices,
                                    const std::string& usbProduct, const std::string& card);
bool HasDevice(const std::vector<AudioDeviceEntry>& devices, const std::string& name);

struct AudioConfig {
    std::string captureDevice;
    std::string outputDevice;
    int deviceFrames = 256;
    int targetMs = 25;
    bool driftCorrection = true;
};

struct AudioStatus {
    bool running = false;
    bool prefilling = false;
    bool driftCorrection = false;
    std::string captureName;
    std::string outputName;
    std::string driver;
    int outputDeviceFrames = 0;
    int captureDeviceFrames = 0;
    uint32_t ringFrames = 0;
    uint32_t resamplerFrames = 0;
    uint32_t minimumQueueFrames = 0;
    uint32_t targetFrames = 0;
    uint32_t lastCapturePacketFrames = 0;
    int appliedPpm = 0;
    uint64_t underrunEvents = 0;
    uint64_t underrunFrames = 0;
    uint64_t overrunEvents = 0;
    uint64_t trimEvents = 0;
    uint64_t captureCallbacks = 0;
};

class AudioEngine {
public:
    static constexpr int kSampleRate = 48000;

    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool Start(const AudioConfig& config, std::string& error);
    void Stop();
    bool Running() const { return running_.load(std::memory_order_acquire); }
    bool OwnsDevice(SDL_AudioDeviceID id) const;

    void SetVolume(int master, int left, int right);
    void SetBackgroundMuted(bool muted);
    void ResetMinimum();

    AudioStatus Status() const;
    int PeakLeft() const { return peakLeft_.load(std::memory_order_relaxed); }
    int PeakRight() const { return peakRight_.load(std::memory_order_relaxed); }
    uint64_t ClipEvents() const { return clipEvents_.load(std::memory_order_relaxed); }
    uint64_t LastClipMs() const { return lastClipMs_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t kScratchFrames = 4096;

    static void SDLCALL CaptureCallback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    static void SDLCALL PlaybackCallback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void OnCapture(SDL_AudioStream* stream);
    void OnPlayback(SDL_AudioStream* stream, int additionalBytes);
    size_t Render(int16_t* output, size_t frames);
    StereoGain TargetGain() const;

    AudioConfig config_;
    SDL_AudioStream* captureStream_ = nullptr;
    SDL_AudioStream* playbackStream_ = nullptr;
    std::unique_ptr<PcmRing> ring_;
    std::unique_ptr<SincDriftResampler> resampler_;
    QueueDriftController controller_;
    std::vector<int16_t> captureScratch_;
    std::vector<int16_t> playbackScratch_;
    StereoGain currentGain_;
    double filteredFrames_ = 0.0;
    bool prefilling_ = true;
    size_t targetFrames_ = 1200;
    size_t hardLimitFrames_ = 4800;
    std::string captureName_;
    std::string outputName_;
    int outputDeviceFrames_ = 0;
    int captureDeviceFrames_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<int> volume_{100};
    std::atomic<int> leftVolume_{100};
    std::atomic<int> rightVolume_{100};
    std::atomic<bool> backgroundMuted_{false};
    std::atomic<int> peakLeft_{0};
    std::atomic<int> peakRight_{0};
    std::atomic<uint64_t> clipEvents_{0};
    std::atomic<uint64_t> lastClipMs_{0};
    std::atomic<bool> prefillingPublished_{true};
    std::atomic<uint32_t> ringFrames_{0};
    std::atomic<uint32_t> resamplerFrames_{0};
    std::atomic<uint32_t> minimumQueueFrames_{UINT32_MAX};
    std::atomic<bool> resetMinimum_{false};
    std::atomic<int> appliedPpm_{0};
    std::atomic<uint64_t> underrunEvents_{0};
    std::atomic<uint64_t> underrunFrames_{0};
    std::atomic<uint64_t> trimEvents_{0};
    std::atomic<uint64_t> captureCallbacks_{0};
    std::atomic<uint32_t> lastCapturePacketFrames_{0};
};

}
