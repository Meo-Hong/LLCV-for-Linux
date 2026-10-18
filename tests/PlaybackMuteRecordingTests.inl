// Real capture callback -> recorder -> AAC decode, not a simulated mute flag.
#ifdef LLCV_RECORDING
#include <mfreadwrite.h>
#include <filesystem>
static int TestPlaybackMuteRecording() {
    using Microsoft::WRL::ComPtr;
    const auto path=std::filesystem::temp_directory_path()/(L"LLCV-playback-mute-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".mp4");
    const auto check=[](HRESULT hr) { if (FAILED(hr)) throw hr; };
    int result=0;
    check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
    check(MFStartup(MF_VERSION));
    try {
        constexpr long bytes=640*360*3/2;
        llcv::video::VideoSampleFormat video{};
        video.valid=true;video.width=640;video.height=360;video.stride=640;
        video.fps=30;video.format=VideoPixelFormat::Nv12;
        llcv::capture_audio::Format audio{};
        audio.channels=2;audio.containerBits=audio.validBits=16;audio.blockAlign=4;
        audio.path=llcv::capture_audio::Path::Direct16BitStereo;
        llcv::recording::Options options;
        options.encoder=L"cpu";options.rateControl=llcv::recording::RateControl::Bitrate;
        options.audioVolumePercent=50;
        llcv::recording::recorder.Configure(video,audio,{},false,options);
        Require(llcv::recording::recorder.Start(path.wstring()),"mute-isolation recording starts");
        const auto deadline=GetTickCount64()+10000;
        while(llcv::recording::recorder.Busy() && !llcv::recording::recorder.Recording() &&
              GetTickCount64()<deadline) Sleep(1);
        Require(llcv::recording::recorder.Recording(),"mute-isolation encoder ready");
        ComPtr<IMemAllocator> allocator;
        check(CoCreateInstance(CLSID_MemoryAllocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&allocator)));
        ALLOCATOR_PROPERTIES request{2,bytes,1,0},actual{};
        check(allocator->SetProperties(&request,&actual));check(allocator->Commit());
        ComPtr<ISampleGrabberCB> callback;callback.Attach(CreateAudioSampleCallback(audio));
        g_settings.driftCorrection=DriftCorrectionMode::Off;
        g_audioTrackingStartMs=UINT64_MAX;g_volumePercent=25;
        g_leftVolumePercent=100;g_rightVolumePercent=100;
        g_ring.Clear();g_playbackMuted=false;g_backgroundAudioMuted=false;
        WasapiRenderState playback;playback.queueTargetFrames=1600;
        playback.currentMix=TargetStereoGain();
        std::array<int16_t,3200> output{};
        const auto began=std::chrono::steady_clock::now();
        for (unsigned frame=0;frame<90;++frame) {
            std::this_thread::sleep_until(began+std::chrono::nanoseconds(frame*1000000000LL/30));
            if(frame==30) SetPlaybackMuted(nullptr,true);
            if(frame==60) SetPlaybackMuted(nullptr,false);
            const auto sample=[&](long length) {
                ComPtr<IMediaSample> s;check(allocator->GetBuffer(&s,nullptr,nullptr,0));
                check(s->SetActualDataLength(length));
                LONGLONG start=frame*10000000LL/30,end=(frame+1)*10000000LL/30;
                check(s->SetTime(&start,&end));return s;
            };
            auto picture=sample(bytes);
            BYTE* data=nullptr;check(picture->GetPointer(&data));std::memset(data,128,bytes);
            llcv::recording::recorder.Push(true,picture.Get());
            auto sound=sample(1600*4);
            check(sound->GetPointer(&data));auto* pcm=reinterpret_cast<int16_t*>(data);
            for (unsigned i=0;i<1600;++i)
                pcm[i*2]=pcm[i*2+1]=static_cast<int16_t>(
                    12000*std::sin((frame*1600+i)*2*3.141592653589793*440/48000));
            const std::vector<int16_t> original(pcm,pcm+3200);
            check(callback->SampleCB(frame/30.0,sound.Get()));
            Require(FillWasapiPcm(&playback,output.data(),1600).writtenFrames==1600,
                    "playback consumes every capture packet during recording and mute");
            if(frame>30 && frame<60)
                Require(std::all_of(output.begin(),output.end(),[](int16_t v){return v==0;}),
                        "actual playback samples are silent during recording");
            Require(std::equal(original.begin(),original.end(),pcm),
                    "playback does not modify capture samples owned by recording");
        }
        llcv::recording::recorder.Stop();
        const auto stopDeadline=GetTickCount64()+10000;
        while(llcv::recording::recorder.Busy() && GetTickCount64()<stopDeadline) Sleep(1);
        Require(!llcv::recording::recorder.Busy(),"mute-isolation recording finalizes within deadline");
        llcv::recording::recorder.Shutdown();check(llcv::recording::recorder.Result());
        check(allocator->Decommit());
        ComPtr<IMFSourceReader> reader;check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
        const auto stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),FALSE));
        check(reader->SetStreamSelection(stream,TRUE));
        ComPtr<IMFMediaType> type;check(MFCreateMediaType(&type));
        check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));
        check(type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));
        check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));
        check(reader->SetCurrentMediaType(stream,nullptr,type.Get()));
        uint64_t cursor=0;std::array<double,3> energy{};std::array<unsigned,3> count{};
        bool ended=false;
        for (unsigned packet=0;packet<1000 && !ended;++packet) {
            DWORD flags=0;ComPtr<IMFSample> sample;
            check(reader->ReadSample(stream,0,nullptr,&flags,nullptr,&sample));
            if(sample) {
                ComPtr<IMFMediaBuffer> buffer;check(sample->ConvertToContiguousBuffer(&buffer));
                BYTE* data=nullptr;DWORD length=0;check(buffer->Lock(&data,nullptr,&length));
                const auto* pcm=reinterpret_cast<const int16_t*>(data);
                for(DWORD i=0;i<length/4;++i,++cursor) {
                    const auto second=cursor/48000,within=cursor%48000;
                    if(second<3 && within>9600 && within<38400) {
                        energy[second]+=std::abs(static_cast<int>(pcm[2*i]));++count[second];
                    }
                }
                check(buffer->Unlock());
            }
            ended=(flags&MF_SOURCE_READERF_ENDOFSTREAM)!=0;
        }
        Require(ended && cursor>=140000,"complete recorded audio with bounded decode loop");
        for(size_t i=0;i<3;++i) {
            const double mean=count[i] ? energy[i]/count[i] : 0;
            std::printf("Recorded audio, second %zu (playback %s): mean absolute PCM %.2f\n",
                i,i==1?"muted":"audible",mean);
            Require(count[i]>20000 && mean>3400 && mean<4200,
                    "recording retains its own 50-percent gain before/during/after playback mute");
        }
    } catch(HRESULT hr) {
        std::printf("Playback mute recording failure: %08lX\n",static_cast<unsigned long>(hr));result=1;
    }
    llcv::recording::recorder.Shutdown();
    std::error_code error;std::filesystem::remove(path,error); // exact test-owned recording only
    MFShutdown();CoUninitialize();
    return result;
}
#endif
