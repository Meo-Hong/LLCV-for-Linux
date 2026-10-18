#pragma once
#include <mfapi.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <vector>

namespace llcv::recording {
// The pool owns one reference. Reuse requires EVERY encoder/sample lease to
// return, not just WriteSample returning. Leases can outlive the pool itself.
class RecordingBuffer final : public IMFMediaBuffer {
public:
    explicit RecordingBuffer(DWORD bytes) {
        HRESULT hr=MFCreateAlignedMemoryBuffer(bytes,MF_16_BYTE_ALIGNMENT,&storage_);
        if(FAILED(hr))throw hr;
        BYTE* data=nullptr;
        hr=storage_->Lock(&data,nullptr,nullptr);
        if(FAILED(hr))throw hr;
        std::memset(data,0,bytes); // Commit pages on the worker, not capture.
        storage_->Unlock();
    }
    bool TryLease() noexcept {
        ULONG idle=1;
        return references_.compare_exchange_strong(idle,2,std::memory_order_acquire,
                                                   std::memory_order_relaxed);
    }
    STDMETHODIMP QueryInterface(REFIID id,void** object) override {
        if(!object)return E_POINTER;
        *object=nullptr;
        if(id!=IID_IUnknown && id!=__uuidof(IMFMediaBuffer))return E_NOINTERFACE;
        *object=static_cast<IMFMediaBuffer*>(this);AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return references_.fetch_add(1,std::memory_order_relaxed)+1; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG left=references_.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!left)delete this;
        return left;
    }
    STDMETHODIMP Lock(BYTE** data,DWORD* maximum,DWORD* current) override { return storage_->Lock(data,maximum,current); }
    STDMETHODIMP Unlock() override { return storage_->Unlock(); }
    STDMETHODIMP GetCurrentLength(DWORD* length) override { return storage_->GetCurrentLength(length); }
    STDMETHODIMP SetCurrentLength(DWORD length) override { return storage_->SetCurrentLength(length); }
    STDMETHODIMP GetMaxLength(DWORD* length) override { return storage_->GetMaxLength(length); }
private:
    std::atomic<ULONG> references_{1};
    Microsoft::WRL::ComPtr<IMFMediaBuffer> storage_;
};

class RecordingBufferPool {
public:
    void Allocate(size_t count,DWORD bytes) {
        std::vector<Microsoft::WRL::ComPtr<RecordingBuffer>> fresh;
        fresh.reserve(count);
        for(size_t i=0;i<count;++i){
            Microsoft::WRL::ComPtr<RecordingBuffer> buffer;
            buffer.Attach(new RecordingBuffer(bytes));
            fresh.push_back(std::move(buffer));
        }
        buffers_.swap(fresh);next_=0;
    }
    // One capture-pin producer; encoder references may be released on any thread.
    // Bounded scan, no allocation, waiting or retention of driver-owned samples.
    Microsoft::WRL::ComPtr<IMFMediaBuffer> Acquire() noexcept {
        Microsoft::WRL::ComPtr<IMFMediaBuffer> result;
        for(size_t i=0;i<buffers_.size();++i){
            const size_t index=next_;
            next_=(next_+1)%buffers_.size();
            if(buffers_[index]->TryLease()){
                result.Attach(buffers_[index].Get());
                result->SetCurrentLength(0);
                break;
            }
        }
        return result;
    }
    void Clear() noexcept { buffers_.clear();next_=0; }
private:
    std::vector<Microsoft::WRL::ComPtr<RecordingBuffer>> buffers_;
    size_t next_=0;
};
} // namespace llcv::recording
