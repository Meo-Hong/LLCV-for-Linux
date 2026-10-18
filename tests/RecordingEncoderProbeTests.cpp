#include "recording/EncoderProbe.h"
#include "recording/EncoderSupport.h"
#include "recording/EncoderProbeProcess.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
using Microsoft::WRL::ComPtr;
using namespace llcv::recording;
using namespace llcv::recording::detail;
static void Check(bool ok,const char* text){if(!ok){printf("FAIL %s\n",text);std::exit(1);}}
static void Hr(HRESULT hr,const char* text){if(FAILED(hr)){printf("FAIL %s: 0x%08lX\n",text,static_cast<unsigned long>(hr));std::exit(1);}}
static ComPtr<IMFByteStream> Temp(){
    ComPtr<IMFByteStream> bytes;
    Hr(MFCreateTempFile(MF_ACCESSMODE_READWRITE,MF_OPENMODE_DELETE_IF_EXIST,MF_FILEFLAGS_NONE,&bytes),"temporary file");
    return bytes;
}
static void Samples(){
    ProbeSamples empty;Check(!empty.Complete(),"initialization with no output rejected");
    Check(!empty.Add(0,0),"empty packet rejected");
    for(size_t i=0;i<kProbeFrames;++i)Check(empty.Add(ProbeTime(i),100),"normal packet");
    Check(empty.Complete(),"complete frame cadence");
    Check(!empty.Add(ProbeTime(kProbeFrames),100),"extra frame rejected");
    ProbeSamples missing;for(size_t i=0;i<kProbeFrames-1;++i)missing.Add(ProbeTime(i),100);
    Check(!missing.Complete(),"early EOS rejected");
    auto duplicate=empty;duplicate.times[3]=duplicate.times[2];Check(!duplicate.Complete(),"duplicate PTS rejected");
    auto gap=empty;gap.times.back()+=ProbeTime(1);Check(!gap.Complete(),"dropped frame gap rejected");
    auto reordered=empty;std::reverse(reordered.times.begin(),reordered.times.end());
    for(auto& time:reordered.times)time+=666666;
    Check(reordered.Complete(),"B frame reordering and constant encoder delay accepted");
    auto rounded=empty;for(auto& time:rounded.times)time=time/10000*10000;
    Check(rounded.Complete(),"MP4 millisecond rounding accepted");
    auto invalid=empty;invalid.times[0]=LLONG_MIN;Check(!invalid.Complete(),"invalid timestamp cannot overflow");
    auto offset=empty;for(auto& time:offset.times)time+=LLONG_MIN;
    Check(offset.Complete(),"constant signed-limit origin accepted without overflow");
}
static void MediaTypes(){
    ComPtr<IMFMediaType> type;Hr(MFCreateMediaType(&type),"type fixture");
    Hr(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"major type");
    Hr(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_HEVC),"subtype");
    Hr(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,1280,720),"size");
    Check(IsProbeMediaType(type.Get(),Codec::Hevc),"exact size accepted");
    Check(!IsProbeMediaType(type.Get(),Codec::H264),"wrong codec rejected by type");
    Hr(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,1280,736),"padded size");
    Check(!IsProbeMediaType(type.Get(),Codec::Hevc),"padding without crop rejected");
    MFVideoArea area{};area.Area.cx=1280;area.Area.cy=720;
    auto crop=[&](UINT32 size=sizeof(MFVideoArea)){Hr(type->SetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,reinterpret_cast<const UINT8*>(&area),size),"crop fixture");};
    crop();Check(IsProbeMediaType(type.Get(),Codec::Hevc),"HEVC block padding accepted with visible crop");
    area.OffsetY.value=16;crop();Check(IsProbeMediaType(type.Get(),Codec::Hevc),"valid crop offset accepted");
    area.OffsetY.fract=1;crop();Check(!IsProbeMediaType(type.Get(),Codec::Hevc),"fractional crop outside frame rejected");
    area.OffsetY.fract=0;area.OffsetY.value=-1;crop();Check(!IsProbeMediaType(type.Get(),Codec::Hevc),"negative crop rejected");
    area.OffsetY.value=0;area.Area.cy=719;crop();Check(!IsProbeMediaType(type.Get(),Codec::Hevc),"incorrect visible height rejected");
    area.Area.cy=720;crop(sizeof(area)-1);Check(!IsProbeMediaType(type.Get(),Codec::Hevc),"malformed crop rejected");
    Hr(type->DeleteItem(MF_MT_MINIMUM_DISPLAY_APERTURE),"remove crop");
    Hr(type->SetBlob(MF_MT_GEOMETRIC_APERTURE,reinterpret_cast<const UINT8*>(&area),sizeof(area)),"geometric crop");
    Check(IsProbeMediaType(type.Get(),Codec::Hevc),"geometric aperture fallback accepted");
}
static void Dump(IMFByteStream* bytes){
    bytes->SetCurrentPosition(0);ComPtr<IMFByteStream> view;ComPtr<IMFSourceReader> reader;
    if(FAILED(MFCreateMFByteStreamWrapper(bytes,&view))||FAILED(MFCreateSourceReaderFromByteStream(view.Get(),nullptr,&reader)))return;
    constexpr DWORD stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    ComPtr<IMFMediaType> type;if(SUCCEEDED(reader->GetCurrentMediaType(stream,&type))){
        GUID subtype{};UINT32 w=0,h=0;type->GetGUID(MF_MT_SUBTYPE,&subtype);MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h);
        printf("native type=%08lX %ux%u\n",subtype.Data1,w,h);
        for(const auto& key:{MF_MT_MINIMUM_DISPLAY_APERTURE,MF_MT_GEOMETRIC_APERTURE}){
            MFVideoArea area{};UINT32 size=0;const HRESULT hr=type->GetBlob(key,reinterpret_cast<UINT8*>(&area),sizeof(area),&size);
            printf("aperture=%08lX hr=%08lX size=%u x=%d:%u y=%d:%u %ldx%ld\n",key.Data1,static_cast<unsigned long>(hr),size,area.OffsetX.value,area.OffsetX.fract,area.OffsetY.value,area.OffsetY.fract,area.Area.cx,area.Area.cy);
        }
    }
    for(int i=0;i<32;++i){
        ComPtr<IMFSample> sample;DWORD flags=0;LONGLONG time=0;
        const HRESULT hr=reader->ReadSample(stream,0,nullptr,&flags,&time,&sample);
        DWORD size=0;if(sample)sample->GetTotalLength(&size);
        printf("packet=%d hr=%08lX flags=%08lX time=%lld bytes=%lu\n",i,static_cast<unsigned long>(hr),flags,time,size);
        if(FAILED(hr)||(flags&MF_SOURCE_READERF_ENDOFSTREAM))break;
    }
}
int wmain(int argc,wchar_t** argv){
    if(argc==2){
        const int child=RunEncoderProbeChild(argv[1],[]{return ProbeEncoders();});
        if(child>=0)return child;
    }
    std::atomic<bool> cancelled{true};
    for(const auto& support:ProbeEncoders(&cancelled))
        Check(!support.container&&!support.software&&support.hardware.empty(),
            "cancelled probe does not start hardware discovery or publish partial support");
    Samples();Hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM");Hr(MFStartup(MF_VERSION,MFSTARTUP_LITE),"MF");
    MediaTypes();
#ifdef LLCV_RECORDING_MODERN_MP4
    {
        // Exercise the actual UI support probe, not a parallel test-only
        // factory. HGLOBAL-backed MF streams can advertise read-only access
        // and previously hid working HEVC/AV1 containers from the settings.
        const auto isolated=probe_process::Run(nullptr,25000);
        Check(isolated.completed,"real encoder discovery completes in the isolated helper");
        const auto& support=isolated.capabilities;
        Check(support[1].container,"HEVC file container remains selectable when an encoder exists");
        Check(support[2].container,"AV1 container support does not require an installed encoder");
        Check(support[0].software&&support[0].softwareRates.bitrate,
            "isolated discovery preserves the working software H.264 encoder");
    }
#endif
    if(argc>1&&wcscmp(argv[1],L"--hardware")==0){
        // Opt-in diagnostic for installed hardware; absent codecs are reported,
        // not interpreted as a regression on machines with different GPUs.
        int outputFailures=0;
        for(auto codec:{Codec::H264,Codec::Hevc,Codec::Av1})for(auto mode:{RateControl::Bitrate,RateControl::Quality}){
            Options options;options.codec=codec;options.rateControl=mode;auto bytes=Temp();
            const HRESULT encoded=WriteEncoderProbe(bytes.Get(),options);
            const HRESULT checked=SUCCEEDED(encoded)?ValidateEncoderProbe(bytes.Get(),codec):encoded;
            printf("HW codec=%d mode=%d encode=%08lX check=%08lX\n",static_cast<int>(codec),static_cast<int>(mode),static_cast<unsigned long>(encoded),static_cast<unsigned long>(checked));
            if(SUCCEEDED(encoded)&&FAILED(checked)){++outputFailures;Dump(bytes.Get());}
        }
        MFShutdown();CoUninitialize();return outputFailures?1:0;
    }
    {
        // The inbox software H.264 encoder is the deterministic integration
        // baseline. No capture device, Radeon, NVIDIA, or decoder is required.
        Options options;options.encoder=L"cpu";
        for(auto mode:{RateControl::Bitrate,RateControl::Quality}){
            options.rateControl=mode;
            for(auto quality:{Quality::Compact,Quality::Balanced,Quality::High}){
                if(mode==RateControl::Bitrate&&quality!=Quality::Balanced)continue;
                options.quality=quality;auto bytes=Temp();
                Hr(WriteEncoderProbe(bytes.Get(),options),"real encode and finalize");
                QWORD size=0;Hr(bytes->GetLength(&size),"completed file size");Check(size>0&&size<=kMaxProbeBytes,"bounded nonempty output");
                Hr(ValidateEncoderProbe(bytes.Get(),options.codec),"read all finalized packets");
                QWORD after=0;Hr(bytes->GetLength(&after),"reader does not close backing file");Check(after==size,"reader preserves output");
                Hr(ValidateEncoderProbe(bytes.Get(),options.codec),"readback is repeatable");
                printf("CPU H.264 mode=%d QP=%d verified %zu frames, %llu bytes\n",static_cast<int>(mode),QualityQp(quality),kProbeFrames,size);
            }
        }
        {auto bytes=Temp();Check(FAILED(ValidateEncoderProbe(bytes.Get(),Codec::H264)),"empty MP4 rejected");}
        {auto bytes=Temp();Hr(WriteEncoderProbe(bytes.Get(),options),"mismatch fixture");
            Check(FAILED(ValidateEncoderProbe(bytes.Get(),Codec::Hevc)),"wrong codec rejected");}
        {auto bytes=Temp();Hr(WriteEncoderProbe(bytes.Get(),options),"truncation fixture");Hr(bytes->SetLength(16),"truncate fixture");
            Check(FAILED(ValidateEncoderProbe(bytes.Get(),Codec::H264)),"truncated MP4 rejected");}
        {auto bytes=Temp();Hr(bytes->SetLength(kMaxProbeBytes+1),"oversize fixture");
            Check(FAILED(ValidateEncoderProbe(bytes.Get(),Codec::H264)),"oversized output rejected before reading");}
        Check(FAILED(WriteEncoderProbe(nullptr,options)),"null writer stream rejected");
        Check(FAILED(ValidateEncoderProbe(nullptr,Codec::H264)),"null output rejected");
    }
    MFShutdown();CoUninitialize();puts("Encoder probe output regression tests passed.");
}
