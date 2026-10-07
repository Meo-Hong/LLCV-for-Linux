#include "audio/AudioEngine.h"

#include "diagnostics/Logger.h"
#include "platform/Clock.h"
#include "platform/Strings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace llcv::audio {
namespace {

std::vector<AudioDeviceEntry> ListDevices(SDL_AudioDeviceID* ids, int count) {
    std::vector<AudioDeviceEntry> devices;
    if (!ids) return devices;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        if (name && *name) devices.push_back({ids[i], name});
    }
    SDL_free(ids);
    return devices;
}

SDL_AudioDeviceID FindDevice(const std::vector<AudioDeviceEntry>& devices, const std::string& name) {
    for (const auto& device : devices) {
        if (device.name == name) return device.id;
    }
    return 0;
}

int DeviceFrames(SDL_AudioStream* stream) {
    SDL_AudioSpec spec{};
    int frames = 0;
    if (stream && SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &spec, &frames)) return frames;
    return 0;
}

std::string StreamDeviceName(SDL_AudioStream* stream, const std::string& fallback) {
    if (!stream) return fallback;
    const char* name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream));
    return name && *name ? std::string(name) : fallback;
}

}

std::vector<AudioDeviceEntry> RecordingDevices() {
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&count);
    return ListDevices(ids, count);
}

std::vector<AudioDeviceEntry> PlaybackDevices() {
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    return ListDevices(ids, count);
}

bool HasDevice(const std::vector<AudioDeviceEntry>& devices, const std::string& name) {
    return FindDevice(devices, name) != 0;
}

std::string MatchCaptureAudioDevice(const std::vector<AudioDeviceEntry>& devices,
                                    const std::string& usbProduct, const std::string& card) {
    if (!usbProduct.empty()) {
        for (const auto& device : devices) {
            if (platform::ContainsIgnoreCase(device.name, usbProduct)) return device.name;
        }
    }
    std::set<std::string> keywords;
    for (const auto& word : platform::Words(usbProduct + " " + card)) {
        if (word.size() >= 3) keywords.insert(word);
    }
    std::string best;
    int bestScore = 0;
    for (const auto& device : devices) {
        int score = 0;
        for (const auto& word : platform::Words(device.name)) {
            if (keywords.count(word)) ++score;
        }
        if (score > bestScore) {
            bestScore = score;
            best = device.name;
        }
    }
    return bestScore >= 2 ? best : std::string{};
}

AudioEngine::~AudioEngine() {
    Stop();
}

bool AudioEngine::Start(const AudioConfig& config, std::string& error) {
    Stop();
    config_ = config;
    targetFrames_ = static_cast<size_t>(std::max(5, config.targetMs)) * kSampleRate / 1000;
    hardLimitFrames_ = targetFrames_ + std::max<size_t>(kSampleRate * 40 / 1000,
                                                        static_cast<size_t>(config.deviceFrames) * 3);
    ring_ = std::make_unique<PcmRing>(kSampleRate / 2);
    resampler_ = std::make_unique<SincDriftResampler>(*ring_);
    resampler_->Prepare(kScratchFrames);
    controller_ = QueueDriftController{};
    captureScratch_.assign(kScratchFrames * 2, 0);
    playbackScratch_.assign(kScratchFrames * 2, 0);
    currentGain_ = TargetGain();
    filteredFrames_ = 0.0;
    prefilling_ = true;
    prefillingPublished_ = true;
    ringFrames_ = 0;
    resamplerFrames_ = 0;
    minimumQueueFrames_ = UINT32_MAX;
    appliedPpm_ = 0;
    underrunEvents_ = 0;
    underrunFrames_ = 0;
    trimEvents_ = 0;
    captureCallbacks_ = 0;
    lastCapturePacketFrames_ = 0;
    peakLeft_ = 0;
    peakRight_ = 0;
    clipEvents_ = 0;
    lastClipMs_ = 0;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = kSampleRate;
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, std::to_string(config.deviceFrames).c_str());

    const SDL_AudioDeviceID captureId = FindDevice(RecordingDevices(), config.captureDevice);
    if (!captureId) {
        error = "capture audio device not found: " + config.captureDevice;
        return false;
    }
    SDL_AudioDeviceID outputId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (!config.outputDevice.empty()) {
        if (const SDL_AudioDeviceID id = FindDevice(PlaybackDevices(), config.outputDevice)) {
            outputId = id;
        } else {
            diagnostics::Log("[audio] output '%s' not found; using the default output",
                             config.outputDevice.c_str());
        }
    }

    playbackStream_ = SDL_OpenAudioDeviceStream(outputId, &spec, &AudioEngine::PlaybackCallback, this);
    if (!playbackStream_) {
        error = std::string("audio output: ") + SDL_GetError();
        return false;
    }
    captureStream_ = SDL_OpenAudioDeviceStream(captureId, &spec, &AudioEngine::CaptureCallback, this);
    if (!captureStream_) {
        error = std::string("audio capture: ") + SDL_GetError();
        SDL_DestroyAudioStream(playbackStream_);
        playbackStream_ = nullptr;
        return false;
    }

    captureName_ = StreamDeviceName(captureStream_, config.captureDevice);
    outputName_ = StreamDeviceName(playbackStream_, config.outputDevice);
    outputDeviceFrames_ = DeviceFrames(playbackStream_);
    captureDeviceFrames_ = DeviceFrames(captureStream_);
    running_ = true;
    SDL_ResumeAudioStreamDevice(captureStream_);
    SDL_ResumeAudioStreamDevice(playbackStream_);
    const char* driver = SDL_GetCurrentAudioDriver();
    diagnostics::Log("[audio] %s: capture '%s' (%d frames) -> output '%s' (%d frames), target %d ms, drift %s",
                     driver ? driver : "?", captureName_.c_str(), captureDeviceFrames_,
                     outputName_.c_str(), outputDeviceFrames_, config.targetMs,
                     config.driftCorrection ? "auto" : "off");
    return true;
}

