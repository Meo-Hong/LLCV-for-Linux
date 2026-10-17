// Opt-in benchmark: identical deterministic SDR scenes through the real Recorder.
// No capture device or user profile is touched. PSNR is not a perceptual rating.
#include "recording/Recorder.h"
#include "recording/EncoderSupport.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <vector>
using Microsoft::WRL::ComPtr;
using llcv::recording::Options;
using llcv::recording::Codec;
using llcv::recording::RateControl;
using llcv::recording::Recorder;
constexpr int width=2560,height=1440,fps=60,frames=360;
static bool stress=false;
static void Check(HRESULT hr){if(FAILED(hr))throw hr;}
static BYTE Luma(int x,int y,int frame){
    const int scene=frame/120;
    const int shift=scene?frame*4:0;
    const unsigned sx=static_cast<unsigned>(x+shift),sy=static_cast<unsigned>(y);
    unsigned hash=(sx/(stress?2:8))*1664525u+(sy/(stress?2:8))*1013904223u;
    hash^=hash>>13;hash*=2246822519u;
    int value=48+static_cast<int>((sx/16+sy/12)%112)+(static_cast<int>(hash%(stress?25:13))-(stress?12:6));
    if((sx%320)<4||(sy%180)<4)value=220;
    if((x-frame*9+width*4)%width<180 && y>height/3&&y<height*2/3)value=190+((x+y)%8<4?16:-16);
    if(scene==2&&((sx/80+sy/80)%3)==0)value=stress?32+static_cast<int>((hash+static_cast<unsigned>(frame/2)*37)%192):value+static_cast<int>((hash+static_cast<unsigned>(frame/2)*37)%17)-8;
    return static_cast<BYTE>(value);
}
static void Fill(BYTE* data,int frame){
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)data[y*width+x]=Luma(x,y,frame);
    for(int y=0;y<height/2;++y)for(int x=0;x<width;x+=2){
        data[width*height+y*width+x]=static_cast<BYTE>(100+(x/64)%56);
        data[width*height+y*width+x+1]=static_cast<BYTE>(100+(y/32)%56);
    }
}
static double Measure(const std::wstring& path){
    ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
    const DWORD stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    Check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE));Check(reader->SetStreamSelection(stream,TRUE));
    ComPtr<IMFMediaType> type;Check(MFCreateMediaType(&type));type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12);
    Check(reader->SetCurrentMediaType(stream,nullptr,type.Get()));
    uint64_t square=0,pixels=0;int count=0;LONGLONG last=-1;
    for(;;){
        DWORD flags=0;LONGLONG time=0;ComPtr<IMFSample> sample;Check(reader->ReadSample(stream,0,nullptr,&flags,&time,&sample));
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;if(!sample)continue;
        const int frame=static_cast<int>((time*fps+5000000)/10000000);
        if(frame!=count||time<=last)throw MF_E_INVALID_TIMESTAMP;last=time;
        ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer));BYTE* data=nullptr;DWORD length=0;
        Check(buffer->Lock(&data,nullptr,&length));
        if(length<width*height*3/2){buffer->Unlock();throw E_FAIL;}
        // Decoder output is contiguous NV12 for this aligned 2560-wide source.
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int error=int(data[y*width+x])-Luma(x,y,frame);square+=error*error;}
        pixels+=uint64_t(width)*height;buffer->Unlock();++count;
    }
    if(count!=frames)throw MF_E_INVALID_STREAM_DATA;
    return 10*log10(255.0*255.0/(double(square)/pixels));
}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;
    stress=argc>2&&std::wstring(argv[2])==L"--stress";
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));Check(MFStartup(MF_VERSION));
    int result=0;
    try{
        const std::filesystem::path folder(argv[1]);std::filesystem::create_directories(folder);
        if(std::filesystem::exists(folder/L"quality.csv"))throw HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
        std::ofstream report(folder/L"quality.csv");report<<"codec,mode,qp,bytes,luma_psnr,push_mean_us,push_p95_us,push_max_us,finalize_ms\n";
        for(auto codec:{Codec::H264,Codec::Hevc})for(int preset=-1;preset<3;++preset){
            Options options;options.codec=codec;
            // This benchmark compares native QHD frames, independently of the
            // app's user-facing 1080p/60 default recording caps.
            options.maxHeight=0;options.maxFps=0;
            options.rateControl=preset<0?RateControl::Bitrate:RateControl::Quality;
            options.quality=preset<0?llcv::recording::Quality::Balanced:static_cast<llcv::recording::Quality>(preset);
            const auto name=std::wstring(codec==Codec::H264?L"h264":L"hevc")+(preset<0?L"-20Mbps":L"-qp"+std::to_wstring(QualityQp(options.quality)));
            const auto path=(folder/(name+L".mp4")).wstring();
            Recorder recorder;llcv::video::VideoSampleFormat video{};
            video.valid=true;video.width=video.stride=width;video.height=height;video.fps=fps;video.format=llcv::settings::VideoPixelFormat::Nv12;
            llcv::capture_audio::Format audio{};audio.channels=2;audio.containerBits=audio.validBits=16;audio.blockAlign=4;audio.path=llcv::capture_audio::Path::Direct16BitStereo;
            recorder.Configure(video,audio,{},false,options);if(!recorder.Start(path))throw E_FAIL;
            for(int i=0;i<1000&&recorder.Busy()&&!recorder.Recording();++i)Sleep(10);
            if(!recorder.Recording()){Check(recorder.Result());throw E_FAIL;}
            ComPtr<IMemAllocator> allocator;Check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator)));
            ALLOCATOR_PROPERTIES request{2,width*height*3/2,1,0},actual{};Check(allocator->SetProperties(&request,&actual));Check(allocator->Commit());
            const auto start=std::chrono::steady_clock::now();std::vector<double> push;
            for(int frame=0;frame<frames&&recorder.Recording();++frame){
                ComPtr<IMediaSample> vs,as;Check(allocator->GetBuffer(&vs,nullptr,nullptr,0));Check(allocator->GetBuffer(&as,nullptr,nullptr,0));
                BYTE *v=nullptr,*a=nullptr;Check(vs->GetPointer(&v));Check(as->GetPointer(&a));Fill(v,frame);
                auto pcm=reinterpret_cast<short*>(a);
                for(int i=0;i<800;++i)pcm[i*2]=pcm[i*2+1]=static_cast<short>(2000*sin((frame*800+i)*6.28318530718*440/48000));
                LONGLONG time=frame*10000000LL/fps,end=(frame+1)*10000000LL/fps;
                vs->SetTime(&time,&end);as->SetTime(&time,&end);vs->SetActualDataLength(width*height*3/2);as->SetActualDataLength(3200);
                std::this_thread::sleep_until(start+std::chrono::nanoseconds(frame*1000000000LL/fps));
                const auto t=std::chrono::steady_clock::now();recorder.Push(true,vs.Get());
                push.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count());
                recorder.Push(false,as.Get());
            }
            const auto end=std::chrono::steady_clock::now();recorder.Stop();recorder.Shutdown();Check(recorder.Result());
            const double finish=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-end).count();
            if(push.size()!=frames)throw E_FAIL;
            double mean=0;for(auto us:push)mean+=us;mean/=push.size();std::sort(push.begin(),push.end());
            const auto bytes=std::filesystem::file_size(path);const double psnr=Measure(path);
            printf("%ls bytes=%llu PSNR=%.3f push mean/p95/max=%.1f/%.1f/%.1f us finish=%.1fms\n",name.c_str(),bytes,psnr,mean,push[push.size()*95/100],push.back(),finish);fflush(stdout);
            report<<(codec==Codec::H264?"h264":"hevc")<<','<<(preset<0?"bitrate":"cqp")<<','<<(preset<0?0:QualityQp(options.quality))<<','<<bytes<<','<<psnr<<','<<mean<<','<<push[push.size()*95/100]<<','<<push.back()<<','<<finish<<'\n';report.flush();
        }
    }catch(HRESULT hr){printf("FAILED %08lX\n",hr);result=1;}
    catch(...){puts("FAILED exception");result=1;}
    MFShutdown();CoUninitialize();return result;
}
