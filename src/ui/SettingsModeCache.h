#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace llcv::settings_ui {

struct ModeQueryKey {
    std::wstring device;
    int width = 0, height = 0;
    bool operator==(const ModeQueryKey&) const = default;
};

// One serialized worker per settings dialog. Cached results are session-local;
// explicit refresh invalidates even in-flight results. Only the latest pending
// choice is queried, and the UI takes results for its current key only.
template<class Result>
class SettingsModeCache {
public:
    using Query = std::function<Result(const ModeQueryKey&)>;
    SettingsModeCache(Query query, std::function<void()> notify)
        : query_(std::move(query)), notify_(std::move(notify)),
          worker_([this] { Run(); }) {}
    ~SettingsModeCache() { Stop(); }
    SettingsModeCache(const SettingsModeCache&) = delete;
    SettingsModeCache& operator=(const SettingsModeCache&) = delete;

    std::optional<Result> Request(const ModeQueryKey& key) {
        std::lock_guard lock(mutex_);
        for (const auto& entry : cache_) if (entry.first == key) {
            pending_.reset(); // do not query a superseded, not-yet-started choice
            return entry.second;
        }
        if (!stopping_ && (!active_ || active_->key != key || active_->epoch != epoch_)) {
            pending_ = Work{key, epoch_};
            wake_.notify_one();
        } else if (active_ && active_->key == key) {
            pending_.reset();
        }
        return std::nullopt;
    }
    void Invalidate() {
        std::lock_guard lock(mutex_);
        ++epoch_;
        cache_.clear();
        pending_.reset();
    }
    void Stop() {
        { std::lock_guard lock(mutex_); stopping_ = true; pending_.reset(); }
        wake_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
private:
    struct Work { ModeQueryKey key; unsigned epoch; };
    void Run() {
        for (;;) {
            Work work;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                work = *pending_; pending_.reset(); active_ = work;
            }
            Result result = query_(work.key);
            bool notify = false;
            {
                std::lock_guard lock(mutex_);
                active_.reset();
                if (!stopping_ && work.epoch == epoch_) {
                    cache_.emplace_back(work.key, std::move(result));
                    notify = true;
                }
            }
            if (notify) notify_();
        }
    }
    Query query_;
    std::function<void()> notify_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    unsigned epoch_ = 0;
    std::optional<Work> pending_, active_;
    std::vector<std::pair<ModeQueryKey, Result>> cache_;
    std::thread worker_;
};
} // namespace llcv::settings_ui
