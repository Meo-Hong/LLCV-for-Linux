#pragma once
#include "recording/RecordingBufferPool.h"
#include <algorithm>
#include <vector>

namespace llcv::recording {
// Worker-only bilinear downscale. The default/native-size path returns the
// original lease and performs no conversion or additional allocation.
class RecordingScaler {
    struct Tap { int a,b,weight; };
    struct Plane {
        int sourceOffset,targetOffset,sourceStride,targetStride,step,width,height;
        std::vector<Tap> x,y;
    };
    RecordingBufferPool buffers_;
    std::vector<Plane> planes_;
    DWORD size_=0;
    static std::vector<Tap> Taps(int source,int target) {
        std::vector<Tap> taps; taps.reserve(target);
        for(int i=0;i<target;++i){
            const auto p=std::clamp(((int64_t(i)*2+1)*source*128/target)-128,int64_t{0},int64_t(source-1)*256);
            taps.push_back({int(p/256),std::min(source-1,int(p/256)+1),int(p%256)});
        }
        return taps;
    }
public:
    void Clear() noexcept { planes_.clear();buffers_.Clear();size_=0; }
    void Configure(bool nv12,int width,int height,int outWidth,int outHeight) {
        Clear();
        if(width==outWidth && height==outHeight)return;
        auto add=[&](int sourceOffset,int targetOffset,int sourceStride,int targetStride,
                     int step,int sw,int sh,int dw,int dh){
            planes_.push_back({sourceOffset,targetOffset,sourceStride,targetStride,step,dw,dh,Taps(sw,dw),Taps(sh,dh)});
        };
        if(nv12){
            add(0,0,width,outWidth,1,width,height,outWidth,outHeight);
            add(width*height,outWidth*outHeight,width,outWidth,2,width/2,height/2,outWidth/2,outHeight/2);
            add(width*height+1,outWidth*outHeight+1,width,outWidth,2,width/2,height/2,outWidth/2,outHeight/2);
            size_=outWidth*outHeight*3/2;
        }else{
            add(0,0,width*2,outWidth*2,2,width,height,outWidth,outHeight);
            add(1,1,width*2,outWidth*2,4,width/2,height,outWidth/2,outHeight);
            add(3,3,width*2,outWidth*2,4,width/2,height,outWidth/2,outHeight);
            size_=outWidth*outHeight*2;
        }
        buffers_.Allocate(16,size_);
    }
    HRESULT Apply(IMFMediaBuffer* input,IMFMediaBuffer** output) {
        if(!input||!output)return E_POINTER;
        *output=nullptr;
        if(planes_.empty()){input->AddRef();*output=input;return S_OK;}
        auto target=buffers_.Acquire();if(!target)return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        BYTE *src=nullptr,*dst=nullptr;
        HRESULT hr=input->Lock(&src,nullptr,nullptr);if(FAILED(hr))return hr;
        hr=target->Lock(&dst,nullptr,nullptr);
        if(SUCCEEDED(hr)){
            for(const auto& p:planes_)for(int y=0;y<p.height;++y){
                const auto& ty=p.y[y];auto* row=dst+p.targetOffset+y*p.targetStride;
                const auto* a=src+p.sourceOffset+ty.a*p.sourceStride;
                const auto* b=src+p.sourceOffset+ty.b*p.sourceStride;
                for(int x=0;x<p.width;++x){
                    const auto& tx=p.x[x];
                    const int top=a[tx.a*p.step]*(256-tx.weight)+a[tx.b*p.step]*tx.weight;
                    const int bottom=b[tx.a*p.step]*(256-tx.weight)+b[tx.b*p.step]*tx.weight;
                    row[x*p.step]=BYTE((top*(256-ty.weight)+bottom*ty.weight+32768)/65536);
                }
            }
            hr=target->Unlock();
        }
        const auto unlocked=input->Unlock();if(SUCCEEDED(hr))hr=unlocked;
        if(SUCCEEDED(hr))hr=target->SetCurrentLength(size_);
        if(SUCCEEDED(hr))*output=target.Detach();
        return hr;
    }
};
}
