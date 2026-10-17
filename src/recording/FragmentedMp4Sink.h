#pragma once
#include <mfidl.h>
#include "recording/RecordingArchive.h"

namespace llcv::recording {
// An encoded-packet archive sink. The caller still uses the Media Foundation
// sink writer/encoders; libavformat only multiplexes HEVC/AV1 and raw AAC.
// The caller owns the byte stream and closes it after releasing the writer.
HRESULT CreateFragmentedMp4Sink(IMFByteStream* bytes, IMFMediaSink** sink,
    const SplitPolicy* split=nullptr,std::shared_ptr<RecordingArchive>* archive=nullptr);
}
