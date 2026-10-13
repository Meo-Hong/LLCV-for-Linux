#include "platform/SleepInhibitor.h"

#include "diagnostics/Logger.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>

#if defined(LLCV_HAVE_DBUS)
#include <dbus/dbus.h>
#endif

namespace llcv::platform {
namespace {

#if defined(LLCV_HAVE_DBUS)
constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr dbus_uint32_t kInhibitSuspend = 4;
constexpr int kPortalTimeoutMs = 3000;

DBusMessage* BuildInhibitCall(const std::string& reason) {
    DBusMessage* call =
        dbus_message_new_method_call(kPortalService, kPortalPath, "org.freedesktop.portal.Inhibit", "Inhibit");
    if (!call) return nullptr;
    const char* window = "";
    const dbus_uint32_t flags = kInhibitSuspend;
    const char* key = "reason";
    const char* value = reason.c_str();
    DBusMessageIter arguments;
    DBusMessageIter options;
    DBusMessageIter entry;
    DBusMessageIter variant;
    dbus_message_iter_init_append(call, &arguments);
    const bool built = dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &window) &&
                       dbus_message_iter_append_basic(&arguments, DBUS_TYPE_UINT32, &flags) &&
                       dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY, "{sv}", &options) &&
                       dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry) &&
                       dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key) &&
                       dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant) &&
                       dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value) &&
                       dbus_message_iter_close_container(&entry, &variant) &&
                       dbus_message_iter_close_container(&options, &entry) &&
                       dbus_message_iter_close_container(&arguments, &options);
    if (!built) {
        dbus_message_unref(call);
        return nullptr;
    }
    return call;
}
#endif

}

SleepInhibitor::~SleepInhibitor() {
    Release();
}

bool SleepInhibitor::BlocksSuspend() {
#if defined(LLCV_HAVE_DBUS)
    return true;
#else
    return false;
#endif
}

void SleepInhibitor::Acquire(bool keepDisplayOn, std::string reason) {
    if (active_ && displayRequested_ == keepDisplayOn) return;
    Release();
    active_ = true;
    displayRequested_ = keepDisplayOn;
    if (keepDisplayOn) {
        if (SDL_DisableScreenSaver()) {
            displayHeld_ = true;
        } else {
            diagnostics::Log("[power] could not keep the display on: %s", SDL_GetError());
        }
    }
#if defined(LLCV_HAVE_DBUS)
    releasing_ = false;
    worker_ = std::thread(&SleepInhibitor::HoldSuspendInhibit, this, std::move(reason));
#else
    (void)reason;
#endif
    diagnostics::Log("[power] sleep prevention on (%s)", keepDisplayOn ? "display and system" : "system only");
}

void SleepInhibitor::Release() {
    if (!active_) return;
    active_ = false;
    if (displayHeld_) {
        displayHeld_ = false;
        if (!SDL_EnableScreenSaver()) diagnostics::Log("[power] could not release the display: %s", SDL_GetError());
    }
    if (worker_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            releasing_ = true;
        }
        wake_.notify_all();
        worker_.join();
    }
    diagnostics::Log("[power] sleep prevention off");
}

void SleepInhibitor::HoldSuspendInhibit(std::string reason) {
#if defined(LLCV_HAVE_DBUS)
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!connection) {
        diagnostics::Log("[power] session bus unavailable; automatic suspend is not blocked: %s",
                         dbus_error_is_set(&error) ? error.message : "unknown error");
        dbus_error_free(&error);
        return;
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);

    std::string handle;
    if (DBusMessage* call = BuildInhibitCall(reason)) {
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, call, kPortalTimeoutMs, &error);
        dbus_message_unref(call);
        if (reply) {
            const char* path = nullptr;
            if (dbus_message_get_args(reply, &error, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID) && path) {
                handle = path;
            }
            dbus_message_unref(reply);
        }
    }
    if (handle.empty()) {
        diagnostics::Log("[power] portal Inhibit failed; automatic suspend is not blocked: %s",
                         dbus_error_is_set(&error) ? error.message : "no request handle");
    } else {
        diagnostics::Log("[power] automatic suspend blocked through the desktop portal");
    }
    dbus_error_free(&error);

    {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return releasing_; });
    }

    if (!handle.empty()) {
        if (DBusMessage* close = dbus_message_new_method_call(kPortalService, handle.c_str(),
                                                              "org.freedesktop.portal.Request", "Close")) {
            dbus_connection_send(connection, close, nullptr);
            dbus_connection_flush(connection);
            dbus_message_unref(close);
        }
    }
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
#else
    (void)reason;
#endif
}

}
