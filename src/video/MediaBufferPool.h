#pragma once

#include <mfapi.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <new>
#include <utility>

namespace llcv::video {

// A small cache, not a frame queue. Buffers held by a transform or caller are
// never reused. The wrapper owns its actual refcount; we do not infer lifetime
// from the unspecified return value of a third-party COM object's AddRef.
class MediaBufferPool {
    class Buffer final : public IMFMediaBuffer {
    public:
        static HRESULT Create(DWORD bytes, DWORD alignment, Buffer** result) {
            *result = nullptr;
            Microsoft::WRL::ComPtr<IMFMediaBuffer> memory;
            const HRESULT hr = MFCreateAlignedMemoryBuffer(bytes, alignment ? alignment - 1 : 0, &memory);
            if (FAILED(hr)) return hr;
            auto* buffer = new (std::nothrow) Buffer;
            if (!buffer) return E_OUTOFMEMORY;
            buffer->memory_ = std::move(memory);
            buffer->bytes = bytes;
            buffer->alignment = alignment;
            *result = buffer;
            return S_OK;
        }
        bool Available() const { return references_.load(std::memory_order_acquire) == 1; }
        STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
            if (!object) return E_POINTER;
            *object = nullptr;
            if (iid != IID_IUnknown && iid != __uuidof(IMFMediaBuffer)) return E_NOINTERFACE;
            *object = static_cast<IMFMediaBuffer*>(this); AddRef(); return S_OK;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return references_.fetch_add(1) + 1; }
        STDMETHODIMP_(ULONG) Release() override {
            const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (!remaining) delete this;
            return remaining;
        }
        STDMETHODIMP Lock(BYTE** data, DWORD* capacity, DWORD* length) override {
            return memory_->Lock(data, capacity, length);
        }
        STDMETHODIMP Unlock() override { return memory_->Unlock(); }
        STDMETHODIMP GetCurrentLength(DWORD* length) override { return memory_->GetCurrentLength(length); }
        STDMETHODIMP SetCurrentLength(DWORD length) override { return memory_->SetCurrentLength(length); }
        STDMETHODIMP GetMaxLength(DWORD* length) override { return memory_->GetMaxLength(length); }
        DWORD bytes = 0, alignment = 0;
    private:
        std::atomic<ULONG> references_{1};
        Microsoft::WRL::ComPtr<IMFMediaBuffer> memory_;
    };
public:
    static constexpr unsigned kCapacity = 3;
    struct Statistics { unsigned allocations = 0, reuses = 0; };
    MediaBufferPool() = default;
    ~MediaBufferPool() { Reset(); }
    MediaBufferPool(const MediaBufferPool&) = delete;
    MediaBufferPool& operator=(const MediaBufferPool&) = delete;

    HRESULT Acquire(DWORD bytes, DWORD alignment, IMFMediaBuffer** result) {
        if (!result) return E_POINTER;
        *result = nullptr;
        // MFT stream-info alignment is a byte boundary; allocation takes a
        // boundary-minus-one mask. Zero means no particular requirement.
        if (!bytes || (alignment && (alignment & (alignment - 1)))) return E_INVALIDARG;
        Buffer** empty = nullptr;
        for (auto& buffer : buffers_) {
            if (!buffer || buffer->Available()) {
                if (!empty) empty = &buffer;
                if (buffer && buffer->bytes >= bytes && buffer->alignment >= alignment) {
                    const HRESULT hr = buffer->SetCurrentLength(0);
                    if (FAILED(hr)) return hr;
                    buffer->AddRef(); *result = buffer; ++statistics_.reuses;
                    return S_OK;
                }
            }
        }
        Buffer* created = nullptr;
        const HRESULT hr = Buffer::Create(bytes, alignment, &created);
        if (FAILED(hr)) return hr;
        ++statistics_.allocations;
        if (empty) {
            if (*empty) (*empty)->Release();
            *empty = created; created->AddRef();
        }
        // When all cached buffers are retained externally, this allocation is
        // owned only by the consumer and never expands the bounded cache.
        *result = created;
        return S_OK;
    }
    void Reset() {
        for (auto& buffer : buffers_) {
            if (buffer) buffer->Release();
            buffer = nullptr;
        }
        statistics_ = {};
    }
    Statistics Stats() const { return statistics_; }
private:
    std::array<Buffer*, kCapacity> buffers_{};
    Statistics statistics_{};
};

} // namespace llcv::video
