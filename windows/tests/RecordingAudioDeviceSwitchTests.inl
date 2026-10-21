// Hardware-free integration: production selection helper, capture callback,
// playback fill, recorder lifecycle, AAC encoding and decoded file contents.
// Synthetic A/B replace driver enumeration/binding. This does NOT exercise
// a physical device, COM moniker binding or the full settings/relaunch window flow.
#ifdef LLCV_RECORDING
#include <filesystem>
#include <mfreadwrite.h>

namespace recording_audio_switch_test {
using Microsoft::WRL::ComPtr;
using llcv::capture_audio::Format;
using llcv::capture_audio::Path;
using llcv::capture_audio::Encoding;
namespace rec=llcv::recording;
static void Check(bool ok,const char* label) {
    if(!ok){std::fprintf(stderr,"FAILED recording input regression: %s\n",label);throw E_FAIL;}
}
static void Hr(HRESULT hr){if(FAILED(hr))throw hr;}
static Format Pcm(unsigned channels=2,unsigned bits=16,bool floating=false) {
    return {floating?Encoding::FloatPcm:Encoding::IntegerPcm,
        channels==2&&bits==16&&!floating?Path::Direct16BitStereo:Path::ConvertTo16BitStereo,
        static_cast<uint16_t>(bits),static_cast<uint16_t>(bits),static_cast<uint16_t>(channels),
        static_cast<uint16_t>(channels*bits/8),0};
}
static llcv::video::VideoSampleFormat Video() {
    llcv::video::VideoSampleFormat v{};v.valid=true;v.width=320;v.height=180;
    v.stride=320;v.fps=30;v.format=VideoPixelFormat::Nv12;return v;
}
static rec::Options Options(int percent=100) {
    rec::Options o;o.encoder=L"cpu";o.rateControl=rec::RateControl::Bitrate;
    o.audioVolumePercent=percent;return o;
}
static void WaitStarted() {
    const auto until=GetTickCount64()+12000;
    while(rec::recorder.Busy()&&!rec::recorder.Recording()&&GetTickCount64()<until)Sleep(1);
    Check(rec::recorder.Recording(),"encoder becomes ready within deadline");
}
static rec::Snapshot Finish() {
    rec::recorder.Stop();const auto until=GetTickCount64()+12000;
    while(rec::recorder.Busy()&&GetTickCount64()<until)Sleep(1);
    Check(!rec::recorder.Busy(),"recording finalizes within deadline");
    return rec::recorder.GetSnapshot();
}
struct Fixture {
    ComPtr<IMemAllocator> allocator;
    std::vector<std::filesystem::path> files;
    const AppSettings savedSettings=g_settings;
    Fixture() {
        Hr(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator)));
        ALLOCATOR_PROPERTIES requested{4,320*180*3/2,1,0},actual{};
        Hr(allocator->SetProperties(&requested,&actual));Hr(allocator->Commit());
        g_settings.driftCorrection=DriftCorrectionMode::Off;
        g_settings.consoleSurround51=false;
        g_audioTrackingStartMs=UINT64_MAX;
        g_volumePercent=100;g_leftVolumePercent=100;g_rightVolumePercent=100;
        g_playbackMuted=false;g_backgroundAudioMuted=false;
        g_ring.ConfigureChannels(2);g_ring.Clear();
    }
    ~Fixture() {
        rec::recorder.Shutdown();g_ring.Clear();g_settings=savedSettings;
        if(allocator)allocator->Decommit();
        // Only exact test-owned paths, never recursive directory deletion.
        for(const auto& p:files){std::error_code ec;std::filesystem::remove(p,ec);}
    }
    std::filesystem::path file(const wchar_t* label) {
        auto p=std::filesystem::temp_directory_path()/(std::wstring(L"LLCV-input-switch-")+label+L"-"+
            std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+
            std::to_wstring(files.size())+L".mp4");files.push_back(p);return p;
    }
    ComPtr<IMediaSample> sample(long bytes,LONGLONG time,bool timestamp=true) {
        ComPtr<IMediaSample> s;Hr(allocator->GetBuffer(&s,nullptr,nullptr,0));Hr(s->SetActualDataLength(bytes));
        if(timestamp){LONGLONG end=time+333333;Hr(s->SetTime(&time,&end));}
        else Hr(s->SetTime(nullptr,nullptr));
        return s;
    }
    ComPtr<IMediaSample> sound(const Format& f,size_t frames,uint64_t cursor,double hz,LONGLONG clockOffset,bool timestamp=true) {
        auto s=sample(static_cast<long>(frames*f.blockAlign),clockOffset+static_cast<LONGLONG>(cursor)*10000000/48000,timestamp);
        BYTE* data=nullptr;Hr(s->GetPointer(&data));
        for(size_t n=0;n<frames;++n)for(unsigned c=0;c<f.channels;++c) {
            const double x=hz==0?0:std::sin((cursor+n)*2*3.141592653589793*hz*(c+1)/48000)*12000/32768;
            BYTE* p=data+n*f.blockAlign+c*f.containerBits/8;
            if(f.encoding==Encoding::FloatPcm){const float v=static_cast<float>(x);std::memcpy(p,&v,4);}
            else {
                const auto scale=uint64_t{1}<<(f.containerBits-1);
                const int64_t value=static_cast<int64_t>(x*scale);
                for(unsigned b=0;b<static_cast<unsigned>(f.containerBits)/8;++b)p[b]=static_cast<BYTE>(static_cast<uint64_t>(value)>>(b*8));
            }
        }
        return s;
    }
    void picture(LONGLONG time) {
        auto s=sample(320*180*3/2,time);BYTE* p=nullptr;Hr(s->GetPointer(&p));
        std::memset(p,128,320*180*3/2);rec::recorder.Push(true,s.Get());
    }
};
struct Decoded {std::vector<int16_t> pcm;unsigned channels=0,rate=0;};
static Decoded Decode(const std::filesystem::path& path) {
    ComPtr<IMFSourceReader> reader;Hr(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
    const DWORD audio=static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    Hr(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE));Hr(reader->SetStreamSelection(audio,TRUE));
    ComPtr<IMFMediaType> t;Hr(MFCreateMediaType(&t));Hr(t->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
    Hr(t->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));Hr(t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
    Hr(reader->SetCurrentMediaType(audio,nullptr,t.Get()));Hr(reader->GetCurrentMediaType(audio,&t));
    Decoded result;Hr(t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&result.channels));Hr(t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&result.rate));
    Check(result.channels==2&&result.rate==48000,"saved AAC decodes as stereo 48kHz");bool end=false;
    for(unsigned i=0;i<2000&&!end;++i) {
        DWORD flags=0;ComPtr<IMFSample> s;Hr(reader->ReadSample(audio,0,nullptr,&flags,nullptr,&s));
        Check(!(flags&MF_SOURCE_READERF_ERROR),"AAC decoder reports no error");
        if(s){ComPtr<IMFMediaBuffer> b;Hr(s->ConvertToContiguousBuffer(&b));BYTE* p=nullptr;DWORD bytes=0;
            Hr(b->Lock(&p,nullptr,&bytes));const auto* pcm=reinterpret_cast<int16_t*>(p);
            result.pcm.insert(result.pcm.end(),pcm,pcm+bytes/2);Hr(b->Unlock());}
        end=(flags&MF_SOURCE_READERF_ENDOFSTREAM)!=0;
    }
    Check(end,"bounded decode reaches end of recording");return result;
}
static double Tone(const Decoded& d,unsigned channel,double hz) {
    // Avoid AAC priming/end padding, use both quadratures (phase-independent).
    const size_t first=12000,end=(std::min)(size_t{60000},d.pcm.size()/2);double a=0,b=0;
    Check(end>first+20000,"recorded audio has a substantive middle region");
    for(size_t n=first;n<end;++n){double phase=2*3.141592653589793*hz*n/48000;
        a+=d.pcm[n*2+channel]*std::cos(phase);b+=d.pcm[n*2+channel]*std::sin(phase);}
    return 2*std::sqrt(a*a+b*b)/(end-first);
}
static void CheckRecorded(const std::filesystem::path& path,double hz,int percent,double oldHz=0) {
    const auto d=Decode(path);Check(d.pcm.size()/2>=60000,"recorded file contains at least 1.25s of audio");
    if(hz==0||percent==0){Check(std::all_of(d.pcm.begin(),d.pcm.end(),[](int16_t s){return s==0;}),"requested/input silence decodes to zero");return;}
    for(unsigned c=0;c<2;++c) {
        const double amplitude=Tone(d,c,hz*(c+1));
        std::printf("recorded channel %u frequency %.0fHz amplitude %.2f, gain %d%%\n",c,hz*(c+1),amplitude,percent);
        Check(amplitude>10000*percent/100.0&&amplitude<14000*percent/100.0,"new selected source survives AAC with expected gain");
        if(oldHz&&oldHz!=hz)Check(amplitude>10*Tone(d,c,oldHz*(c+1)),"old input frequency does not leak after source replacement");
    }
}
static std::filesystem::path Record(Fixture& f,ISampleGrabberCB* callback,const wchar_t* label,double hz,
                                   int percent=100,size_t packetFrames=1600,LONGLONG clockOffset=0,bool timestamp=true,bool muted=false) {
    auto path=f.file(label);auto options=Options(percent);
    // Same common arrival-clock mode as UnifiedCaptureRenderLoop. Recording
    // ignores differing driver timestamp epochs after an input device change.
    rec::recorder.Configure(Video(),Pcm(),{},true,options);
    Check(rec::recorder.Start(path.wstring(),&options),"new recording session starts");WaitStarted();
    g_ring.Clear();g_playbackMuted=muted;g_backgroundAudioMuted=false;
    WasapiRenderState playback;playback.queueTargetFrames=static_cast<UINT32>(packetFrames);playback.currentMix=TargetStereoGain();
    std::vector<int16_t> output(packetFrames*2);const auto begin=std::chrono::steady_clock::now();
    size_t cursor=0;
    while(cursor<72000) {
        std::this_thread::sleep_until(begin+std::chrono::nanoseconds(cursor*1000000000LL/48000));
        f.picture(static_cast<LONGLONG>(cursor)*10000000/48000);
        auto s=f.sound(Pcm(),packetFrames,cursor,hz,clockOffset,timestamp);BYTE* p=nullptr;Hr(s->GetPointer(&p));
        const std::vector<BYTE> original(p,p+packetFrames*4);Hr(callback->SampleCB(0,s.Get()));
        const auto fill=FillWasapiPcm(&playback,output.data(),packetFrames);
        Check(fill.writtenFrames==packetFrames,"playback consumes complete selected-input packets");
        if(hz&&!muted)Check(std::any_of(output.begin(),output.end(),[](int16_t v){return std::abs(int(v))>5000;}),"selected source is audible in actual playback fill");
        else Check(std::all_of(output.begin(),output.end(),[](int16_t v){return v==0;}),"playback mute/input silence stays zero");
        Check(std::equal(original.begin(),original.end(),p),"playback/recording never modify driver-owned source PCM");
        cursor+=packetFrames;
    }
    const auto status=Finish();Check(SUCCEEDED(status.result)&&SUCCEEDED(status.finalizeResult)&&status.playable,"recording finishes with verified video and audio tracks");
    CheckRecorded(path,hz,percent);g_playbackMuted=false;return path;
}
static void Switches(Fixture& f) {
    SettingsDialogState selection{};
    selection.captureAudioDevices={{L"synthetic-a",L"Synthetic A"},{L"synthetic-hdmi-b",L"Synthetic HDMI B"}};
    HWND combo=CreateWindowExW(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST,0,0,200,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(combo!=nullptr,"create hidden audio device selector");
    struct Window {HWND h;~Window(){DestroyWindow(h);}} window{combo};selection.captureAudioDeviceCombo=combo;
    for(auto* text:{L"Auto",L"Synthetic A",L"Synthetic HDMI B"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    const auto choose=[&](int index,const wchar_t* expected) {
        SendMessageW(combo,CB_SETCURSEL,index,0);g_settings.captureAudioDeviceId=SelectedCaptureAudioDeviceId(&selection);
        Check(g_settings.captureAudioDeviceId==expected,"production selector returns requested identity, not stale/default index");
    };
    ComPtr<ISampleGrabberCB> callback;
    const auto replace=[&]{rec::recorder.Shutdown();callback.Reset();g_ring.Clear();callback.Attach(CreateAudioSampleCallback(Pcm()));};
    choose(0,L"");replace();Record(f,callback.Get(),L"auto-silent",0);
    choose(2,L"synthetic-hdmi-b");replace();auto b=Record(f,callback.Get(),L"explicit-B",880,100,1600,120*10000000LL);
    CheckRecorded(b,880,100,440);
    choose(1,L"synthetic-a");replace();auto a=Record(f,callback.Get(),L"explicit-A",440,100,1600,0,false);
    CheckRecorded(a,440,100,880);
    choose(2,L"synthetic-hdmi-b");replace();Record(f,callback.Get(),L"B-playback-muted",880,100,1600,987*10000000LL,true,true);
    // Keep the same input callback across stop/start: only the recorder changes.
    Record(f,callback.Get(),L"B-recording-muted",880,0);
    Record(f,callback.Get(),L"B-recording-gain-restored",880,50);
    std::puts("Device switch: auto(silence) -> HDMI B -> A -> B; missing/different driver timestamps; repeated recording; playback/recording mute isolation passed.");
}
static void Boundaries(Fixture& f) {
    for(const Format format:{Pcm(1),Pcm(2,24),Pcm(2,32),Pcm(2,32,true)}) {
        rec::recorder.Shutdown();g_ring.Clear();g_captureAudioRejected=false;
        ComPtr<ISampleGrabberCB> cb;cb.Attach(CreateAudioSampleCallback(format));
        auto options=Options();rec::recorder.Configure(Video(),format,{},true,options);
        const auto path=f.file(L"unsupported-input");Check(!rec::recorder.Start(path.wstring()),"non-PCM16-stereo recording is explicitly rejected");
        Check(rec::recorder.GetSnapshot().failure==rec::Failure::InvalidInput,"unsupported format is input error, not successful silent recording");
        auto s=f.sound(format,1600,0,440,0);Hr(cb->SampleCB(0,s.Get()));
        WasapiRenderState playback;playback.queueTargetFrames=1600;playback.currentMix=TargetStereoGain();
        std::array<int16_t,3200> output{};auto fill=FillWasapiPcm(&playback,output.data(),1600);
        Check(fill.writtenFrames==1600&&std::any_of(output.begin(),output.end(),[](int16_t v){return std::abs(int(v))>5000;}),"playback converts unsupported recording format successfully");
        Check(!std::filesystem::exists(path),"rejected format does not create a misleading silent file");
        std::printf("format %uch %ubit float=%u: playback audible / recording rejected as expected\n",format.channels,format.containerBits,format.encoding==Encoding::FloatPcm);
    }
    rec::recorder.Shutdown();g_ring.Clear();ComPtr<ISampleGrabberCB> cb;cb.Attach(CreateAudioSampleCallback(Pcm()));
    Record(f,cb.Get(),L"packet-100ms",440,100,4800);
    Record(f,cb.Get(),L"packet-101ms",440,100,4848);
    Record(f,cb.Get(),L"packet-250ms",440,100,12000);
    Record(f,cb.Get(),L"after-large-packet",880);
    std::puts("Boundaries: mono/24bit/32bit/float playback-only; 100/101/250ms packets audible and recorded; restart passed.");
}
static int Run(bool boundaries) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 1;
    const HRESULT mf=MFStartup(MF_VERSION);if(FAILED(mf)){CoUninitialize();return 1;}int result=0;
    try{Fixture f;if(boundaries)Boundaries(f);else Switches(f);}
    catch(HRESULT hr){std::printf("Recording input test HRESULT=0x%08lX\n",static_cast<unsigned long>(hr));result=1;}
    catch(...){std::puts("Recording input test unexpected exception");result=1;}
    MFShutdown();CoUninitialize();return result;
}
}
#endif
