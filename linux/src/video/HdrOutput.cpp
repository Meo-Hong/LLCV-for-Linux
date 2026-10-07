#include "video/HdrOutput.h"

#include "diagnostics/Logger.h"
#include "platform/Strings.h"

#ifdef LLCV_HAVE_COLOR_MANAGEMENT
#include <unistd.h>
#include <wayland-client.h>

#include "color-management-v1-client-protocol.h"
#endif

#include <algorithm>
#include <cstring>

namespace llcv::video {

#ifdef LLCV_HAVE_COLOR_MANAGEMENT

struct HdrOutputState {
    wl_display* display = nullptr;
    wl_surface* surface = nullptr;
    wl_event_queue* queue = nullptr;
    wl_display* wrapper = nullptr;
    wl_registry* registry = nullptr;
    wp_color_manager_v1* manager = nullptr;
    uint32_t version = 0;
    wp_color_management_surface_v1* colorSurface = nullptr;
    wp_color_management_surface_feedback_v1* feedback = nullptr;
    bool parametric = false;
    bool perceptual = false;
    bool pq = false;
    bool bt2020 = false;
    bool preferredChanged = false;
    bool active = false;
};

namespace {

#ifdef WP_IMAGE_DESCRIPTION_V1_READY2_SINCE_VERSION
constexpr uint32_t kMaxManagerVersion = 2;
#else
constexpr uint32_t kMaxManagerVersion = 1;
#endif

struct DescriptionWait {
    bool ready = false;
    bool failed = false;
    uint32_t cause = 0;
};

struct InformationWait {
    bool done = false;
    uint32_t transfer = 0;
    uint32_t maxLuminance = 0;
    uint32_t referenceLuminance = 0;
    uint32_t targetMaxLuminance = 0;
};

HdrOutputState* State(void* data) {
    return static_cast<HdrOutputState*>(data);
}

void OnSupportedIntent(void* data, wp_color_manager_v1*, uint32_t intent) {
    if (intent == WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL) State(data)->perceptual = true;
}

void OnSupportedFeature(void* data, wp_color_manager_v1*, uint32_t feature) {
    if (feature == WP_COLOR_MANAGER_V1_FEATURE_PARAMETRIC) State(data)->parametric = true;
}

void OnSupportedTransfer(void* data, wp_color_manager_v1*, uint32_t transfer) {
    if (transfer == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ) State(data)->pq = true;
}

void OnSupportedPrimaries(void* data, wp_color_manager_v1*, uint32_t primaries) {
    if (primaries == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020) State(data)->bt2020 = true;
}

void OnManagerDone(void*, wp_color_manager_v1*) {}

const wp_color_manager_v1_listener kManagerListener = {
    .supported_intent = OnSupportedIntent,
    .supported_feature = OnSupportedFeature,
    .supported_tf_named = OnSupportedTransfer,
    .supported_primaries_named = OnSupportedPrimaries,
    .done = OnManagerDone,
};

void OnGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    HdrOutputState* state = State(data);
    if (state->manager || std::strcmp(interface, wp_color_manager_v1_interface.name) != 0) return;
    state->version = std::min(version, kMaxManagerVersion);
    state->manager = static_cast<wp_color_manager_v1*>(
        wl_registry_bind(registry, name, &wp_color_manager_v1_interface, state->version));
    wp_color_manager_v1_add_listener(state->manager, &kManagerListener, state);
}

void OnGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {
    .global = OnGlobal,
    .global_remove = OnGlobalRemove,
};

void OnPreferredChanged(void* data, wp_color_management_surface_feedback_v1*, uint32_t) {
    State(data)->preferredChanged = true;
}

#ifdef WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_PREFERRED_CHANGED2_SINCE_VERSION
void OnPreferredChanged2(void* data, wp_color_management_surface_feedback_v1*, uint32_t, uint32_t) {
    State(data)->preferredChanged = true;
}
#endif

const wp_color_management_surface_feedback_v1_listener kFeedbackListener = {
    .preferred_changed = OnPreferredChanged,
#ifdef WP_COLOR_MANAGEMENT_SURFACE_FEEDBACK_V1_PREFERRED_CHANGED2_SINCE_VERSION
    .preferred_changed2 = OnPreferredChanged2,
#endif
};

void OnDescriptionFailed(void* data, wp_image_description_v1*, uint32_t cause, const char* message) {
    auto* wait = static_cast<DescriptionWait*>(data);
    wait->failed = true;
    wait->cause = cause;
    diagnostics::Log("[hdr] image description failed (%u): %s", cause, message ? message : "");
}

void OnDescriptionReady(void* data, wp_image_description_v1*, uint32_t) {
    static_cast<DescriptionWait*>(data)->ready = true;
}

#ifdef WP_IMAGE_DESCRIPTION_V1_READY2_SINCE_VERSION
void OnDescriptionReady2(void* data, wp_image_description_v1*, uint32_t, uint32_t) {
    static_cast<DescriptionWait*>(data)->ready = true;
}
#endif

const wp_image_description_v1_listener kDescriptionListener = {
    .failed = OnDescriptionFailed,
    .ready = OnDescriptionReady,
#ifdef WP_IMAGE_DESCRIPTION_V1_READY2_SINCE_VERSION
    .ready2 = OnDescriptionReady2,
#endif
};

InformationWait* Information(void* data) {
    return static_cast<InformationWait*>(data);
}

void OnInfoDone(void* data, wp_image_description_info_v1*) {
    Information(data)->done = true;
}

void OnInfoIcc(void*, wp_image_description_info_v1*, int32_t descriptor, uint32_t) {
    if (descriptor >= 0) close(descriptor);
}

void OnInfoPrimaries(void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                     int32_t, int32_t) {}

void OnInfoPrimariesNamed(void*, wp_image_description_info_v1*, uint32_t) {}

void OnInfoTfPower(void*, wp_image_description_info_v1*, uint32_t) {}

void OnInfoTfNamed(void* data, wp_image_description_info_v1*, uint32_t transfer) {
    Information(data)->transfer = transfer;
}

void OnInfoLuminances(void* data, wp_image_description_info_v1*, uint32_t, uint32_t maxLuminance,
                      uint32_t referenceLuminance) {
    Information(data)->maxLuminance = maxLuminance;
    Information(data)->referenceLuminance = referenceLuminance;
}

void OnInfoTargetLuminance(void* data, wp_image_description_info_v1*, uint32_t, uint32_t maxLuminance) {
    Information(data)->targetMaxLuminance = maxLuminance;
}

void OnInfoUnsigned(void*, wp_image_description_info_v1*, uint32_t) {}

const wp_image_description_info_v1_listener kInfoListener = {
    .done = OnInfoDone,
    .icc_file = OnInfoIcc,
    .primaries = OnInfoPrimaries,
    .primaries_named = OnInfoPrimariesNamed,
    .tf_power = OnInfoTfPower,
    .tf_named = OnInfoTfNamed,
    .luminances = OnInfoLuminances,
    .target_primaries = OnInfoPrimaries,
    .target_luminance = OnInfoTargetLuminance,
    .target_max_cll = OnInfoUnsigned,
    .target_max_fall = OnInfoUnsigned,
};

template <typename Finished>
void WaitFor(HdrOutputState& state, Finished finished) {
    for (int attempt = 0; attempt < 5 && !finished(); ++attempt) {
        if (wl_display_roundtrip_queue(state.display, state.queue) < 0) break;
    }
}

void Release(HdrOutputState& state) {
    if (state.colorSurface) wp_color_management_surface_v1_destroy(state.colorSurface);
    if (state.feedback) wp_color_management_surface_feedback_v1_destroy(state.feedback);
    if (state.manager) wp_color_manager_v1_destroy(state.manager);
    if (state.registry) wl_registry_destroy(state.registry);
    if (state.wrapper) wl_proxy_wrapper_destroy(state.wrapper);
    if (state.display) wl_display_flush(state.display);
    if (state.queue) wl_event_queue_destroy(state.queue);
    state = HdrOutputState{};
}

const char* YesNo(bool value) {
    return value ? "yes" : "no";
}

}

