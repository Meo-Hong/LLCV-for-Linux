#include "recording/RecordingPauseTimeline.h"
#include "recording/RecordingScaler.h"
#include "recording/RecordingOptions.h"
#include "recording/RecordingSplitBoundary.h"
#include <mfapi.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
using namespace llcv::recording;
using Microsoft::WRL::ComPtr;
void Check(bool ok,const char* why){if(!ok){printf("FAIL %s\n",why);std::exit(1);}}
void Hr(HRESULT hr){Check(SUCCEEDED(hr),"media buffer operation");}
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