void AudioEngine::Stop() {
    running_ = false;
    if (captureStream_) {
        SDL_DestroyAudioStream(captureStream_);
        captureStream_ = nullptr;
    }
    if (playbackStream_) {
        SDL_DestroyAudioStream(playbackStream_);
        playbackStream_ = nullptr;
    }
    resampler_.reset();
    ring_.reset();
}

bool AudioEngine::OwnsDevice(SDL_AudioDeviceID id) const {
    return (captureStream_ && SDL_GetAudioStreamDevice(captureStream_) == id) ||
           (playbackStream_ && SDL_GetAudioStreamDevice(playbackStream_) == id);
}

void AudioEngine::SetVolume(int master, int left, int right) {
    volume_ = std::clamp(master, 0, 200);
    leftVolume_ = std::clamp(left, 0, 100);
    rightVolume_ = std::clamp(right, 0, 100);
}

void AudioEngine::SetBackgroundMuted(bool muted) {
    backgroundMuted_ = muted;
}

void AudioEngine::ResetMinimum() {
    resetMinimum_ = true;
}

AudioStatus AudioEngine::Status() const {
    AudioStatus status;
    status.running = running_.load();
    status.prefilling = prefillingPublished_.load();
    status.driftCorrection = config_.driftCorrection;
    status.captureName = captureName_;
    status.outputName = outputName_;
    const char* driver = SDL_GetCurrentAudioDriver();
    status.driver = driver ? driver : "";
    status.outputDeviceFrames = outputDeviceFrames_;
    status.captureDeviceFrames = captureDeviceFrames_;
    status.ringFrames = ringFrames_.load();
    status.resamplerFrames = resamplerFrames_.load();
    const uint32_t minimum = minimumQueueFrames_.load();
    status.minimumQueueFrames = minimum == UINT32_MAX ? 0 : minimum;
    status.targetFrames = static_cast<uint32_t>(targetFrames_);
    status.lastCapturePacketFrames = lastCapturePacketFrames_.load();
    status.appliedPpm = appliedPpm_.load();
    status.underrunEvents = underrunEvents_.load();
    status.underrunFrames = underrunFrames_.load();
    status.overrunEvents = ring_ ? ring_->Overruns() : 0;
    status.trimEvents = trimEvents_.load();
    status.captureCallbacks = captureCallbacks_.load();
    return status;
}

void SDLCALL AudioEngine::CaptureCallback(void* userdata, SDL_AudioStream* stream, int, int) {
    static_cast<AudioEngine*>(userdata)->OnCapture(stream);
}

void SDLCALL AudioEngine::PlaybackCallback(void* userdata, SDL_AudioStream* stream, int additional, int) {
    static_cast<AudioEngine*>(userdata)->OnPlayback(stream, additional);
}

