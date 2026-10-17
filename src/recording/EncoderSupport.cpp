#include "recording/EncoderSupport.h"
#include "recording/EncoderProbe.h"
#include "recording/RecordingContainer.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <memory>
#include <cstring>
#include <strmif.h>
#include <codecapi.h>
namespace llcv::recording {
using Microsoft::WRL::ComPtr;
const GUID& CodecSubtype(Codec codec) {
    if(codec==Codec::Hevc)return MFVideoFormat_HEVC;
    if(codec==Codec::Av1)return MFVideoFormat_AV1;
    return MFVideoFormat_H264;
}
namespace {
std::wstring String(IMFAttributes* attributes,REFGUID key) {
    PWSTR value=nullptr;UINT32 length=0;
    if(!attributes||FAILED(attributes->GetAllocatedString(key,&value,&length)))return {};
    std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> owned(value,CoTaskMemFree);
    return std::wstring(value,length);
}
bool ContainerSupports(Codec codec) {
    ComPtr<IMFByteStream> bytes;
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFSinkWriter> writer;
    ComPtr<IMFMediaType> type;
    // Probe the same seekable file capability as real recording. The MF
    // IStream/HGLOBAL adapter can omit byte-stream capability flags even when
    // the backing IStream supports writes; that is not a codec limitation.
    if(FAILED(MFCreateTempFile(MF_ACCESSMODE_READWRITE,MF_OPENMODE_DELETE_IF_EXIST,MF_FILEFLAGS_NONE,&bytes)) ||
       FAILED(MFCreateAttributes(&attributes,1)) ||
       FAILED(CreateRecordingWriter(bytes.Get(),attributes.Get(),&writer,codec)) ||
       FAILED(MFCreateMediaType(&type)))return false;
    type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE,CodecSubtype(codec));
    type->SetUINT32(MF_MT_AVG_BITRATE,8000000);
    type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
    MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,1280,720);
    MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,30,1);
    MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
    DWORD stream=0;return SUCCEEDED(writer->AddStream(type.Get(),&stream));
}
bool ProbeRate(Codec codec,bool cpu,RateControl mode,Quality quality=Quality::Balanced) {
    ComPtr<IMFByteStream> bytes;
    if(FAILED(MFCreateTempFile(MF_ACCESSMODE_READWRITE,MF_OPENMODE_DELETE_IF_EXIST,MF_FILEFLAGS_NONE,&bytes)))return false;
    Options options;options.codec=codec;options.encoder=cpu?L"cpu":L"";
    options.rateControl=mode;options.quality=quality;
    return SUCCEEDED(detail::WriteEncoderProbe(bytes.Get(),options)) &&
        SUCCEEDED(detail::ValidateEncoderProbe(bytes.Get(),codec));
}
}
namespace detail {
HRESULT WriteEncoderProbe(IMFByteStream* bytes,const Options& options) {
    if(!bytes||!Valid(options))return E_INVALIDARG;
    ComPtr<IMFAttributes> attrs;
    // Use the same crash-tolerant container as production, including the
    // wrapper that keeps our owned temporary file open for readback.
    ComPtr<IMFSinkWriter> writer;ComPtr<IMFMediaType> output,input;
    HRESULT hr=MFCreateAttributes(&attrs,3);if(FAILED(hr))return hr;
    if(FAILED(hr=attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,options.encoder!=L"cpu")) ||
       // Only the tiny probe is unthrottled. Production recording is unchanged.
       FAILED(hr=attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE)) ||
       FAILED(hr=CreateRecordingWriter(bytes,attrs.Get(),&writer,options.codec)) ||
       FAILED(hr=MFCreateMediaType(&output)) || FAILED(hr=MFCreateMediaType(&input)))return hr;
    if(FAILED(hr=output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)) ||
       FAILED(hr=output->SetGUID(MF_MT_SUBTYPE,CodecSubtype(options.codec))) ||
       FAILED(hr=output->SetUINT32(MF_MT_AVG_BITRATE,options.videoMbps*1000000)) ||
       FAILED(hr=output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive)) ||
       FAILED(hr=MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,kProbeWidth,kProbeHeight)) ||
       FAILED(hr=MFSetAttributeRatio(output.Get(),MF_MT_FRAME_RATE,kProbeFps,1)) ||
       FAILED(hr=MFSetAttributeRatio(output.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1)))return hr;
    DWORD stream=0;
    if(FAILED(hr=writer->AddStream(output.Get(),&stream)) || FAILED(hr=output->CopyAllItems(input.Get())) ||
       FAILED(hr=input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12)) ||
       FAILED(hr=ConfigureVideoInput(writer.Get(),stream,input.Get(),options)) ||
       FAILED(hr=writer->BeginWriting()) || FAILED(hr=VerifyRateControl(writer.Get(),stream,options)))return hr;
    constexpr DWORD frameBytes=kProbeWidth*kProbeHeight*3/2;
    for(size_t frame=0;frame<kProbeFrames;++frame){
        ComPtr<IMFSample> sample;ComPtr<IMFMediaBuffer> buffer;
        if(FAILED(hr=MFCreateSample(&sample)) || FAILED(hr=MFCreateMemoryBuffer(frameBytes,&buffer)))return hr;
        BYTE* data=nullptr;if(FAILED(hr=buffer->Lock(&data,nullptr,nullptr)))return hr;
        // Spatial detail and motion, rather than an all-black clip optimized away.
        for(UINT32 y=0;y<kProbeHeight;++y)for(UINT32 x=0;x<kProbeWidth;++x)
            data[y*kProbeWidth+x]=static_cast<BYTE>(16+((x/8+y/8+frame*7)%220));
        memset(data+kProbeWidth*kProbeHeight,128,frameBytes-kProbeWidth*kProbeHeight);
        if(FAILED(hr=buffer->Unlock()) || FAILED(hr=buffer->SetCurrentLength(frameBytes)) ||
           FAILED(hr=sample->AddBuffer(buffer.Get())) || FAILED(hr=sample->SetSampleTime(ProbeTime(frame))) ||
           FAILED(hr=sample->SetSampleDuration(ProbeTime(frame+1)-ProbeTime(frame))) ||
           FAILED(hr=writer->WriteSample(stream,sample.Get())))return hr;
    }
    // Successful SetValue/BeginWriting alone is not sufficient. Drain delayed
    // frames and finish the container before checking actual compressed output.
    if(FAILED(hr=VerifyRateControl(writer.Get(),stream,options)))return hr;
    return writer->Finalize();
}
bool IsProbeMediaType(IMFMediaType* type,Codec codec) {
    if(!type||static_cast<int>(codec)<0||static_cast<int>(codec)>2)return false;
    GUID major{},subtype{};UINT32 width=0,height=0;
    if(FAILED(type->GetGUID(MF_MT_MAJOR_TYPE,&major)) || FAILED(type->GetGUID(MF_MT_SUBTYPE,&subtype)) ||
       FAILED(MFGetAttributeSize(type,MF_MT_FRAME_SIZE,&width,&height)) ||
       major!=MFMediaType_Video || subtype!=CodecSubtype(codec))return false;
    // HEVC can report a block-aligned coded height (e.g. 736 for 720 visible
    // lines). Validate the declared crop, not just the padded storage size.
    for(const auto& key:{MF_MT_MINIMUM_DISPLAY_APERTURE,MF_MT_GEOMETRIC_APERTURE}){
        MFVideoArea area{};UINT32 size=0;
        const HRESULT hr=type->GetBlob(key,reinterpret_cast<UINT8*>(&area),sizeof(area),&size);
        if(hr==MF_E_ATTRIBUTENOTFOUND)continue;
        if(FAILED(hr)||size!=sizeof(area)||area.OffsetX.value<0||area.OffsetY.value<0||
           area.Area.cx!=kProbeWidth||area.Area.cy!=kProbeHeight)return false;
        const auto right=(uint64_t(area.OffsetX.value)+kProbeWidth)*65536+area.OffsetX.fract;
        const auto bottom=(uint64_t(area.OffsetY.value)+kProbeHeight)*65536+area.OffsetY.fract;
        return right<=uint64_t(width)*65536 && bottom<=uint64_t(height)*65536;
    }
    return width==kProbeWidth&&height==kProbeHeight;
}
HRESULT ValidateEncoderProbe(IMFByteStream* bytes,Codec codec) {
    if(!bytes||static_cast<int>(codec)<0||static_cast<int>(codec)>2)return E_INVALIDARG;
    QWORD length=0;HRESULT hr=bytes->GetLength(&length);if(FAILED(hr))return hr;
    if(!length||length>kMaxProbeBytes)return MF_E_INVALID_FILE_FORMAT;
    if(FAILED(hr=bytes->SetCurrentPosition(0)))return hr;
    ComPtr<IMFByteStream> readerBytes;
    if(FAILED(hr=MFCreateMFByteStreamWrapper(bytes,&readerBytes)))return hr;
    ComPtr<IMFSourceReader> reader;ComPtr<IMFMediaType> type;
    constexpr DWORD videoStream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    if(FAILED(hr=MFCreateSourceReaderFromByteStream(readerBytes.Get(),nullptr,&reader)) ||
       FAILED(hr=reader->GetCurrentMediaType(videoStream,&type)))return hr;
    if(!IsProbeMediaType(type.Get(),codec))return MF_E_INVALIDMEDIATYPE;
    // Read compressed packets, not decoded NV12. An optional OS HEVC/AV1 decoder
    // must not become a new requirement for using an otherwise working encoder.
    ProbeSamples samples;
    for(size_t reads=0;reads<kProbeFrames*2+4;++reads){
        ComPtr<IMFSample> sample;DWORD flags=0;LONGLONG time=0;
        if(FAILED(hr=reader->ReadSample(videoStream,0,nullptr,&flags,&time,&sample)))return hr;
        if(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED|MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED))
            return MF_E_INVALID_FILE_FORMAT;
        if(sample){
            DWORD size=0;if(FAILED(hr=sample->GetTotalLength(&size)))return hr;
            if(!samples.Add(time,size))return MF_E_INVALID_FILE_FORMAT;
        }
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)return samples.Complete()?S_OK:MF_E_INVALID_FILE_FORMAT;
    }
    return MF_E_INVALID_FILE_FORMAT;
}
}
Capabilities ProbeEncoders(const std::atomic<bool>* stop) {
    Capabilities result{};
    const auto cancelled=[&]{return stop && stop->load();};
    if(cancelled())return result;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com))return result;
    const HRESULT mf=MFStartup(MF_VERSION,MFSTARTUP_LITE);
    if(SUCCEEDED(mf)){
        try {
            for(int i=0;i<3;++i){
                if(cancelled())break;
                auto& support=result[i];const auto codec=static_cast<Codec>(i);
                support.container=ContainerSupports(codec);
                MFT_REGISTER_TYPE_INFO output{MFMediaType_Video,CodecSubtype(codec)};
                for(bool hardware:{true,false}){
                    if(cancelled())break;
                    struct Activations {
                        IMFActivate** values=nullptr;UINT32 count=0;
                        ~Activations(){for(UINT32 j=0;j<count;++j)values[j]->Release();CoTaskMemFree(values);}
                    } list;
                    const UINT32 flags=hardware?MFT_ENUM_FLAG_HARDWARE:
                        (MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_ASYNCMFT|MFT_ENUM_FLAG_LOCALMFT);
                    if(SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,flags|MFT_ENUM_FLAG_SORTANDFILTER,
                        nullptr,&output,&list.values,&list.count))){
                        for(UINT32 j=0;j<list.count;++j){
                            // Discovery runs on the existing asynchronous probe worker.
                            if(hardware){
                                auto id=String(list.values[j],MFT_ENUM_HARDWARE_URL_Attribute);
                                if(!id.empty())support.hardware.push_back({String(list.values[j],MFT_FRIENDLY_NAME_Attribute),id});
                            }else support.software=true;
                        }
                    }
                }
                if(support.container){
                    for(bool cpu:{false,true}){
                        if(cancelled())break;
                        if(cpu?!support.software:support.hardware.empty())continue;
                        auto& rates=cpu?support.softwareRates:support.hardwareRates;
                        rates.bitrate=ProbeRate(codec,cpu,RateControl::Bitrate);
                        rates.quality=!cancelled() && ProbeRate(codec,cpu,RateControl::Quality) &&
                            !cancelled() && ProbeRate(codec,cpu,RateControl::Quality,Quality::Compact) &&
                            !cancelled() && ProbeRate(codec,cpu,RateControl::Quality,Quality::High);
                    }
                }
            }
        }catch(...){result={};}
        MFShutdown();
    }
    CoUninitialize();return cancelled()?Capabilities{}:result;
}
HRESULT VerifyEncoder(IMFSinkWriter* writer,DWORD stream,const Options& options){
    ComPtr<IMFSinkWriterEx> extended;
    HRESULT hr=writer->QueryInterface(IID_PPV_ARGS(&extended));if(FAILED(hr))return hr;
    for(DWORD i=0;i<16;++i){
        GUID category{};ComPtr<IMFTransform> transform;
        hr=extended->GetTransformForStream(stream,i,&category,&transform);
        if(FAILED(hr))return hr;
        if(category!=MFT_CATEGORY_VIDEO_ENCODER)continue;
        ComPtr<IMFAttributes> attributes;
        hr=transform->GetAttributes(&attributes);
        const auto device=SUCCEEDED(hr)?String(attributes.Get(),MFT_ENUM_HARDWARE_URL_Attribute):L"";
        if(options.encoder==L"cpu")return device.empty()?S_OK:MF_E_TOPO_CODEC_NOT_FOUND;
        if(device.empty())return MF_E_TOPO_CODEC_NOT_FOUND;
        return S_OK;
    }
    return MF_E_TOPO_CODEC_NOT_FOUND;
}
namespace {
HRESULT ReadBack(ICodecAPI* api,const GUID& key,uint64_t expected) {
    VARIANT result{};HRESULT hr=api->GetValue(&key,&result);
    if(SUCCEEDED(hr)){
        const bool matches=(result.vt==VT_UI4&&result.ulVal==expected)||(result.vt==VT_UI8&&result.ullVal==expected);
        if(!matches)hr=MF_E_INVALIDREQUEST;
    }
    VariantClear(&result);return hr;
}
}
HRESULT ConfigureVideoInput(IMFSinkWriter* writer,DWORD stream,IMFMediaType* input,const Options& options){
    if(!Valid(options))return E_INVALIDARG;
    ComPtr<IMFAttributes> parameters;
    HRESULT hr=MFCreateAttributes(&parameters,2);if(FAILED(hr))return hr;
    const bool quality=options.rateControl==RateControl::Quality;
    hr=parameters->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
        quality?eAVEncCommonRateControlMode_Quality:eAVEncCommonRateControlMode_CBR);
    if(FAILED(hr))return hr;
    hr=quality?parameters->SetUINT64(CODECAPI_AVEncVideoEncodeQP,QualityQp(options.quality)):
        parameters->SetUINT32(CODECAPI_AVEncCommonMeanBitRate,options.videoMbps*1000000);
    if(FAILED(hr))return hr;
    // These are static properties on some MFTs. Pass them BEFORE media-type
    // negotiation; SetValue afterwards can succeed without changing the mode.
    hr=writer->SetInputMediaType(stream,input,parameters.Get());if(FAILED(hr))return hr;
    hr=VerifyEncoder(writer,stream,options);if(FAILED(hr))return hr;
    return VerifyRateControl(writer,stream,options);
}
HRESULT VerifyRateControl(IMFSinkWriter* writer,DWORD stream,const Options& options){
    ComPtr<ICodecAPI> api;
    HRESULT hr=writer->GetServiceForStream(stream,GUID_NULL,IID_PPV_ARGS(&api));if(FAILED(hr))return hr;
    return VerifyRateControl(api.Get(),options);
}
HRESULT VerifyRateControl(ICodecAPI* api,const Options& options){
    if(!api||!Valid(options))return E_INVALIDARG;
    const bool quality=options.rateControl==RateControl::Quality;
    const HRESULT hr=ReadBack(api,CODECAPI_AVEncCommonRateControlMode,
        quality?eAVEncCommonRateControlMode_Quality:eAVEncCommonRateControlMode_CBR);
    if(FAILED(hr))return hr;
    return quality?ReadBack(api,CODECAPI_AVEncVideoEncodeQP,QualityQp(options.quality)):
        ReadBack(api,CODECAPI_AVEncCommonMeanBitRate,options.videoMbps*1000000);
}
}
