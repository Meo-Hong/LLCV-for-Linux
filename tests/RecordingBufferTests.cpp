#include "recording/RecordingBufferPool.h"
#include <mfidl.h>
#include <cstdio>
#include <thread>
using namespace llcv::recording;
using Microsoft::WRL::ComPtr;
static int failures=0;
static void Expect(bool value,const char* message){if(!value){++failures;printf("FAIL: %s\n",message);}}
static void Check(HRESULT hr){if(FAILED(hr))throw hr;}
int main(){
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));Check(MFStartup(MF_VERSION));
    try{
        RecordingBufferPool pool;
        Expect(!pool.Acquire(),"empty pool is nonblocking");
        pool.Allocate(2,1024);
        auto a=pool.Acquire(),b=pool.Acquire();
        Expect(a && b && a.Get()!=b.Get(),"distinct leases");
        Expect(!pool.Acquire(),"exhaustion does not allocate or reuse a leased buffer");
        BYTE* data=nullptr;DWORD maximum=0,length=0;
        Check(a->Lock(&data,&maximum,&length));
        Expect(maximum==1024 && length==0,"capacity and initial length");
        memset(data,0x5a,maximum);Check(a->Unlock());Check(a->SetCurrentLength(1024));
        Expect(FAILED(a->SetCurrentLength(1025)),"capacity cannot be exceeded");
        ComPtr<IUnknown> identity;Check(a.As(&identity));
        ComPtr<IMFMediaBuffer> same;Check(identity.As(&same));
        Expect(a.Get()==same.Get(),"COM identity remains on wrapper");
        identity.Reset();same.Reset();
        ComPtr<IMFSample> sample;Check(MFCreateSample(&sample));Check(sample->AddBuffer(a.Get()));
        auto* original=a.Get();a.Reset();
        Expect(!pool.Acquire(),"sample-only downstream retention prevents reuse");
        ComPtr<IMFMediaBuffer> retained;Check(sample->GetBufferByIndex(0,&retained));
        sample.Reset();
        Expect(!pool.Acquire(),"buffer-only downstream retention prevents reuse");
        Check(retained->Lock(&data,nullptr,&length));
        Expect(length==1024 && data[0]==0x5a && data[1023]==0x5a,"retained payload unchanged");
        Check(retained->Unlock());retained.Reset();
        a=pool.Acquire();Expect(a.Get()==original,"fully released lease is reused");
        Check(a->GetCurrentLength(&length));Expect(length==0,"new lease resets length");
        pool.Clear();
        Check(b->Lock(&data,&maximum,nullptr));data[0]=42;Check(b->Unlock());
        Expect(maximum==1024,"lease remains valid after pool destruction");
        a.Reset();b.Reset();
        pool.Allocate(4,sizeof(unsigned));
        std::atomic<IMFMediaBuffer*> mailbox{nullptr};
        std::atomic<bool> bad{false};
        constexpr unsigned iterations=50000;
        std::thread consumer([&]{
            for(unsigned i=0;i<iterations;++i){
                IMFMediaBuffer* buffer=nullptr;
                do {
                    buffer=mailbox.exchange(nullptr,std::memory_order_acquire);
                    if(!buffer)std::this_thread::yield();
                }while(!buffer);
                BYTE* bytes=nullptr;DWORD used=0;
                if(FAILED(buffer->Lock(&bytes,nullptr,&used)))bad=true;
                else {
                    unsigned value=0;memcpy(&value,bytes,sizeof(value));
                    if(used!=sizeof(value)||value!=i)bad=true;
                    buffer->Unlock();
                }
                buffer->Release(); // Return lease from an encoder-like second thread.
            }
        });
        for(unsigned i=0;i<iterations;++i){
            ComPtr<IMFMediaBuffer> lease;
            do {lease=pool.Acquire();if(!lease)std::this_thread::yield();}while(!lease);
            Check(lease->Lock(&data,nullptr,nullptr));memcpy(data,&i,sizeof(i));Check(lease->Unlock());
            Check(lease->SetCurrentLength(sizeof(i)));
            while(mailbox.load(std::memory_order_acquire))std::this_thread::yield();
            mailbox.store(lease.Detach(),std::memory_order_release);
        }
        consumer.join();Expect(!bad,"concurrent release/acquire preserves 50,000 payloads");
        pool.Clear();
    }catch(HRESULT hr){printf("HRESULT %08lX\n",static_cast<unsigned long>(hr));++failures;}
    MFShutdown();CoUninitialize();return failures?1:0;
}
