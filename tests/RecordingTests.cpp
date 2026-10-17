#include "recording/Recorder.h"
#include "recording/RecordingArchive.h"
#include "recording/EncoderSupport.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <filesystem>
#include <cstdio>
#include <chrono>
#include <cmath>
using Microsoft::WRL::ComPtr;
static int failures=0;
static void Expect(bool value,const char* message) { if(!value){++failures;printf("FAIL: %s\n",message);} }
static void Check(HRESULT hr) { if(FAILED(hr)) throw hr; }
static ComPtr<IMediaSample> Sample(IMemAllocator* allocator, long bytes, LONGLONG time,LONGLONG duration) {
    ComPtr<IMediaSample> s; Check(allocator->GetBuffer(&s,nullptr,nullptr,0));
    Check(s->SetActualDataLength(bytes)); LONGLONG end=time+duration; Check(s->SetTime(&time,&end)); return s;
}
static std::vector<double> Pulses(const wchar_t* path,bool video) {
    ComPtr<IMFSourceReader> r; Check(MFCreateSourceReaderFromURL(path,nullptr,&r));
    Check(r->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE));
    DWORD stream=video?MF_SOURCE_READER_FIRST_VIDEO_STREAM:MF_SOURCE_READER_FIRST_AUDIO_STREAM;
    Check(r->SetStreamSelection(stream,TRUE));
    ComPtr<IMFMediaType> type; Check(MFCreateMediaType(&type));
    Check(type->SetGUID(MF_MT_MAJOR_TYPE,video?MFMediaType_Video:MFMediaType_Audio));
    Check(type->SetGUID(MF_MT_SUBTYPE,video?MFVideoFormat_NV12:MFAudioFormat_PCM));
    if(!video){Check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16)); Check(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));Check(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000));}
    Check(r->SetCurrentMediaType(stream,nullptr,type.Get()));
    std::vector<double> result; bool previous=false; LONGLONG last=-1;
    for(;;) {
        DWORD flags=0;LONGLONG time=0;ComPtr<IMFSample> sample;
        Check(r->ReadSample(stream,0,nullptr,&flags,&time,&sample));
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
        if(!sample) continue;
        Expect(time>last,"decoded timestamps monotonic");last=time;
        ComPtr<IMFMediaBuffer> b;Check(sample->ConvertToContiguousBuffer(&b));
        BYTE* data=nullptr;DWORD len=0;Check(b->Lock(&data,nullptr,&len));
        if(video){
            const bool pulse=len && data[0]>160;
            if(pulse&&!previous) result.push_back(time/1e7);
            previous=pulse;
        }else{
            auto* pcm=reinterpret_cast<const short*>(data);
            for(DWORD i=0;i<len/4;++i){
                const bool pulse=std::abs(static_cast<int>(pcm[2*i]))>5000;
                // Debounce the sine wave and AAC pre/post ringing.
                if(pulse && (result.empty() || time/1e7+i/48000.0-result.back()>0.5))
                    result.push_back(time/1e7+i/48000.0);
            }
        }
        b->Unlock();
    }
    return result;
}
static void Inspect(const wchar_t* path,bool video) {
    ComPtr<IMFSourceReader> r;Check(MFCreateSourceReaderFromURL(path,nullptr,&r));
    Check(r->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE));
    const DWORD stream=video?static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM):static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    Check(r->SetStreamSelection(stream,TRUE));
    ComPtr<IMFMediaType> type;Check(MFCreateMediaType(&type));
    Check(type->SetGUID(MF_MT_MAJOR_TYPE,video?MFMediaType_Video:MFMediaType_Audio));
    Check(type->SetGUID(MF_MT_SUBTYPE,video?MFVideoFormat_NV12:MFAudioFormat_PCM));
    if(!video) Check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
    Check(r->SetCurrentMediaType(stream,nullptr,type.Get()));
    LONGLONG first=-1,end=0,last=-1,maxGap=0;unsigned count=0;uint64_t nonzero=0;
    for(;;){
        DWORD flags=0;LONGLONG time=0;ComPtr<IMFSample> sample;
        Check(r->ReadSample(stream,0,nullptr,&flags,&time,&sample));
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
        if(!sample)continue;
        if(first<0)first=time;
        Expect(time>last,"file timestamps monotonic");last=time;
        if(count)maxGap=(std::max)(maxGap,time-end);
        LONGLONG duration=0;Check(sample->GetSampleDuration(&duration));end=time+duration;
        if(!video){
            ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer));
            BYTE* data=nullptr;DWORD bytes=0;Check(buffer->Lock(&data,nullptr,&bytes));
            auto* pcm=reinterpret_cast<short*>(data);
            for(DWORD i=0;i<bytes/2;++i)if(std::abs(static_cast<int>(pcm[i]))>100)++nonzero;
            buffer->Unlock();
        }
        ++count;
    }
    printf("%s samples=%u start=%.6fs end=%.6fs max_gap=%.3fms audible_samples=%llu\n",
        video?"video":"audio",count,first/1e7,end/1e7,maxGap/10000.0,nonzero);
    Expect(count>0,"file contains decoded stream");
}
static void Lifecycle(llcv::recording::Recorder& recorder,
                      const llcv::video::VideoSampleFormat& video,
                      const llcv::capture_audio::Format& audio) {
    llcv::recording::Options options;options.encoder=L"cpu";
    ComPtr<IMemAllocator> allocator;Check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator)));
    ALLOCATOR_PROPERTIES requested{4,640*360*3/2,1,0},actual{};
    Check(allocator->SetProperties(&requested,&actual));Check(allocator->Commit());
    for(int cycle=0;cycle<4;++cycle){
        const auto path=std::filesystem::temp_directory_path()/
            (L"LLCV-lifecycle-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".mp4");
        recorder.Configure(video,audio,{},false,options);
        Expect(recorder.Start(path.wstring()),"restart after completed stop");
        if(cycle>0){
            for(int i=0;i<1000 && recorder.Busy()&&!recorder.Recording();++i)Sleep(10);
            Expect(recorder.Recording(),"restarted encoder ready");
        }
        std::thread vp,ap;
        std::atomic<bool> running{true};
        std::atomic<bool> producerError{false};
        if(cycle>=2){
            auto produce=[&](bool isVideo){
                try{
                    LONGLONG time=0;const LONGLONG duration=isVideo?333333:100000;
                    while(running){
                        auto sample=Sample(allocator.Get(),isVideo?640*360*3/2:1920,time,duration);
                        BYTE* data=nullptr;Check(sample->GetPointer(&data));
                        memset(data,isVideo?128:0,sample->GetActualDataLength());
                        recorder.Push(isVideo,sample.Get());time+=duration;
                        Sleep(isVideo?33:10);
                    }
                }catch(...){producerError=true;}
            };
            vp=std::thread(produce,true);ap=std::thread(produce,false);
            Sleep(150);
        }
        const auto start=std::chrono::steady_clock::now();
        recorder.Stop();recorder.Shutdown();
        running=false;if(vp.joinable())vp.join();if(ap.joinable())ap.join();
        Expect(!producerError,"concurrent callback producers complete");
        Expect(!recorder.Busy()&&!recorder.Recording(),"stop before ready, idle, and active callbacks");
        if(cycle>=2)Expect(SUCCEEDED(recorder.Result()),"concurrent stop finalizes both streams");
        Expect(std::chrono::steady_clock::now()-start<std::chrono::seconds(5),"shutdown wakes idle worker promptly");
    }
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); Check(MFStartup(MF_VERSION));
    try {
        if(argc>1&&std::wstring(argv[1])==L"--encoders"){
            const auto support=llcv::recording::ProbeEncoders();
            for(int i=0;i<3;++i){printf("codec=%d MP4=%d software=%d hardware=%zu HW(bitrate/CQP)=%d/%d CPU=%d/%d\n",i,support[i].container,support[i].software,support[i].hardware.size(),support[i].hardwareRates.bitrate,support[i].hardwareRates.quality,support[i].softwareRates.bitrate,support[i].softwareRates.quality);
                for(auto& encoder:support[i].hardware)wprintf(L"  %ls\n",encoder.name.c_str());}
            MFShutdown();CoUninitialize();return 0;
        }
        if(argc>2 && std::wstring(argv[1])==L"--inspect"){
            Inspect(argv[2],true);Inspect(argv[2],false);
            goto finished;
        }
        llcv::recording::Recorder recorder;
        llcv::video::VideoSampleFormat v{};v.valid=true;v.width=640;v.height=360;v.fps=30;v.stride=640;v.format=llcv::settings::VideoPixelFormat::Nv12;
        llcv::capture_audio::Format a{};a.channels=2;a.containerBits=a.validBits=16;a.blockAlign=4;a.path=llcv::capture_audio::Path::Direct16BitStereo;
        if(argc>1&&std::wstring(argv[1])==L"--lifecycle"){
            Lifecycle(recorder,v,a);MFShutdown();CoUninitialize();return failures?1:0;
        }
        const bool arrival=argc>1 && (std::wstring(argv[1])==L"--arrival" || std::wstring(argv[1])==L"--hevc-arrival");
        const bool limited=argc>1 && std::wstring(argv[1])==L"--limited";
        const bool split=argc>1 && std::wstring(argv[1])==L"--split-pause";
        const bool unaligned=argc>1 && std::wstring(argv[1])==L"--pause-unaligned";
        const bool pause=argc>1 && (std::wstring(argv[1])==L"--pause"||split||unaligned);
        const bool scale=argc>1 && std::wstring(argv[1])==L"--scale";
        const bool hevc=argc>1 && (std::wstring(argv[1])==L"--hevc" || std::wstring(argv[1])==L"--hevc-arrival");
        const bool quality=argc>1 && (std::wstring(argv[1])==L"--quality"||std::wstring(argv[1])==L"--quality-yuy2");
        const bool padded=argc>1 && std::wstring(argv[1])==L"--padded";
        const bool yuy2=argc>1 && (std::wstring(argv[1])==L"--yuy2"||std::wstring(argv[1])==L"--quality-yuy2");
        if(padded)v.stride+=32;
        if(yuy2){v.format=llcv::settings::VideoPixelFormat::Yuy2;v.stride=v.width*2+32;}
        if (arrival) v.fps=50;
        if (argc>1 && std::wstring(argv[1])==L"--probe") {
            v.width=v.stride=2560;v.height=1440;v.fps=120;
            recorder.Configure(v,a,{});
            const auto probe=std::filesystem::temp_directory_path()/(L"LLCV-probe-"+std::to_wstring(GetTickCount64())+L".mp4");
            recorder.Start(probe.wstring());
            while(recorder.Busy()&&!recorder.Recording()) Sleep(10);
            printf("probe result=0x%08lX ready=%d\n",static_cast<unsigned long>(recorder.Result()),recorder.Recording()?1:0);
            recorder.Shutdown();return 0;
        }
        const auto path=std::filesystem::temp_directory_path()/(L"LLCV-record-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".mp4");
        llcv::recording::Options testOptions;testOptions.encoder=L"cpu";
        if(split){testOptions.splitMode=llcv::recording::SplitMode::Time;testOptions.splitSeconds=2;}
        if(scale){v.width=v.stride=1280;v.height=960;testOptions.maxHeight=720;}
        testOptions.rateControl=(quality||hevc)?llcv::recording::RateControl::Quality:llcv::recording::RateControl::Bitrate;
        if(limited){v.fps=60;testOptions.maxFps=30;testOptions.videoMbps=4;testOptions.audioKbps=128;}
        if(hevc){
            const auto support=llcv::recording::ProbeEncoders();
            if(!support[1].hardwareRates.quality){
                puts("SKIP: HEVC hardware quality encoder unavailable");
                MFShutdown();CoUninitialize();return 77;
            }
            testOptions.codec=llcv::recording::Codec::Hevc;testOptions.encoder.clear();
        }
        recorder.Configure(v,a,{},arrival,testOptions);
        Expect(recorder.Start(path.wstring()),"start");
        for(int i=0;i<1000&&!recorder.Recording()&&recorder.Busy();++i) Sleep(10);
        Expect(recorder.Recording(),"encoder ready");
        ComPtr<IMemAllocator> allocator;Check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator)));
        const long frameBytes=static_cast<long>(v.stride*(yuy2?v.height:v.height*3/2));
        ALLOCATOR_PROPERTIES requested{2,frameBytes,1,0},actual{};Check(allocator->SetProperties(&requested,&actual));Check(allocator->Commit());
        auto start=std::chrono::steady_clock::now();double maxPushUs=0,totalPushUs=0;unsigned pushes=0;
        std::vector<double> flashWall;
        double audioWall=0;
        auto wall=[] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
        for(int tick=0;tick<600 && recorder.Recording();++tick){
            std::this_thread::sleep_until(start+std::chrono::milliseconds(tick*10));
            if(pause&&(tick==(unaligned?185:180)||tick==(unaligned?255:250))){
                const bool pausing=tick<200;
                Expect(recorder.SetPaused(pausing),"pause/resume accepted");
                Expect(recorder.GetSnapshot().paused==pausing,"pause snapshot changes immediately");
            }
            if(tick%(arrival?2:10)==0){
                // 30fps timestamps / 3 frames every 100ms, deliberately bursty.
                for(int j=0;j<(arrival?1:(limited?6:3));++j){
                    int frame=arrival?tick/2:tick/10*(limited?6:3)+j; LONGLONG time=frame*10000000LL/v.fps;
                    auto s=Sample(allocator.Get(),frameBytes,time,10000000/v.fps);BYTE* d=nullptr;s->GetPointer(&d);
                    if(arrival) s->SetTime(nullptr,nullptr);
                    const bool pulse=(time>=10000000&&time<11000000)||(time>=30000000&&time<31000000)||(time>=50000000&&time<51000000);
                    memset(d,0xff,frameBytes); // Padding must not enter encoded rows.
                    for(int row=0;row<(yuy2?v.height:v.height*3/2);++row){
                        auto* line=d+row*v.stride;
                        if(yuy2)for(int x=0;x<v.width;++x){line[x*2]=pulse?220:16;line[x*2+1]=128;}
                        else memset(line,row<v.height?(pulse?220:16):128,v.width);
                    }
                    auto t=std::chrono::steady_clock::now();
                    if(arrival && (time==10000000 || time==30000000 || time==50000000)) flashWall.push_back(wall());
                    recorder.Push(true,s.Get());
                    double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count();
                    maxPushUs=(std::max)(maxPushUs,us);totalPushUs+=us;++pushes;
                }
            }
            auto s=Sample(allocator.Get(),1920,tick*100000LL,100000);BYTE* d=nullptr;s->GetPointer(&d);auto* pcm=reinterpret_cast<short*>(d);
            if(arrival) s->SetTime(nullptr,nullptr);
            const bool pulse=(tick>=100&&tick<110)||(tick>=300&&tick<310)||(tick>=500&&tick<510);
            for(int i=0;i<480;++i) pcm[i*2]=pcm[i*2+1]=pulse?static_cast<short>(12000*sin(i*6.28318530718*1000/48000)):0;
            if(tick==0)audioWall=wall();
            recorder.Push(false,s.Get());
        }
        recorder.Stop();recorder.Shutdown();
        printf("record result=0x%08lX, video enqueue mean=%.2fus max=%.2fus\n",static_cast<unsigned long>(recorder.Result()),totalPushUs/(std::max)(1u,pushes),maxPushUs);
        Expect(SUCCEEDED(recorder.Result()),"record/finalize");
        if(split){
            unsigned frames=0,parts=0,events=0;
            for(unsigned part=1;;++part){
                const auto segment=llcv::recording::RecordingPartPath(path.wstring(),part);
                if(!std::filesystem::exists(segment))break;
                ++parts;
                ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(segment.c_str(),nullptr,&reader));
                for(;;){ComPtr<IMFSample> sample;DWORD flags=0;Check(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,nullptr,&sample));
                    if(sample)++frames;if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;}
                const auto vp=Pulses(segment.c_str(),true),ap=Pulses(segment.c_str(),false);
                Expect(vp.size()==ap.size(),"split decoded flash/tone counts agree");
                events+=static_cast<unsigned>(vp.size());
                for(size_t i=0;i<std::min(vp.size(),ap.size());++i)Expect(std::abs(vp[i]-ap[i])<0.04,"split decoded A/V skew below 40 ms");
                printf("split: %ls\n",segment.c_str());
            }
            Expect(parts>=2&&frames==159&&events==3,"split and pause preserve every intended frame/event");
            goto finished;
        }
        if(SUCCEEDED(recorder.Result())){
            if(pause||scale){
                ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
                ComPtr<IMFMediaType> media;Check(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&media));
                UINT32 width=0,height=0;Check(MFGetAttributeSize(media.Get(),MF_MT_FRAME_SIZE,&width,&height));
                Expect(width==(scale?960u:640u)&&height==(scale?720u:360u),"aspect-preserving recording size");
                unsigned frames=0;
                for(;;){ComPtr<IMFSample> sample;DWORD flags=0;Check(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,nullptr,&flags,nullptr,&sample));
                    if(sample)++frames;if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;}
                Expect(frames==(pause?159u:180u),"no video loss outside intentional paused interval");
            }
            if(limited){
                ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
                ComPtr<IMFMediaType> media;Check(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),0,&media));
                UINT32 num=0,den=0;Check(MFGetAttributeRatio(media.Get(),MF_MT_FRAME_RATE,&num,&den));
                Expect(num==30*den,"output frame rate is capped at 30, not capture rate");
            }
            auto vp=Pulses(path.c_str(),true),ap=Pulses(path.c_str(),false);
            Expect(vp.size()==3&&ap.size()==3,"three decoded synchronized flash/tone events");
            for(size_t i=0;i<(std::min)(vp.size(),ap.size());++i){
                if(pause)Expect(std::abs(vp[i]-(1.0+i*2-(i?0.7:0)))<0.04,"paused time removed from saved timeline");
                printf("event %zu video=%.6fs audio=%.6fs skew=%.3fms\n",i,vp[i],ap[i],(ap[i]-vp[i])*1000);
                Expect(std::abs(ap[i]-vp[i])<0.040,"decoded A/V skew below 40ms");
                if(arrival && flashWall.size()==3) {
                    const double expected=audioWall-0.010+(1.0+i*2)-(flashWall[i]-1.0/v.fps);
                    printf("arrival scheduler skew=%.3fms, encoding residual=%.3fms\n",expected*1000,(ap[i]-vp[i]-expected)*1000);
                    Expect(std::abs(ap[i]-vp[i]-expected)<0.005,"arrival clock preserves measured source timing within 5ms");
                }
            }
            if(!arrival&&vp.size()==3&&ap.size()==3) Expect(std::abs((ap[2]-vp[2])-(ap[0]-vp[0]))<0.005,"no growing sync drift");
        }
        recorder.Configure(v,a,{});Expect(recorder.Start(path.wstring()),"existing file async start");
        while(recorder.Busy()) Sleep(10);
        Expect(FAILED(recorder.Result()),"existing file must not be overwritten");
        v.format=llcv::settings::VideoPixelFormat::P010;recorder.Configure(v,a,{});
        Expect(!recorder.Start(path.wstring()+L".hdr.mp4"),"HDR rejected");
        v.format=llcv::settings::VideoPixelFormat::Nv12;a.channels=6;recorder.Configure(v,a,{});
        Expect(!recorder.Start(path.wstring()+L".51.mp4"),"unsupported audio rejected");
        printf("test recording: %ls\n",path.c_str());
    } catch(HRESULT hr){printf("HRESULT 0x%08lX\n",static_cast<unsigned long>(hr));++failures;}
finished:
    MFShutdown();CoUninitialize();return failures?1:0;
}
