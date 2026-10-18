#include "recording/RecordingPauseTimeline.h"
#include "recording/RecordingScaler.h"
#include "recording/RecordingOptions.h"
#include "recording/RecordingSplitBoundary.h"
#include <mfapi.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <array>
#include <cstring>
#include <limits>
using namespace llcv::recording;
using Microsoft::WRL::ComPtr;
void Check(bool ok,const char* why){if(!ok){printf("FAIL %s\n",why);std::exit(1);}}
void Hr(HRESULT hr){Check(SUCCEEDED(hr),"media buffer operation");}
ComPtr<IMFMediaBuffer> Buffer(DWORD size){
    ComPtr<IMFMediaBuffer> buffer;Hr(MFCreateMemoryBuffer(size,&buffer));
    Hr(buffer->SetCurrentLength(size));return buffer;
}
std::vector<BYTE> Contents(IMFMediaBuffer* buffer){
    BYTE* data=nullptr;DWORD size=0;Hr(buffer->Lock(&data,nullptr,&size));
    std::vector<BYTE> contents(data,data+size);Hr(buffer->Unlock());return contents;
}
void TestDecoderStrideScaling(){
    for(bool nv12:{true,false}){
        const LONG packedStride=nv12?8:16;
        const int rows=nv12?6:4;
        auto source=[&](LONG pitch){
            auto buffer=Buffer(pitch*rows);BYTE* data=nullptr;Hr(buffer->Lock(&data,nullptr,nullptr));
            std::memset(data,0xff,pitch*rows);
            for(int y=0;y<rows;++y)for(int x=0;x<packedStride;++x)
                data[y*pitch+x]=BYTE((x*13+y*31)%251);
            Hr(buffer->Unlock());return buffer;
        };
        auto packed=source(packedStride);
        RecordingScaler baseline,direct;
        baseline.Configure(nv12,8,4,4,2);direct.Configure(nv12,8,4,4,2);
        ComPtr<IMFMediaBuffer> expected;Hr(baseline.Apply(packed.Get(),&expected));
        const auto reference=Contents(expected.Get());
        for(LONG pitch:{packedStride,packedStride+4,packedStride+8}){
            auto padded=source(pitch);const auto original=Contents(padded.Get());
            ComPtr<IMFMediaBuffer> actual;Hr(direct.Apply(padded.Get(),&actual,pitch));
            Check(Contents(actual.Get())==reference,"direct padded scaling equals packed staging for both formats");
            Check(Contents(padded.Get())==original,"scaling never changes decoder-owned source bytes");
            const DWORD required=pitch*rows;
            Hr(padded->SetCurrentLength(required-1));
            ComPtr<IMFMediaBuffer> rejected;
            Check(direct.Apply(padded.Get(),&rejected,pitch)==MF_E_INVALID_STREAM_DATA&&!rejected,
                "short padded luma/chroma storage is rejected before access");
            Hr(padded->SetCurrentLength(required));
            for(LONG invalid:{LONG{-1},packedStride-2,packedStride+1,(std::numeric_limits<LONG>::max)()-1})
                Check(direct.Apply(padded.Get(),&rejected,invalid)==MF_E_INVALID_STREAM_DATA&&!rejected,
                    "negative, short, odd and overflowing strides are rejected");
        }
        auto padded=source(packedStride+4);
        std::array<ComPtr<IMFMediaBuffer>,16> retained;
        for(auto& output:retained)Hr(direct.Apply(padded.Get(),&output,packedStride+4));
        ComPtr<IMFMediaBuffer> extra;
        Check(direct.Apply(padded.Get(),&extra,packedStride+4)==HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)&&!extra,
            "retained scaled frames cannot be overwritten when the bounded pool is full");
        BYTE* data=nullptr;Hr(padded->Lock(&data,nullptr,nullptr));
        std::memset(data,0,(packedStride+4)*rows);Hr(padded->Unlock());
        retained[1].Reset();Hr(direct.Apply(padded.Get(),&extra,packedStride+4));
        Check(Contents(retained[0].Get())==reference,"decoder source reuse leaves retained encoded frame unchanged");
        padded.Reset();direct.Clear();
        Check(Contents(retained[0].Get())==reference,"scaled lease survives source and scaler destruction");

        RecordingBufferPool nativePool;nativePool.Allocate(1,packedStride*rows);
        auto native=nativePool.Acquire();Hr(native->SetCurrentLength(packedStride*rows));
        RecordingScaler identity;identity.Configure(nv12,8,4,8,4);
        ComPtr<IMFMediaBuffer> rejected;
        Check(identity.Apply(packed.Get(),&rejected,packedStride+4)==MF_E_INVALID_STREAM_DATA&&!rejected,
            "native path cannot silently forward padded frames as packed encoder input");
        ComPtr<IMFMediaBuffer> lease;Hr(identity.Apply(native.Get(),&lease));native.Reset();
        Check(!nativePool.Acquire(),"native recording buffer remains leased while encoder retains it");
        lease.Reset();Check(bool(nativePool.Acquire()),"native recording buffer returns only after encoder releases it");
    }
}
void BenchmarkDecoderStrideScaling(){
    constexpr int width=3840,height=2160,pitch=width+32,rows=height*3/2;
    auto padded=Buffer(pitch*rows),staging=Buffer(width*rows);
    BYTE* data=nullptr;Hr(padded->Lock(&data,nullptr,nullptr));std::memset(data,96,pitch*rows);Hr(padded->Unlock());
    RecordingScaler baseline,direct;
    baseline.Configure(true,width,height,1920,1080);direct.Configure(true,width,height,1920,1080);
    auto oldPath=[&]{
        BYTE *source=nullptr,*target=nullptr;Hr(padded->Lock(&source,nullptr,nullptr));Hr(staging->Lock(&target,nullptr,nullptr));
        for(int y=0;y<rows;++y)std::memcpy(target+static_cast<size_t>(y)*width,source+static_cast<size_t>(y)*pitch,width);
        Hr(staging->Unlock());Hr(padded->Unlock());
        ComPtr<IMFMediaBuffer> output;Hr(baseline.Apply(staging.Get(),&output));return output;
    };
    auto newPath=[&]{ComPtr<IMFMediaBuffer> output;Hr(direct.Apply(padded.Get(),&output,pitch));return output;};
    {auto old=oldPath(),current=newPath();Check(Contents(old.Get())==Contents(current.Get()),"4K direct scaling equals staged scaling");}
    constexpr int iterations=3;
    const auto before=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i)oldPath();
    const auto middle=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i)newPath();
    const auto after=std::chrono::steady_clock::now();
    printf("4K->1080p MJPEG worker scaling (3 measured iterations after warm-up): staged %.3f ms/frame, direct %.3f ms/frame; avoids %d copied bytes/frame and full-resolution pool.\n",
        std::chrono::duration<double,std::milli>(middle-before).count()/iterations,
        std::chrono::duration<double,std::milli>(after-middle).count()/iterations,width*rows);
}
void TestRecordingStartTimeline(){
    constexpr int64_t ms=10000;
    RecordingPauseTimeline videoFirst;
    const auto video=videoFirst.Map(true,0,100*ms,20*ms);
    Check(video.keep&&video.time==0,"first arriving video anchors recording start");
    const auto straddling=videoFirst.Map(false,0,95*ms,20*ms);
    Check(straddling.keep&&straddling.time==0&&straddling.duration==15*ms&&straddling.skipAudioFrames==240,
        "late initial PCM retains all 15 ms after first video, trimming only its 5 ms prefix");
    const auto following=videoFirst.Map(false,0,115*ms,20*ms);
    Check(following.keep&&following.time==15*ms&&following.duration==20*ms&&!following.skipAudioFrames,
        "initial trimmed PCM joins the next packet without a gap or duplicate audio");

    RecordingPauseTimeline beforeCut;
    Check(beforeCut.Map(true,0,100*ms,20*ms).keep,"video anchors prefix-only tests");
    Check(!beforeCut.Map(false,0,70*ms,20*ms).keep,"PCM wholly before recording start is discarded");
    Check(!beforeCut.Map(false,0,80*ms,20*ms).keep,"PCM ending exactly at recording start is discarded");
    const auto atCut=beforeCut.Map(false,0,100*ms,20*ms);
    Check(atCut.keep&&atCut.time==0&&atCut.duration==20*ms&&!atCut.skipAudioFrames,
        "discarded prefixes never advance the audio clock past valid initial PCM");

    RecordingPauseTimeline audioFirst;
    const auto firstAudio=audioFirst.Map(false,0,95*ms,20*ms);
    Check(firstAudio.keep&&firstAudio.time==0&&firstAudio.duration==20*ms&&!firstAudio.skipAudioFrames,
        "first arriving audio remains intact and anchors recording start");
    const auto delayedVideo=audioFirst.Map(true,0,100*ms,20*ms);
    Check(delayedVideo.keep&&delayedVideo.time==5*ms&&delayedVideo.duration==20*ms,
        "audio-first recording preserves the original 5 ms video offset");
    const auto nextAudio=audioFirst.Map(false,0,115*ms,20*ms);
    Check(nextAudio.keep&&nextAudio.time==20*ms&&!nextAudio.skipAudioFrames,
        "audio-first packet cadence is unchanged");

    RecordingPauseTimeline rounded;
    Check(rounded.Map(true,0,100*ms,20*ms).keep,"video anchors sample rounding test");
    const auto fractional=rounded.Map(false,0,100*ms-1,10*ms);
    Check(fractional.keep&&fractional.skipAudioFrames==1&&fractional.time==207&&fractional.duration==10*ms-208,
        "non-aligned initial cut rounds up to one whole 48 kHz PCM frame without negative timestamps");

    // Initial trimming must not change resume's shared video/audio endpoint.
    Check(!videoFirst.Map(false,1,200*ms,20*ms).keep,"paused initial-overlap audio is omitted");
    Check(videoFirst.NeedsVideo(2)&&!videoFirst.Map(false,2,995*ms,20*ms).keep,
        "resume still waits for video after initial trimming");
    const auto resumedVideo=videoFirst.Map(true,2,1000*ms,20*ms);
    Check(resumedVideo.keep&&resumedVideo.time==35*ms,"resume uses longest pre-pause stream end");
    const auto resumedAudio=videoFirst.Map(false,2,995*ms,20*ms);
    Check(resumedAudio.keep&&resumedAudio.time==35*ms&&resumedAudio.duration==15*ms&&resumedAudio.skipAudioFrames==240,
        "resume PCM trimming stays identical after an initial overlap");
}
int main(){
    Hr(MFStartup(MF_VERSION));
    const uint8_t cra[]={0,0,0,1,42,1,7},idr[]={0,0,1,40,1,7},lengthIdr[]={0,0,0,3,40,1,7};
    Check(!HasHevcIdr(cra,sizeof(cra)),"open GOP CRA is not a safe reordered split");
    Check(HasHevcIdr(idr,sizeof(idr))&&HasHevcIdr(lengthIdr,sizeof(lengthIdr)),"Annex B and length-prefixed closed IDRs");
    Options o;
    for(int cap:{0,720,1080,1440})for(auto size:{VideoSize{1920,1080},VideoSize{2560,1440},VideoSize{1280,960},VideoSize{640,360}}){
        o.maxHeight=cap;auto out=OutputSize(o,size.width,size.height);
        Check(out.width<=size.width&&out.height<=size.height,"never upscale");
        Check(!(out.width&1)&&!(out.height&1),"chroma-aligned dimensions");
        Check(std::abs(double(out.width)/out.height-double(size.width)/size.height)<0.003,"aspect ratio preserved");
    }
    for(bool nv12:{true,false}){
        ComPtr<IMFMediaBuffer> input;const DWORD size=nv12?48:64;Hr(MFCreateMemoryBuffer(size,&input));
        BYTE* data=nullptr;Hr(input->Lock(&data,nullptr,nullptr));
        if(nv12){for(int y=0;y<4;++y)for(int x=0;x<8;++x)data[y*8+x]=BYTE(y*20+x*2);
            for(int i=32;i<48;i+=2){data[i]=80;data[i+1]=180;}}
        else for(int y=0;y<4;++y)for(int x=0;x<8;++x){data[y*16+x*2]=BYTE(y*20+x*2);data[y*16+x*2+1]=(x&1)?180:80;}
        Hr(input->Unlock());Hr(input->SetCurrentLength(size));
        RecordingScaler scaler;scaler.Configure(nv12,8,4,8,4);
        ComPtr<IMFMediaBuffer> same;Hr(scaler.Apply(input.Get(),&same));Check(same.Get()==input.Get(),"native path retains original lease");
        scaler.Configure(nv12,8,4,4,2);
        ComPtr<IMFMediaBuffer> output;Hr(scaler.Apply(input.Get(),&output));
        DWORD length=0;Hr(output->GetCurrentLength(&length));Check(length==(nv12?12u:16u),"scaled buffer length");
        Hr(output->Lock(&data,nullptr,nullptr));
        for(int y=0;y<2;++y)for(int x=0;x<4;++x)
            Check(data[y*(nv12?4:8)+x*(nv12?1:2)]==11+y*40+x*4,"bilinear luma ramp");
        if(nv12){for(int i=8;i<12;++i)Check(data[i]==((i&1)?180:80),"NV12 chroma order");}
        else{for(int i=1;i<16;i+=2)Check(data[i]==((i%4==3)?180:80),"YUY2 chroma order");}
        Hr(output->Unlock());
    }
    TestDecoderStrideScaling();BenchmarkDecoderStrideScaling();TestRecordingStartTimeline();
    constexpr int64_t second=10000000;
    RecordingPauseTimeline t;
    Check(t.Map(true,0,0,second/10).keep,"first video");
    Check(t.Map(false,0,0,second/10).keep,"first audio");
    Check(!t.Map(true,1,second,second/10).keep,"paused samples omitted");
    Check(t.NeedsVideo(2)&&!t.Map(false,2,second,second/10).keep,"resume waits for video anchor");
    auto v=t.Map(true,2,second,second/10);
    Check(v.keep&&v.time==second/10,"resume removes gap on common clock");
    auto a=t.Map(false,2,second-50000,100000);
    Check(a.keep&&a.time==second/10&&a.skipAudioFrames==240&&a.duration==50000,"straddling PCM trimmed exactly at shared boundary");
    Check(!t.Map(false,0,second,100000).keep,"stale callback generation discarded");
    Check(!t.Map(false,2,second-200000,100000).keep,"audio wholly before resume omitted");
    auto v2=t.Map(true,6,5*second,second/10);
    Check(v2.keep&&v2.time==second/5,"rapid pause resume skips empty epochs");
    Check(t.Map(false,6,5*second,100000).time==v2.time,"audio follows resumed video without drift");
    MFShutdown();puts("Recording scaling and pause timeline passed.");return 0;
}
