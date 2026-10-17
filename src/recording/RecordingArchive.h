#pragma once
#include <cstdint>
#include <windows.h>
#include <memory>
#include <string>
#include <filesystem>
#include <cstdio>
namespace llcv::recording {
inline std::wstring RecordingPartPath(const std::wstring& path,unsigned part) {
    std::filesystem::path p(path);
    wchar_t suffix[32]{};swprintf_s(suffix,L"-%03u",part);
    return (p.parent_path()/(p.stem().wstring()+suffix+p.extension().wstring())).wstring();
}
struct SplitPolicy {
    std::wstring basePath;
    uint64_t duration100ns=0,bytes=0;
};
// Synchronizes the currently open segment with the existing worker-side
// durability checks. No UI/capture thread calls into this interface.
struct RecordingArchive {
    virtual ~RecordingArchive()=default;
    virtual HRESULT Flush()=0;
    virtual HRESULT RequestFlush()=0;
    virtual HRESULT Close()=0;
    virtual std::wstring CurrentPath()=0;
};
}
