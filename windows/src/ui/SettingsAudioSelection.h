#pragma once

#include <string>

namespace llcv::settings_ui {

// A list index is only meaningful inside the list that displayed it. Keep
// WASAPI endpoints and ASIO drivers independently, including unaccepted edits.
struct AudioOutputSelections {
    bool initialized = false;
    bool displayedAsio = false;
    std::wstring wasapiEndpoint;
    std::wstring asioDriver;

    void Initialize(const std::wstring& endpoint, const std::wstring& driver) {
        if (initialized) return;
        wasapiEndpoint = endpoint;
        asioDriver = driver;
        initialized = true;
    }
    void Remember(const std::wstring& identity) {
        (displayedAsio ? asioDriver : wasapiEndpoint) = identity;
    }
};

} // namespace llcv::settings_ui
