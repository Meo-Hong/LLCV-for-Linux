#include "ElgatoHidToneMapping.h"
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <cwchar>
#include <cstring>

// Wire formats and supported USB IDs from Elgato capture-device-support
// fe9630974d47f51bf54826e72fb8b654e620aa93 (MIT, see DEPENDENCIES.txt).
namespace llcv::capture {
namespace {
bool ParseUsbId(std::wstring_view path, unsigned short& vendor, unsigned short& product, bool hid = false) {
    const std::wstring_view prefix = hid ? L"\\\\?\\hid#vid_" : L"\\\\?\\usb#vid_";
    if (path.size() < prefix.size() + 13 || _wcsnicmp(path.data(), prefix.data(), prefix.size()) != 0) return false;
    auto hex4 = [](std::wstring_view s, unsigned short& value) {
        value = 0;
        for (wchar_t c : s) {
            unsigned digit = c >= L'0' && c <= L'9' ? c-L'0' :
                c >= L'a' && c <= L'f' ? c-L'a'+10 : c >= L'A' && c <= L'F' ? c-L'A'+10 : 16;
            if (digit > 15) return false;
            value = static_cast<unsigned short>((value << 4) | digit);
        }
        return true;
    };
    const size_t n = prefix.size();
    return hex4(path.substr(n,4), vendor) && _wcsnicmp(path.data()+n+4,L"&pid_",5)==0 &&
        hex4(path.substr(n+9,4), product) && path.size() > n+13 &&
        (path[n+13] == L'&' || path[n+13] == L'#');
}
HRESULT WinError() {
    const auto error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}
struct DeviceList {
    HDEVINFO handle = INVALID_HANDLE_VALUE;
    ~DeviceList() { if (handle != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(handle); }
};
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
HRESULT Detail(HDEVINFO list, SP_DEVICE_INTERFACE_DATA& iface,
               SP_DEVINFO_DATA& info, std::wstring& path) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(list, &iface, nullptr, 0, &needed, nullptr);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) ||
        needed > 65536) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    std::vector<unsigned char> bytes(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());
    detail->cbSize = sizeof(*detail);
    info = {}; info.cbSize = sizeof(info);
    if (!SetupDiGetDeviceInterfaceDetailW(list, &iface, detail, needed, nullptr, &info)) return WinError();
    const size_t capacity = (needed - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) / sizeof(wchar_t);
    const size_t length = wcsnlen_s(detail->DevicePath, capacity);
    if (!length || length == capacity) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    path.assign(detail->DevicePath, length);
    return S_OK;
}
HRESULT Container(HDEVINFO list, SP_DEVINFO_DATA& info, GUID& container) {
    DEVPROPTYPE type = 0;
    DWORD size = 0;
    if (!SetupDiGetDevicePropertyW(list, &info, &DEVPKEY_Device_ContainerId, &type,
        reinterpret_cast<BYTE*>(&container), sizeof(container), &size, 0)) return WinError();
    if (type != DEVPROP_TYPE_GUID || size != sizeof(container) || container == GUID_NULL)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return S_OK;
}
HRESULT DescribePath(std::wstring_view path, GUID& container) {
    if (path.empty() || path.find(L'\0') != std::wstring_view::npos)
        return E_INVALIDARG;
    DeviceList list{SetupDiCreateDeviceInfoList(nullptr, nullptr)};
    if (list.handle == INVALID_HANDLE_VALUE) return WinError();
    SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
    const std::wstring terminated(path);
    if (!SetupDiOpenDeviceInterfaceW(list.handle, terminated.c_str(), 0, &iface)) return WinError();
    SP_DEVINFO_DATA info{};
    std::wstring actual;
    HRESULT hr = Detail(list.handle, iface, info, actual);
    if (hr != S_OK) return hr;
    if (_wcsicmp(actual.c_str(), terminated.c_str()) != 0) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return Container(list.handle, info, container);
}
bool Attributes(HANDLE handle, const HidToneMappingDevice& device) {
    HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
    return HidD_GetAttributes(handle, &attr) && attr.VendorID == device.vendor && attr.ProductID == device.product;
}
class WindowsHidAccess final : public ElgatoHidAccess {
public:
    HRESULT DescribeSelected(std::wstring_view path, HidToneMappingDevice& device) override {
        if (!ParseUsbId(path, device.vendor, device.product) || !IsKnownElgatoHidProduct(device.vendor, device.product))
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        return DescribePath(path, device.container);
    }
    HRESULT FindCandidates(const HidToneMappingDevice& selected, std::vector<HidToneMappingDevice>& devices) override {
        GUID hidGuid{}; HidD_GetHidGuid(&hidGuid);
        DeviceList list{SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
        if (list.handle == INVALID_HANDLE_VALUE) return WinError();
        // Enumerate identity metadata only. Open handles only inside the selected
        // physical container, never every Elgato/HID device on the machine.
        for (DWORD i = 0; i < 4096; ++i) {
            SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(list.handle, nullptr, &hidGuid, i, &iface))
                return GetLastError() == ERROR_NO_MORE_ITEMS ? S_OK : WinError();
            SP_DEVINFO_DATA info{};
            HidToneMappingDevice candidate;
            HRESULT hr = Detail(list.handle, iface, info, candidate.path);
            if (hr != S_OK) return hr;
            if (!ParseUsbId(candidate.path, candidate.vendor, candidate.product, true) ||
                candidate.vendor != selected.vendor || candidate.product != selected.product) continue;
            hr = Container(list.handle, info, candidate.container);
            if (hr != S_OK) return hr;
            if (candidate.container != selected.container) continue;
            File file{CreateFileW(candidate.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
            if (file.handle == INVALID_HANDLE_VALUE) return WinError();
            if (!Attributes(file.handle, candidate)) continue;
            PHIDP_PREPARSED_DATA preparsed = nullptr;
            if (!HidD_GetPreparsedData(file.handle, &preparsed)) return WinError();
            HIDP_CAPS caps{};
            const auto status = HidP_GetCaps(preparsed, &caps);
            HidD_FreePreparsedData(preparsed);
            if (status != HIDP_STATUS_SUCCESS) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            candidate.reportBytes = caps.OutputReportByteLength;
            devices.push_back(std::move(candidate));
        }
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    HRESULT Write(const HidToneMappingDevice& device, std::span<const uint8_t> report) override {
        File file{CreateFileW(device.path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
        if (file.handle == INVALID_HANDLE_VALUE) return WinError();
        GUID currentContainer{};
        const HRESULT hr = DescribePath(device.path, currentContainer);
        if (hr != S_OK) return hr;
        if (currentContainer != device.container || !Attributes(file.handle, device))
            return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(file.handle, &preparsed)) return WinError();
        HIDP_CAPS caps{};
        const auto status = HidP_GetCaps(preparsed, &caps);
        HidD_FreePreparsedData(preparsed);
        if (status != HIDP_STATUS_SUCCESS || caps.OutputReportByteLength != report.size())
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // No retry after a write: success means transport accepted it, not that
        // the firmware actually changed the encoding.
        return HidD_SetOutputReport(file.handle, const_cast<uint8_t*>(report.data()),
            static_cast<ULONG>(report.size())) ? S_OK : WinError();
    }
};
}
bool IsKnownElgatoHidProduct(unsigned short vendor, unsigned short product) {
    return vendor == 0x0fd9 && (product == 0x006a || product == 0x0082 || product == 0x008a);
}
bool SameCaptureContainer(const HidToneMappingDevice& selected, const HidToneMappingDevice& candidate) {
    return selected.container != GUID_NULL && selected.container == candidate.container &&
        selected.vendor == candidate.vendor && selected.product == candidate.product;
}
std::vector<uint8_t> ElgatoToneMappingReport(unsigned short product, unsigned short bytes, bool enable) {
    if (!IsKnownElgatoHidProduct(0x0fd9, product)) return {};
    const bool old = product == 0x006a;
    if (bytes < (old ? 5 : 7) || bytes > 4096) return {};
    std::vector<uint8_t> report(bytes, 0);
    if (old) {
        report[0]=11; report[1]=0x55; report[2]=0x0a; report[3]=1; report[4]=enable ? 1 : 0;
    } else {
        report[0]=6; report[1]=6; report[2]=6; report[3]=0x55; report[4]=2; report[5]=0x0a; report[6]=enable ? 1 : 0;
    }
    return report;
}
ToneMappingResult ConfigureElgatoHidToneMapping(std::wstring_view path, bool enable, ElgatoHidAccess& access) {
    ToneMappingResult result;
    result.enable = enable; result.vendor = ToneMappingVendor::ElgatoHid;
    unsigned short vendor = 0, product = 0;
    if (path.find(L'\0') != std::wstring_view::npos || !ParseUsbId(path, vendor, product) ||
        !IsKnownElgatoHidProduct(vendor, product)) return result;
    HidToneMappingDevice selected;
    result.result = access.DescribeSelected(path, selected);
    result.status = ToneMappingStatus::QueryFailed;
    if (result.result != S_OK) return result;
    if (selected.container == GUID_NULL || selected.vendor != vendor || selected.product != product) {
        result.result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA); return result;
    }
    std::vector<HidToneMappingDevice> candidates;
    result.result = access.FindCandidates(selected, candidates);
    if (result.result != S_OK) return result;
    const HidToneMappingDevice* chosen = nullptr;
    std::vector<uint8_t> report;
    for (const auto& candidate : candidates) {
        if (!SameCaptureContainer(selected, candidate)) continue;
        if (candidate.path.empty() || candidate.path.find(L'\0') != std::wstring::npos) {
            result.result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA); return result;
        }
        auto bytes = ElgatoToneMappingReport(product, candidate.reportBytes, enable);
        if (bytes.empty()) continue;
        if (chosen) {
            result.status = ToneMappingStatus::AmbiguousDevice;
            result.result = HRESULT_FROM_WIN32(ERROR_DUP_NAME); return result;
        }
        chosen = &candidate; report = std::move(bytes);
    }
    if (!chosen) {
        result.status = ToneMappingStatus::Unsupported;
        result.result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return result;
    }
    result.result = access.Write(*chosen, report);
    result.status = result.result == S_OK ? ToneMappingStatus::Applied : ToneMappingStatus::SetFailed;
    return result;
}
ToneMappingResult ConfigureElgatoHidToneMapping(std::wstring_view path, bool enable) {
    WindowsHidAccess access;
    return ConfigureElgatoHidToneMapping(path, enable, access);
}
}
