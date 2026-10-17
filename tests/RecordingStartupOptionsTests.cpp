#include "recording/Recorder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using Microsoft::WRL::ComPtr;
namespace {
int failures=0;
void Expect(bool ok,const char* message) {
    if(!ok){++failures;std::printf("FAIL: %s\n",message);}
}
void Check(HRESULT hr){if(FAILED(hr))throw hr;}
ComPtr<IMediaSample> Sample(IMemAllocator* allocator,long bytes,LONGLONG time,LONGLONG duration) {
    ComPtr<IMediaSample> sample;Check(allocator->GetBuffer(&sample,nullptr,nullptr,0));
    Check(sample->SetActualDataLength(bytes));
    auto end=time+duration;Check(sample->SetTime(&time,&end));return sample;
}
void Verify(const std::filesystem::path& path) {
    ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
    const auto video=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    const auto audio=static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFMediaType> native;Check(reader->GetNativeMediaType(video,0,&native));
    UINT32 width=0,height=0,rate=0,denominator=0;
    Check(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&width,&height));
    Check(MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&rate,&denominator));
    Expect(width==960 && height==720,"Start uses newly accepted resolution cap, not Configure defaults");
    Expect(denominator && double(rate)/denominator==30,"Start uses newly accepted frame cap");
    Check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE));
    Check(reader->SetStreamSelection(audio,TRUE));
    ComPtr<IMFMediaType> pcm;Check(MFCreateMediaType(&pcm));
    Check(pcm->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
    Check(pcm->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
    Check(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
    Check(reader->SetCurrentMediaType(audio,nullptr,pcm.Get()));
    uint64_t samples=0,energy=0;
    for(;;){
        DWORD flags=0;ComPtr<IMFSample> sample;
        Check(reader->ReadSample(audio,0,nullptr,&flags,nullptr,&sample));
        if(sample){
            ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer));
            BYTE* data=nullptr;DWORD length=0;Check(buffer->Lock(&data,nullptr,&length));
            const auto* values=reinterpret_cast<const int16_t*>(data);
            for(DWORD i=0;i<length/sizeof(int16_t);++i){
                energy+=static_cast<uint64_t>(std::abs(static_cast<int>(values[i])));++samples;
            }
            Check(buffer->Unlock());
        }
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
    }
    Expect(samples>=48000,"the newly configured recorder retains the audio stream");
    Expect(samples && double(energy)/samples<1.0,"Start uses newly accepted recording mute");
}
void Run(const std::filesystem::path& path) {
    using namespace llcv;
    constexpr long width=1280,height=960,frameBytes=width*height*3/2;
    video::VideoSampleFormat video{};video.valid=true;video.width=width;video.height=height;
    video.fps=60;video.stride=width;video.format=settings::VideoPixelFormat::Nv12;
    capture_audio::Format audio{};audio.channels=2;audio.containerBits=audio.validBits=16;
    audio.blockAlign=4;audio.path=capture_audio::Path::Direct16BitStereo;
    recording::Recorder recorder;
    // The app's capture worker initializes input only, without touching UI-owned
    // sessionOptions. Preferences may be changed until the first Start.
    recorder.Configure(video,audio,{},false,recording::Options{});
    recording::Options edited;
    edited.encoder=L"cpu";edited.rateControl=recording::RateControl::Bitrate;
    edited.maxHeight=720;edited.maxFps=30;edited.audioVolumePercent=0;
    edited.folder=path.parent_path().wstring();
    Expect(recorder.Start(path.wstring(),&edited),"first recording accepts options edited after Configure");
    // Start must take a value snapshot before returning, not retain this address.
    edited.encoder=L"invalid-after-start";edited.maxHeight=0;edited.maxFps=0;
    edited.audioVolumePercent=100;edited.folder.assign(8192,L'x');
    const auto deadline=GetTickCount64()+10000;
    while(recorder.Busy()&&!recorder.Recording()&&GetTickCount64()<deadline)Sleep(1);
    Expect(recorder.Recording(),"CPU recording becomes ready with the copied options");
    if(!recorder.Recording()){recorder.Shutdown();return;}
    ComPtr<IMemAllocator> allocator;
    Check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator)));
    ALLOCATOR_PROPERTIES requested{2,frameBytes,1,0},actual{};
    Check(allocator->SetProperties(&requested,&actual));Check(allocator->Commit());
    const auto began=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<36 && recorder.Recording();++frame){
        std::this_thread::sleep_until(began+std::chrono::nanoseconds(frame*1000000000LL/60));
        const auto time=frame*10000000LL/60;
        auto picture=Sample(allocator.Get(),frameBytes,time,10000000/60);
        BYTE* data=nullptr;Check(picture->GetPointer(&data));
        std::memset(data,static_cast<int>(32+frame),width*height);
        std::memset(data+width*height,128,width*height/2);recorder.Push(true,picture.Get());
        // 800 stereo PCM frames per 60 Hz interval, with a clearly nonzero tone.
        auto sound=Sample(allocator.Get(),800*4,time,10000000/60);
        Check(sound->GetPointer(&data));auto* values=reinterpret_cast<int16_t*>(data);
        for(unsigned i=0;i<800;++i){
            const auto value=static_cast<int16_t>(12000*std::sin((frame*800+i)*2*3.141592653589793*440/48000));
            values[i*2]=values[i*2+1]=value;
        }
        recorder.Push(false,sound.Get());
    }
    recorder.Stop();recorder.Shutdown();Check(allocator->Decommit());
    Expect(SUCCEEDED(recorder.Result()),"copied start preferences remain valid after caller mutation");
    if(SUCCEEDED(recorder.Result()))Verify(path);
}
}
int main() {
    const auto path=std::filesystem::temp_directory_path()/(L"LLCV-startup-options-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".mp4");
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com))return 1;
    const HRESULT mf=MFStartup(MF_VERSION);
    if(FAILED(mf)){CoUninitialize();return 1;}
    try{Run(path);}catch(HRESULT hr){++failures;std::printf("FAIL: 0x%08lX\n",static_cast<unsigned long>(hr));}
    catch(...){++failures;std::puts("FAIL: unexpected startup options exception");}
    // Only the exact unique recording created by this fixture is removed.
    std::error_code error;std::filesystem::remove(path,error);
    MFShutdown();CoUninitialize();
    if(!failures)std::puts("Recording startup: latest UI settings and independent Start snapshot passed.");
    return failures?1:0;
}
