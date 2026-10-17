#pragma once
#include <mfidl.h>
#include <cstddef>

namespace llcv::recording {
// Compressed bytes only. Writes own their queued copies; a successful Flush
// means they reached the underlying file. Physical failures are also published
// through RecordingStorageResult while recording. Capture pools are unchanged.
struct StorageLimits { size_t bytes=32*1024*1024,operations=4096; };
HRESULT BufferRecordingStorage(IMFByteStream* file,IMFByteStream* sibling,
    IMFByteStream** result,StorageLimits limits={});
// Periodic durability checks enqueue a coalesced flush. Flush()/Close() still
// wait for physical completion when the recording is finalized.
HRESULT RequestRecordingFlush(IMFByteStream* stream);
HRESULT RecordingStorageResult(IMFByteStream* stream);
// Seal a completed split file in order, without delaying the next segment.
HRESULT RetireRecordingStorage(IMFByteStream* stream);
}
