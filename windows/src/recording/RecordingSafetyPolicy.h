#pragma once
#include "recording/RecordingStatus.h"
#include <mferror.h>

namespace llcv::recording {
enum class Operation { Input, Storage, Encoder, Write, Finalize, Queue };

inline HRESULT FinalizationResult(HRESULT pendingWrite,HRESULT finalize) noexcept {
    // A successful container close cannot erase a failed last-frame write.
    return FAILED(finalize) ? finalize : pendingWrite;
}

inline Failure ClassifyFailure(HRESULT result, Operation operation) noexcept {
    if (SUCCEEDED(result)) return Failure::None;
    if (result == MF_E_INVALID_TIMESTAMP) return Failure::InvalidInput;
    // Disk-full HRESULTs can originate from the filesystem, byte stream, or
    // muxer. Keep the actionable cause even when it occurs during finalization.
    if (result == HRESULT_FROM_WIN32(ERROR_DISK_FULL) ||
        result == HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL) ||
        result == STG_E_MEDIUMFULL)
        return Failure::DiskFull;
    if (result == STG_E_WRITEFAULT ||
        result == HRESULT_FROM_WIN32(ERROR_WRITE_FAULT) ||
        result == HRESULT_FROM_WIN32(ERROR_IO_DEVICE) ||
        result == HRESULT_FROM_WIN32(ERROR_CRC) ||
        result == HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT) ||
        result == HRESULT_FROM_WIN32(ERROR_FILE_CORRUPT))
        return Failure::WriteFailed;
    switch (operation) {
    case Operation::Input: return Failure::InvalidInput;
    case Operation::Storage: return Failure::StorageUnavailable;
    case Operation::Encoder: return Failure::EncoderFailed;
    case Operation::Write: return Failure::WriteFailed;
    case Operation::Finalize: return Failure::FinalizeFailed;
    // A full bounded queue is encoder OR storage backpressure. The UI must not
    // claim that this proves GPU/CPU encoder overload specifically.
    case Operation::Queue: return Failure::EncoderOverload;
    }
    return Failure::EncoderFailed;
}

} // namespace llcv::recording
