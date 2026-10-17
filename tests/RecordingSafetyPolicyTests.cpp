#include "recording/RecordingSafetyPolicy.h"
#include <cstdio>

using namespace llcv::recording;
namespace {
int failures = 0;
void Expect(bool passed, const char* text) {
    if (!passed) { ++failures; std::printf("FAIL: %s\n", text); }
}

}

int main() {
    Expect(FinalizationResult(E_FAIL,S_OK)==E_FAIL,"failed final frame is not erased by successful MP4 close");
    Expect(FinalizationResult(E_FAIL,STG_E_WRITEFAULT)==STG_E_WRITEFAULT,"failed container close remains visible");
    Expect(FinalizationResult(S_OK,S_OK)==S_OK,"clean pending frame and close remain successful");
    for (auto operation : { Operation::Input, Operation::Storage, Operation::Encoder,
                            Operation::Write, Operation::Finalize, Operation::Queue }) {
        Expect(ClassifyFailure(S_OK, operation) == Failure::None, "success is not a failure");
        Expect(ClassifyFailure(S_FALSE, operation) == Failure::None, "nonzero success is not a failure");
        Expect(ClassifyFailure(MF_E_INVALID_TIMESTAMP, operation) == Failure::InvalidInput,
               "timeline rejection is not misreported as disk failure");
        for (auto result : { HRESULT_FROM_WIN32(ERROR_DISK_FULL),
                             HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL), STG_E_MEDIUMFULL })
            Expect(ClassifyFailure(result, operation) == Failure::DiskFull,
                   "disk full survives encoder/finalizer operation context");
        Expect(ClassifyFailure(STG_E_WRITEFAULT, operation) == Failure::WriteFailed,
               "storage write fault remains distinguishable from encoder error");
    }
    Expect(ClassifyFailure(E_INVALIDARG, Operation::Input) == Failure::InvalidInput, "bad input category");
    Expect(ClassifyFailure(E_ACCESSDENIED, Operation::Storage) == Failure::StorageUnavailable, "folder access category");
    Expect(ClassifyFailure(E_FAIL, Operation::Encoder) == Failure::EncoderFailed, "encoder initialization category");
    Expect(ClassifyFailure(E_FAIL, Operation::Write) == Failure::WriteFailed, "sample writing category");
    Expect(ClassifyFailure(E_FAIL, Operation::Finalize) == Failure::FinalizeFailed, "finalization category");
    Expect(ClassifyFailure(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW), Operation::Queue) == Failure::EncoderOverload,
           "bounded queue backpressure category");

    std::printf("Recording safety policy: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
