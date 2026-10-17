#pragma once
#include "recording/RecordingOptions.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <vector>
struct IMFSinkWriter;
struct IMFMediaType;
struct ICodecAPI;
namespace llcv::recording {
struct EncoderInfo { std::wstring name,id; };
struct RateSupport {
    bool bitrate=false,quality=false;
    bool Available() const { return bitrate||quality; }
    bool Supports(RateControl mode) const { return mode==RateControl::Quality?quality:bitrate; }
};
struct CodecSupport {
    bool container=false, software=false;
    std::vector<EncoderInfo> hardware;
    RateSupport hardwareRates,softwareRates;
    bool Available() const { return container && ((!hardware.empty()&&hardwareRates.Available()) || (software&&softwareRates.Available())); }
    const RateSupport& Rates(bool cpu) const { return cpu?softwareRates:hardwareRates; }
};
using Capabilities=std::array<CodecSupport,3>;
const GUID& CodecSubtype(Codec);
// Worker-only: verifies settings, a short real encode, finalization, and MP4
// packet count/cadence. Not a guarantee of other resolutions or actual CQP quality.
// Cancellation is checked between driver calls; an in-flight call is allowed
// to finish safely. The flag must outlive this call.
Capabilities ProbeEncoders(const std::atomic<bool>* stop=nullptr);
HRESULT VerifyEncoder(IMFSinkWriter*,DWORD stream,const Options&);
// Runs only on the recording/probe worker, before BeginWriting. Never falls
// back to another policy or rate-control mode when a request is rejected.
HRESULT ConfigureVideoInput(IMFSinkWriter*,DWORD stream,IMFMediaType*,const Options&);
HRESULT VerifyRateControl(IMFSinkWriter*,DWORD stream,const Options&);
HRESULT VerifyRateControl(ICodecAPI*,const Options&);
}
