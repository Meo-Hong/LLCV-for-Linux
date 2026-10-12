#pragma once

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <string>

namespace llcv::update::detail {

// Narrow WinHTTP seam for deterministic transport/lifetime tests. Production
// uses the default functions; callbacks never retain this caller-owned table.
struct WinHttpApi {
    decltype(&WinHttpOpen) open = &WinHttpOpen;
    decltype(&WinHttpSetTimeouts) setTimeouts = &WinHttpSetTimeouts;
    decltype(&WinHttpConnect) connect = &WinHttpConnect;
    decltype(&WinHttpOpenRequest) openRequest = &WinHttpOpenRequest;
    decltype(&WinHttpAddRequestHeaders) addHeaders = &WinHttpAddRequestHeaders;
    decltype(&WinHttpSetStatusCallback) setCallback = &WinHttpSetStatusCallback;
    decltype(&WinHttpSetOption) setOption = &WinHttpSetOption;
    decltype(&WinHttpSendRequest) send = &WinHttpSendRequest;
    decltype(&WinHttpReceiveResponse) receive = &WinHttpReceiveResponse;
    decltype(&WinHttpQueryHeaders) queryHeaders = &WinHttpQueryHeaders;
    decltype(&WinHttpReadData) read = &WinHttpReadData;
    decltype(&WinHttpCloseHandle) close = &WinHttpCloseHandle;
};

bool FetchReleaseJson(const wchar_t* userAgent, std::string& json,
                      const std::atomic<bool>* stop, const WinHttpApi& api,
                      DWORD deadlineMilliseconds = 10000);

} // namespace llcv::update::detail