void AudioEngine::OnCapture(SDL_AudioStream* stream) {
    if (!ring_) return;
    const int capacityBytes = static_cast<int>(captureScratch_.size() * sizeof(int16_t));
    for (;;) {
        const int available = SDL_GetAudioStreamAvailable(stream);
        if (available < 4) break;
        const int request = std::min(available, capacityBytes) & ~3;
        const int received = SDL_GetAudioStreamData(stream, captureScratch_.data(), request);
        if (received < 4) break;
        const size_t frames = static_cast<size_t>(received) / 4;
        ring_->Push(captureScratch_.data(), frames);
        captureCallbacks_.fetch_add(1, std::memory_order_relaxed);
        lastCapturePacketFrames_.store(static_cast<uint32_t>(frames), std::memory_order_relaxed);
    }
}

void AudioEngine::OnPlayback(SDL_AudioStream* stream, int additionalBytes) {
    if (!ring_ || additionalBytes <= 0) return;
    size_t remaining = static_cast<size_t>(additionalBytes) / 4;
    while (remaining > 0) {
        const size_t chunk = std::min(remaining, kScratchFrames);
        int16_t* output = playbackScratch_.data();
        Render(output, chunk);
        const MixMetrics metrics = ProcessStereoPcm(output, chunk, currentGain_, TargetGain(), true);
        peakLeft_.store(DecayAndHoldPeak(peakLeft_.load(std::memory_order_relaxed), metrics.peakLeft),
                        std::memory_order_relaxed);
        peakRight_.store(DecayAndHoldPeak(peakRight_.load(std::memory_order_relaxed), metrics.peakRight),
                         std::memory_order_relaxed);
        if (metrics.clipped) {
            clipEvents_.fetch_add(1, std::memory_order_relaxed);
            lastClipMs_.store(platform::MonotonicMs(), std::memory_order_relaxed);
        }
        SDL_PutAudioStreamData(stream, output, static_cast<int>(chunk * 4));
        remaining -= chunk;
    }
}

size_t AudioEngine::Render(int16_t* output, size_t frames) {
    const bool drift = config_.driftCorrection;
    size_t queued = ring_->AvailableFrames() + (drift ? resampler_->BufferedFrames() : 0);
    ringFrames_.store(static_cast<uint32_t>(ring_->AvailableFrames()), std::memory_order_relaxed);
    resamplerFrames_.store(drift ? static_cast<uint32_t>(resampler_->BufferedFrames()) : 0,
                           std::memory_order_relaxed);

    if (prefilling_) {
        if (queued < targetFrames_) {
            std::fill_n(output, frames * 2, int16_t{0});
            return 0;
        }
        prefilling_ = false;
        prefillingPublished_.store(false, std::memory_order_relaxed);
        filteredFrames_ = static_cast<double>(queued);
    }

    if (queued > hardLimitFrames_) {
        const size_t excess = queued - targetFrames_;
        if (drift) resampler_->Reset();
        ring_->Discard(excess);
        trimEvents_.fetch_add(1, std::memory_order_relaxed);
        queued = ring_->AvailableFrames();
        filteredFrames_ = static_cast<double>(queued);
    }

    if (resetMinimum_.exchange(false, std::memory_order_relaxed)) {
        minimumQueueFrames_.store(UINT32_MAX, std::memory_order_relaxed);
    }
    const uint32_t queuedFrames = static_cast<uint32_t>(queued);
    if (queuedFrames < minimumQueueFrames_.load(std::memory_order_relaxed)) {
        minimumQueueFrames_.store(queuedFrames, std::memory_order_relaxed);
    }

    size_t produced = 0;
    if (drift) {
        filteredFrames_ += (static_cast<double>(queued) - filteredFrames_) *
                           std::min(1.0, static_cast<double>(frames) / 4800.0);
        const double ppm = controller_.Update(filteredFrames_, static_cast<double>(targetFrames_), frames);
        appliedPpm_.store(static_cast<int>(std::lround(ppm)), std::memory_order_relaxed);
        produced = resampler_->Render(output, frames, 1.0 + ppm / 1000000.0);
    } else {
        produced = ring_->Pop(output, frames);
    }

    if (produced < frames) {
        std::fill(output + produced * 2, output + frames * 2, int16_t{0});
        underrunEvents_.fetch_add(1, std::memory_order_relaxed);
        underrunFrames_.fetch_add(frames - produced, std::memory_order_relaxed);
        prefilling_ = true;
        prefillingPublished_.store(true, std::memory_order_relaxed);
        if (drift) resampler_->Reset();
    }
    return produced;
}

StereoGain AudioEngine::TargetGain() const {
    if (backgroundMuted_.load(std::memory_order_relaxed)) return {0.0, 0.0};
    const double master = volume_.load(std::memory_order_relaxed) / 100.0;
    return {master * leftVolume_.load(std::memory_order_relaxed) / 100.0,
            master * rightVolume_.load(std::memory_order_relaxed) / 100.0};
}

}
