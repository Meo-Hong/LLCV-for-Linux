#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace llcv::recording {
enum class Phase { Idle, Starting, Recording, Finalizing, Saved, Failed };
enum class Failure {
    None, DiskFull, WriteFailed, EncoderOverload, EncoderFailed,
    InvalidInput, StorageUnavailable, FinalizeFailed
};

// Published by the recording worker. UI reads must never query a disk or wait
// for an encoder; an unknown value is distinct from a measured zero.
struct Snapshot {
    Phase phase=Phase::Idle;
    Failure failure=Failure::None;
    HRESULT result=S_OK, finalizeResult=S_OK;
    bool busy=false, playable=false,paused=false;
    uint64_t elapsedMs=0;
    // Worker-side low-space safety check; not a recording-time estimate.
    uint64_t freeBytes=0;
    bool freeBytesKnown=false;
    std::wstring path;
};
}