HdrOutput::HdrOutput() = default;

HdrOutput::~HdrOutput() {
    Shutdown();
}

bool HdrOutput::Compiled() {
    return true;
}

bool HdrOutput::Initialize(SDL_Window* window) {
    Shutdown();
    if (!window) return false;
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    auto* display = static_cast<wl_display*>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr));
    auto* surface = static_cast<wl_surface*>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
    if (!display || !surface) return false;

    auto state = std::make_unique<HdrOutputState>();
    state->display = display;
    state->surface = surface;
    state->queue = wl_display_create_queue(display);
    state->wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    if (!state->queue || !state->wrapper) {
        Release(*state);
        return false;
    }
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(state->wrapper), state->queue);
    state->registry = wl_display_get_registry(state->wrapper);
    wl_registry_add_listener(state->registry, &kRegistryListener, state.get());
    wl_display_roundtrip_queue(display, state->queue);
    if (!state->manager) {
        Release(*state);
        diagnostics::Log("[hdr] compositor does not offer wp_color_manager_v1");
        return false;
    }
    wl_display_roundtrip_queue(display, state->queue);
    if (state->parametric) {
        state->feedback = wp_color_manager_v1_get_surface_feedback(state->manager, surface);
        wp_color_management_surface_feedback_v1_add_listener(state->feedback, &kFeedbackListener, state.get());
    }
    state_ = std::move(state);
    diagnostics::Log("[hdr] %s", Capabilities().c_str());
    return true;
}

void HdrOutput::Shutdown() {
    if (!state_) return;
    Release(*state_);
    state_.reset();
}

bool HdrOutput::Connected() const {
    return state_ && state_->manager;
}

bool HdrOutput::Supported() const {
    return Connected() && state_->parametric && state_->pq && state_->bt2020 && state_->perceptual;
}

bool HdrOutput::Active() const {
    return state_ && state_->active;
}

std::string HdrOutput::Capabilities() const {
    if (!Connected()) return "wp_color_management_v1 unavailable";
    return platform::Format("wp_color_management_v1 v%u, parametric %s, PQ %s, BT.2020 %s, perceptual %s",
                            state_->version, YesNo(state_->parametric), YesNo(state_->pq),
                            YesNo(state_->bt2020), YesNo(state_->perceptual));
}

PreferredColor HdrOutput::QueryPreferred() {
    PreferredColor result;
    if (!state_ || !state_->feedback) return result;
    DescriptionWait wait;
    wp_image_description_v1* description =
        wp_color_management_surface_feedback_v1_get_preferred_parametric(state_->feedback);
    wp_image_description_v1_add_listener(description, &kDescriptionListener, &wait);
    WaitFor(*state_, [&] { return wait.ready || wait.failed; });
    if (wait.ready) {
        InformationWait information;
        wp_image_description_info_v1* info = wp_image_description_v1_get_information(description);
        wp_image_description_info_v1_add_listener(info, &kInfoListener, &information);
        WaitFor(*state_, [&] { return information.done; });
        wp_image_description_info_v1_destroy(info);
        result.known = information.done;
        result.transfer = information.transfer;
        result.maxLuminance = information.maxLuminance;
        result.referenceLuminance = information.referenceLuminance;
        result.targetMaxLuminance = information.targetMaxLuminance;
        const bool hdrTransfer = information.transfer == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ ||
                                 information.transfer == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
        const uint32_t reference = information.referenceLuminance ? information.referenceLuminance : 203;
        const bool headroom = information.targetMaxLuminance >= 400 &&
                              information.targetMaxLuminance * 2 >= reference * 3;
        result.hdr = result.known && (hdrTransfer || headroom);
    }
    wp_image_description_v1_destroy(description);
    wl_display_flush(state_->display);
    return result;
}

bool HdrOutput::TakePreferredChanged() {
    if (!state_) return false;
    wl_display_dispatch_queue_pending(state_->display, state_->queue);
    const bool changed = state_->preferredChanged;
    state_->preferredChanged = false;
    return changed;
}

bool HdrOutput::Enable() {
    if (!Supported()) return false;
    if (state_->active) return true;
    if (!state_->colorSurface) {
        state_->colorSurface = wp_color_manager_v1_get_surface(state_->manager, state_->surface);
    }
    wp_image_description_creator_params_v1* creator = wp_color_manager_v1_create_parametric_creator(state_->manager);
    wp_image_description_creator_params_v1_set_tf_named(creator, WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ);
    wp_image_description_creator_params_v1_set_primaries_named(creator, WP_COLOR_MANAGER_V1_PRIMARIES_BT2020);
    wp_image_description_v1* description = wp_image_description_creator_params_v1_create(creator);
    DescriptionWait wait;
    wp_image_description_v1_add_listener(description, &kDescriptionListener, &wait);
    WaitFor(*state_, [&] { return wait.ready || wait.failed; });
    if (!wait.ready) {
        wp_image_description_v1_destroy(description);
        wl_display_flush(state_->display);
        diagnostics::Log("[hdr] PQ/BT.2020 image description was not accepted");
        return false;
    }
    wp_color_management_surface_v1_set_image_description(state_->colorSurface, description,
                                                         WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);
    wp_image_description_v1_destroy(description);
    wl_display_flush(state_->display);
    state_->active = true;
    diagnostics::Log("[hdr] surface image description set to PQ/BT.2020");
    return true;
}

void HdrOutput::Disable() {
    if (!state_ || !state_->active) return;
    wp_color_management_surface_v1_unset_image_description(state_->colorSurface);
    wl_display_flush(state_->display);
    state_->active = false;
    diagnostics::Log("[hdr] surface image description cleared");
}

#else

struct HdrOutputState {};

HdrOutput::HdrOutput() = default;
HdrOutput::~HdrOutput() = default;

bool HdrOutput::Compiled() {
    return false;
}

bool HdrOutput::Initialize(SDL_Window*) {
    return false;
}

void HdrOutput::Shutdown() {}

bool HdrOutput::Connected() const {
    return false;
}

bool HdrOutput::Supported() const {
    return false;
}

bool HdrOutput::Active() const {
    return false;
}

std::string HdrOutput::Capabilities() const {
    return "built without wp_color_management_v1";
}

PreferredColor HdrOutput::QueryPreferred() {
    return {};
}

bool HdrOutput::TakePreferredChanged() {
    return false;
}

bool HdrOutput::Enable() {
    return false;
}

void HdrOutput::Disable() {}

#endif

}
