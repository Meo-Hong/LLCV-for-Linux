#include "ui/SettingsScreen.h"

#include "app/AppInfo.h"
#include "app/ModeSelection.h"
#include "platform/SleepInhibitor.h"
#include "platform/Strings.h"
#include "ui/Shortcuts.h"
#include "ui/Text.h"

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <iterator>

namespace llcv::ui {
namespace {

using settings::AppSettings;

float g_scale = 1.0f;
FontSet g_fonts;
const input::Bindings* g_bindings = &input::kDefaultBindings;

std::string Keys(const char* text) {
    return input::ExpandKeyLabels(text, *g_bindings);
}

ImU32 WithAlpha(ImU32 color, int alpha) {
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

bool BeginCard(const char* id, const char* title) {
    const Palette& palette = CurrentPalette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, palette.cardEdge);
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(g_fonts.semibold, 0.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 2.0f * g_scale));
    const bool table = ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingStretchProp);
    if (table) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 220.0f * g_scale);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    }
    return table;
}

void EndCard(bool table) {
    if (table) ImGui::EndTable();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0.0f, 6.0f * g_scale));
}

void Row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void ValueRow() {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(1);
}

void Note(const std::string& text, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void Note(const std::string& text) {
    Note(text, CurrentPalette().secondary);
}

bool Combo(const char* id, int& index, const std::vector<std::string>& items) {
    const char* preview = index >= 0 && index < static_cast<int>(items.size()) ? items[index].c_str() : "";
    bool changed = false;
    if (ImGui::BeginCombo(id, preview)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            const bool selected = i == index;
            if (ImGui::Selectable(items[i].c_str(), selected)) {
                index = i;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

template <typename E>
bool EnumCombo(const char* id, E& value, const std::vector<std::pair<E, std::string>>& options) {
    std::vector<std::string> labels;
    int index = 0;
    for (size_t i = 0; i < options.size(); ++i) {
        labels.push_back(options[i].second);
        if (options[i].first == value) index = static_cast<int>(i);
    }
    if (!Combo(id, index, labels)) return false;
    value = options[static_cast<size_t>(index)].first;
    return true;
}

float ButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void PathRow(const char* label, const std::string& path, const char* buttonLabel, bool& clicked) {
    Row(label);
    const float buttonWidth = ButtonWidth(buttonLabel);
    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float available = ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x;
    ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(available, 40.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, CurrentPalette().secondary);
    ImGui::TextWrapped("%s", path.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::SameLine(rightEdge - buttonWidth);
    clicked = ImGui::Button(buttonLabel);
}

constexpr const char* kCapture40Guide[] = {
    N_("Leave it off for normal viewing."),
    N_("When checked, 40 fps is requested instead of the frame-rate selection; unchecking restores that selection."),
    N_("Support is checked on Start. If the driver rejects it or connects at another rate, LLCV shows an error "
       "instead of falling back to another FPS."),
    N_("It does not interpolate frames, lock a game to 40 fps, or replace VRR."),
    N_("A 40 fps game can also be viewed with 120 fps capture, so it is not required just for 40 fps games."),
    N_("Check the measured input rate under Actual FPS in the Tab diagnostics."),
};

void HelpMarker(const char* const* lines, size_t count) {
    ImGui::TextDisabled("(?)");
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    for (size_t i = 0; i < count; ++i) {
        ImGui::BulletText("%s", Keys(T(lines[i])).c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

std::string FormatLabel(capture::PixelFormat format) {
    switch (format) {
    case capture::PixelFormat::Nv12: return "NV12 (4:2:0)";
    case capture::PixelFormat::Yuyv: return "YUY2 (4:2:2)";
    case capture::PixelFormat::Mjpeg:
        return T("MJPEG (compressed · high resolution/refresh)");
    case capture::PixelFormat::Bgr24: return "BGR24 (RGB)";
    case capture::PixelFormat::P010: return T("P010 (10-bit HDR10)");
    }
    return "?";
}

std::string PcmTargetLabel(int ms) {
    const char* suffix = "";
    switch (ms) {
    case 10: suffix = T(" (minimum latency)"); break;
    case 15: suffix = T(" (low-latency target)"); break;
    case 20: suffix = T(" (stability target)"); break;
    case 25: suffix = T(" (recommended · default)"); break;
    case 30: suffix = T(" (stability first)"); break;
    case 40: suffix = T(" (maximum stability)"); break;
    default: break;
    }
    return platform::Format("%d ms%s", ms, suffix);
}

std::string DeviceFramesLabel(int frames) {
    const double ms = frames * 1000.0 / 48000.0;
    const char* suffix = frames == 256 ? T(" (recommended)") : "";
    return platform::Format(T("%d frames · %.1f ms%s"), frames, ms, suffix);
}

}

SettingsAction SettingsScreen::Draw(AppSettings& settings, const SettingsContext& context) {
    action_ = SettingsAction::None;
    g_scale = context.scale;
    g_fonts = context.fonts;
    g_bindings = &settings.shortcuts;
    const Palette& palette = CurrentPalette();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const float footerPadding = 12.0f * g_scale;
    const float footerHeight = ImGui::GetFrameHeight() + footerPadding * 2.0f;
    const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);

    DrawSidebar(context, bodyHeight);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * g_scale, 20.0f * g_scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.background);
    ImGui::BeginChild("##content", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (!context.errorMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(palette.danger, 40));
        ImGui::PushStyleColor(ImGuiCol_Border, palette.danger);
        ImGui::BeginChild("##error", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding);
        Note(context.errorMessage, palette.danger);
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0.0f, 6.0f * g_scale));
    }

    switch (page_) {
    case Page::Video: DrawVideo(settings, context); break;
    case Page::Audio: DrawAudio(settings, context); break;
    case Page::Window: DrawWindow(settings, context); break;
    case Page::Guide: DrawGuide(settings, context); break;
    case Page::App: DrawApp(settings, context); break;
    }
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, bodyHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * g_scale, footerPadding));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.sidebar);
    ImGui::BeginChild("##footer", ImVec2(0.0f, footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    DrawFooter(context);
    ImGui::EndChild();

    ImGui::End();
    if (page_ != Page::Guide || action_ == SettingsAction::Start || action_ == SettingsAction::Quit) {
        CancelShortcutCapture();
    }
    return action_;
}

void SettingsScreen::DrawSidebar(const SettingsContext& context, float height) {
    const Palette& palette = CurrentPalette();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * g_scale, 18.0f * g_scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.sidebar);
    ImGui::BeginChild("##sidebar", ImVec2(210.0f * g_scale, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PushFont(context.fonts.semibold, 24.0f);
    ImGui::TextUnformatted(app::kAppName);
    ImGui::PopFont();
    Note(T("Low Latency Capture Viewer"));
    ImGui::Dummy(ImVec2(0.0f, 14.0f * g_scale));

    struct Item {
        Page page;
        const char* label;
    };
    const Item items[] = {
        {Page::Video, T("Video##video")},
        {Page::Audio, T("Audio##audio")},
        {Page::Window, T("Window##window")},
        {Page::Guide, T("Guide & logs##guide")},
        {Page::App, T("App##app")},
    };
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    for (const auto& item : items) {
        if (ImGui::Selectable(item.label, page_ == item.page, 0, ImVec2(0.0f, 34.0f * g_scale))) {
            page_ = item.page;
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void SettingsScreen::DrawFooter(const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonWidth = 112.0f * g_scale;
    const float buttonsWidth = buttonWidth * 2.0f + style.ItemSpacing.x;
    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const float textWidth = ImGui::GetContentRegionAvail().x - buttonsWidth - style.ItemSpacing.x * 2.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                        ImVec2(ImGui::GetCursorScreenPos().x + std::max(textWidth, 0.0f),
                               ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight()),
                        true);
    ImGui::PushStyleColor(ImGuiCol_Text, context.canStart ? palette.secondary : palette.warning);
    ImGui::TextUnformatted(context.startSummary.c_str());
    ImGui::PopStyleColor();
    ImGui::PopClipRect();

    ImGui::SameLine(rightEdge - buttonsWidth);
    if (ImGui::Button(T("Quit##quit"), ImVec2(buttonWidth, 0.0f))) {
        action_ = SettingsAction::Quit;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.canStart);
    ImGui::PushStyleColor(ImGuiCol_Button, palette.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.primaryHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.primaryPressed);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.onAccent);
    if (ImGui::Button(T("Start##start"), ImVec2(buttonWidth, 0.0f))) {
        action_ = SettingsAction::Start;
    }
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
}

void SettingsScreen::DrawVideo(AppSettings& s, const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const auto& devices = *context.captureDevices;

    bool table = BeginCard("##capture", T("Capture"));
    if (table) {
        Row(T("Capture device"));
        std::vector<std::string> names{T("Auto select (recommended)")};
        int index = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            names.push_back(devices[i].DisplayName());
            if (!s.captureDevice.empty() &&
                (devices[i].id == s.captureDevice || devices[i].node == s.captureDevice)) {
                index = static_cast<int>(i) + 1;
            }
        }
        if (!s.captureDevice.empty() && index == 0) {
            names.push_back(s.captureDevice + T(" (not connected)"));
            index = static_cast<int>(names.size()) - 1;
        }
        const char* refresh = T("Refresh##refresh");
        const float refreshWidth = ButtonWidth(refresh);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - refreshWidth - ImGui::GetStyle().ItemSpacing.x);
        if (Combo("##captureDevice", index, names)) {
            if (index == 0) {
                s.captureDevice.clear();
            } else if (index <= static_cast<int>(devices.size())) {
                s.captureDevice = devices[static_cast<size_t>(index - 1)].id;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(refresh)) action_ = SettingsAction::RefreshDevices;

        const capture::CaptureDevice* device = capture::SelectDevice(devices, s.captureDevice);
        if (!device) {
            ValueRow();
            Note(devices.empty()
                     ? T("No capture device found. Check the USB connection and select Refresh.")
                     : T("The selected device is not connected."),
                 palette.warning);
        } else {
            const auto resolutions = capture::Resolutions(*device);
            Row(T("Capture resolution"));
            std::vector<std::string> sizeItems;
            int sizeIndex = -1;
            for (size_t i = 0; i < resolutions.size(); ++i) {
                sizeItems.push_back(platform::Format("%d × %d", resolutions[i].first, resolutions[i].second));
                if (resolutions[i].first == s.videoWidth && resolutions[i].second == s.videoHeight) {
                    sizeIndex = static_cast<int>(i);
                }
            }
            if (sizeIndex < 0) {
                sizeItems.push_back(platform::Format("%d × %d%s", s.videoWidth, s.videoHeight,
                                                     T(" (unsupported)")));
                sizeIndex = static_cast<int>(sizeItems.size()) - 1;
            }
            if (Combo("##resolution", sizeIndex, sizeItems) &&
                sizeIndex < static_cast<int>(resolutions.size())) {
                s.videoWidth = resolutions[static_cast<size_t>(sizeIndex)].first;
                s.videoHeight = resolutions[static_cast<size_t>(sizeIndex)].second;
            }

            Row(T("Pixel format"));
            const auto formats = capture::FormatsAt(*device, s.videoWidth, s.videoHeight);
            std::vector<std::string> formatItems{T("Auto (NV12 first · recommended)")};
            std::vector<settings::PixelFormatChoice> formatChoices{settings::PixelFormatChoice::Auto};
            for (const auto format : formats) {
                formatItems.push_back(FormatLabel(format));
                formatChoices.push_back(app::ChoiceFromFormat(format));
            }
            int formatIndex = -1;
            for (size_t i = 0; i < formatChoices.size(); ++i) {
                if (formatChoices[i] == s.pixelFormat) formatIndex = static_cast<int>(i);
            }
            if (formatIndex < 0) {
                const auto format = app::FormatFromChoice(s.pixelFormat);
                formatItems.push_back(std::string(format ? capture::PixelFormatName(*format) : "?") +
                                      T(" (unsupported)"));
                formatChoices.push_back(s.pixelFormat);
                formatIndex = static_cast<int>(formatItems.size()) - 1;
            }
            if (Combo("##pixelFormat", formatIndex, formatItems)) {
                s.pixelFormat = formatChoices[static_cast<size_t>(formatIndex)];
            }

            Row(T("Frame rate"));
            const auto rates = capture::RatesAt(*device, s.videoWidth, s.videoHeight,
                                                app::FormatFromChoice(s.pixelFormat));
            ImGui::BeginDisabled(s.capture40Fps);
            std::vector<std::string> rateItems{T("Auto (highest uncompressed rate)")};
            std::vector<int> rateValues{0};
            int rateIndex = s.frameRateMilli == 0 ? 0 : -1;
            for (const auto& rate : rates) {
                rateItems.push_back(capture::FormatRate(rate) + " fps");
                rateValues.push_back(rate.Milli());
                if (s.frameRateMilli > 0 && std::abs(rate.Milli() - s.frameRateMilli) <= 10) {
                    rateIndex = static_cast<int>(rateValues.size()) - 1;
                }
            }
            if (rateIndex < 0) {
                rateItems.push_back(platform::Format("%.2f fps%s", s.frameRateMilli / 1000.0,
                                                     T(" (unsupported)")));
                rateValues.push_back(s.frameRateMilli);
                rateIndex = static_cast<int>(rateItems.size()) - 1;
            }
            if (Combo("##frameRate", rateIndex, rateItems)) {
                s.frameRateMilli = rateValues[static_cast<size_t>(rateIndex)];
            }
            ImGui::EndDisabled();

            ValueRow();
            ImGui::Checkbox(T("40 fps capture"), &s.capture40Fps);
            ImGui::SameLine();
            HelpMarker(kCapture40Guide, std::size(kCapture40Guide));
            if (s.capture40Fps) {
                const bool listed = std::any_of(rates.begin(), rates.end(), capture::IsFortyFps);
                ValueRow();
                if (listed) {
                    Note(T("40 fps is requested instead of the frame-rate selection. Uncheck to restore it."));
                } else {
                    Note(T("40 fps is not in the detected list. Start checks whether the driver accepts it; "
                           "if not, LLCV shows an error instead of using another rate."),
                         palette.warning);
                }
            }

            ValueRow();
            if (const auto mode = capture::ResolveMode(*device, app::RequestFromSettings(s))) {
                Note(platform::Format(T("Selected mode: %d × %d · %s · %s fps"),
                                      mode->width, mode->height, capture::PixelFormatName(mode->format),
                                      capture::FormatRate(mode->rate).c_str()));
            } else {
                Note(T("No supported mode: choose another resolution or format."),
                     palette.warning);
            }
        }
    }
    EndCard(table);

    table = BeginCard("##display", T("Display"));
    if (table) {
        Row(T("Presentation mode"));
        EnumCombo<settings::PresentationMode>(
            "##presentation", s.presentationMode,
            {{settings::PresentationMode::Immediate, T("Immediate (minimum latency · recommended)")},
             {settings::PresentationMode::VSync, T("VSync (reduced tearing)")}});
        Row(T("Scaling mode"));
        EnumCombo<settings::ScalingMode>("##scaling", s.scalingMode,
                                         {{settings::ScalingMode::Smooth, T("Smooth")},
                                          {settings::ScalingMode::Sharp, T("Sharp")}});
        Row(T("Color interpretation"));
        EnumCombo<video::ColorOverride>("##color", s.colorOverride,
                                        {{video::ColorOverride::Auto, T("Auto (recommended)")},
                                         {video::ColorOverride::Bt709Limited, "BT.709 Limited"},
                                         {video::ColorOverride::Bt709Full, "BT.709 Full"},
                                         {video::ColorOverride::Bt601Limited, "BT.601 Limited"},
                                         {video::ColorOverride::Bt601Full, "BT.601 Full"}});
    }
    EndCard(table);

    DrawHdr(s, context);

    table = BeginCard("##screenshot", Keys(T("Screenshots (F12)")).c_str());
    if (table) {
        bool open = false;
        PathRow(T("Save location"), context.screenshotDirectory,
                T("Open folder##shots"), open);
        if (open) action_ = SettingsAction::OpenScreenshotFolder;
        ValueRow();
        ImGui::Checkbox(T("Also copy the PNG to the clipboard"), &s.screenshotClipboard);
        ValueRow();
        Note(T("Saved as PNG at the capture resolution."));
    }
    EndCard(table);
}

void SettingsScreen::DrawHdr(AppSettings& s, const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const bool table = BeginCard("##hdr", "HDR");
    if (table) {
        Row(T("HDR input interpretation"));
        EnumCombo<video::HdrInput>(
            "##hdrInput", s.hdrInput,
            {{video::HdrInput::Auto, T("Auto (P010 = HDR10 · recommended)")},
             {video::HdrInput::ForceHdr10, T("Force HDR10 (8-bit input as PQ/BT.2020)")},
             {video::HdrInput::ForceSdr, T("Treat as SDR")}});
        Row(T("HDR presentation"));
        EnumCombo<settings::HdrOutputMode>(
            "##hdrOutput", s.hdrOutput,
            {{settings::HdrOutputMode::Auto, T("Auto (HDR output on HDR displays, else tone mapping)")},
             {settings::HdrOutputMode::ToneMap, T("Always SDR tone mapping")},
             {settings::HdrOutputMode::Hdr, T("Always HDR output (Wayland)")}});
        Row(T("SDR reference white"));
        std::vector<std::pair<int, std::string>> whiteOptions;
        for (const int nits : settings::kSdrWhiteOptions) {
            whiteOptions.emplace_back(nits, platform::Format("%d nit%s", nits,
                                                             nits == 203 ? T(" (recommended · BT.2408)") : ""));
        }
        EnumCombo<int>("##sdrWhite", s.sdrWhiteNits, whiteOptions);
        ValueRow();
        Note(T("Sets tone-mapping brightness and UI brightness during HDR output."));

        const HdrSupport& hdr = context.hdr;
        if (!hdr.input && !hdr.output) {
            ValueRow();
            Note(T("HDR is not supported in this environment. HDR signals are converted to SDR."),
                 palette.warning);
        }
        const auto status = [&](const char* label, bool supported, const std::string& detail) {
            Row(label);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, supported ? palette.text : palette.warning);
            ImGui::TextUnformatted(supported ? T("Supported") : T("Not supported"));
            ImGui::PopStyleColor();
            ValueRow();
            Note(detail, supported ? palette.secondary : palette.warning);
        };
        status(T("HDR input"), hdr.input, hdr.inputText);
        status(T("HDR output"), hdr.output, hdr.outputText);
        if (s.hdrOutput == settings::HdrOutputMode::ToneMap) {
            ValueRow();
            Note(T("If you switch to another mode, the 10-bit framebuffer applies from the next launch."));
        }
    }
    EndCard(table);
}

void SettingsScreen::DrawAudio(AppSettings& s, const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const auto& recording = *context.recordingDevices;
    const auto& playback = *context.playbackDevices;

    bool table = BeginCard("##audioCapture", T("Capture"));
    if (table) {
        Row(T("Capture audio device"));
        std::vector<std::string> items{
            T("Auto (same USB device as video · recommended)"),
            T("Disabled (video only)")};
        int index = 0;
        if (s.captureAudioDevice == settings::kCaptureAudioDisabled) index = 1;
        for (size_t i = 0; i < recording.size(); ++i) {
            items.push_back(recording[i].name);
            if (s.captureAudioDevice == recording[i].name) index = static_cast<int>(i) + 2;
        }
        if (!s.captureAudioDevice.empty() && s.captureAudioDevice != settings::kCaptureAudioDisabled &&
            index == 0) {
            items.push_back(s.captureAudioDevice + T(" (not connected)"));
            index = static_cast<int>(items.size()) - 1;
        }
        if (Combo("##captureAudio", index, items)) {
            if (index == 0) {
                s.captureAudioDevice.clear();
            } else if (index == 1) {
                s.captureAudioDevice = settings::kCaptureAudioDisabled;
            } else if (index - 2 < static_cast<int>(recording.size())) {
                s.captureAudioDevice = recording[static_cast<size_t>(index - 2)].name;
            }
        }
        if (s.captureAudioDevice.empty()) {
            ValueRow();
            if (context.autoCaptureAudio.empty()) {
                Note(T("Auto detection failed · select the capture audio device manually."),
                     palette.warning);
            } else {
                Note(std::string(T("Detected: ")) + context.autoCaptureAudio);
            }
        }
    }
    EndCard(table);

    table = BeginCard("##audioOutput", T("Output"));
    if (table) {
        Row(T("Audio output device"));
        std::vector<std::string> items{T("Follow the system default output (recommended)")};
        int index = 0;
        for (size_t i = 0; i < playback.size(); ++i) {
            items.push_back(playback[i].name);
            if (s.audioOutputDevice == playback[i].name) index = static_cast<int>(i) + 1;
        }
        if (!s.audioOutputDevice.empty() && index == 0) {
            items.push_back(s.audioOutputDevice + T(" (not connected)"));
            index = static_cast<int>(items.size()) - 1;
        }
        if (Combo("##audioOutput", index, items)) {
            if (index == 0) {
                s.audioOutputDevice.clear();
            } else if (index - 1 < static_cast<int>(playback.size())) {
                s.audioOutputDevice = playback[static_cast<size_t>(index - 1)].name;
            }
        }

        Row(T("Audio output buffer"));
        std::vector<std::pair<int, std::string>> frameOptions;
        for (const int frames : settings::kDeviceFrameOptions) frameOptions.emplace_back(frames, DeviceFramesLabel(frames));
        EnumCombo<int>("##deviceFrames", s.audioDeviceFrames, frameOptions);
        ValueRow();
        Note(platform::Format(T("Audio server: %s · the buffer is requested as the PipeWire graph quantum."),
                              context.audioDriver.empty() ? "?" : context.audioDriver.c_str()));
    }
    EndCard(table);

    table = BeginCard("##audioSync", T("Sync & stability"));
    if (table) {
        Row(T("PCM buffer target"));
        std::vector<std::pair<int, std::string>> targetOptions;
        for (const int ms : settings::kPcmTargetOptions) targetOptions.emplace_back(ms, PcmTargetLabel(ms));
        EnumCombo<int>("##pcmTarget", s.pcmQueueTargetMs, targetOptions);
        Row(T("Clock-drift correction"));
        EnumCombo<settings::DriftCorrection>(
            "##drift", s.driftCorrection,
            {{settings::DriftCorrection::Auto, T("Auto (recommended · correct only when needed)")},
             {settings::DriftCorrection::Off, T("Off (unaltered PCM · quality first)")}});
        ValueRow();
        Note(T("If sound breaks up occasionally, raise the PCM target in 5 ms steps."));
    }
    EndCard(table);

    table = BeginCard("##audioPlayback", T("Playback & convenience"));
    if (table) {
        Row(T("Volume"));
        ImGui::SliderInt("##volume", &s.volumePercent, 0, s.allowVolumeBoost ? 200 : 100, "%d%%");
        Row(T("Left / right volume"));
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(half);
        ImGui::SliderInt("##left", &s.leftVolumePercent, 0, 100, T("Left %d%%"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half);
        ImGui::SliderInt("##right", &s.rightVolumePercent, 0, 100, T("Right %d%%"));
        ValueRow();
        if (ImGui::Checkbox(T("Allow volume boost above 100% (up to 200%)"),
                            &s.allowVolumeBoost) &&
            !s.allowVolumeBoost) {
            s.volumePercent = std::min(s.volumePercent, 100);
        }
        ValueRow();
        ImGui::Checkbox(T("Mute automatically in background"), &s.muteWhenBackground);
        Row(T("Volume indicator position"));
        EnumCombo<settings::VolumeHudPosition>(
            "##hud", s.volumeHudPosition,
            {{settings::VolumeHudPosition::TopLeft, T("Top-left (default)")},
             {settings::VolumeHudPosition::TopRight, T("Top-right")},
             {settings::VolumeHudPosition::BottomLeft, T("Bottom-left")},
             {settings::VolumeHudPosition::BottomRight, T("Bottom-right")}});
        ValueRow();
        ImGui::Checkbox(T("Audio-only mode (sound without video)"), &s.audioOnly);
    }
    EndCard(table);
}

void SettingsScreen::DrawWindow(AppSettings& s, const SettingsContext& context) {
    bool table = BeginCard("##windowSize", T("Window size & display"));
    if (table) {
        ValueRow();
        ImGui::Checkbox(T("Pixel-perfect (1:1 source size · locked)"),
                        &s.pixelPerfect);
        ValueRow();
        Note(Keys(T("When off, the window can be resized freely while keeping the aspect ratio. F5 restores 1:1.")));
        ValueRow();
        ImGui::Checkbox(T("Borderless window"), &s.borderlessWindow);
        ValueRow();
        ImGui::Checkbox(T("Start in fullscreen"), &s.startFullscreen);
        Row(T("Fullscreen cursor"));
        EnumCombo<settings::FullscreenCursor>(
            "##cursor", s.fullscreenCursor,
            {{settings::FullscreenCursor::AutoHide, T("Auto-hide (recommended)")},
             {settings::FullscreenCursor::AlwaysVisible, T("Always visible")}});
    }
    EndCard(table);

    table = BeginCard("##monitor", T("Monitor"));
    if (table) {
        Row(T("Startup monitor"));
        const auto& displays = *context.displays;
        std::vector<std::string> items{T("Auto (last position)")};
        int index = 0;
        for (size_t i = 0; i < displays.size(); ++i) {
            items.push_back(displays[i].name);
            if (s.preferredDisplay == displays[i].name) index = static_cast<int>(i) + 1;
        }
        if (!s.preferredDisplay.empty() && index == 0) {
            items.push_back(s.preferredDisplay + T(" (not connected)"));
            index = static_cast<int>(items.size()) - 1;
        }
        if (Combo("##display", index, items)) {
            if (index == 0) {
                s.preferredDisplay.clear();
            } else if (index - 1 < static_cast<int>(displays.size())) {
                s.preferredDisplay = displays[static_cast<size_t>(index - 1)].name;
            }
        }
        ValueRow();
        if (context.videoDriver == "wayland") {
            Note(T("On Wayland the compositor places windows. The selected monitor applies to fullscreen. "
                   "Choose X11 under App → Display backend to position windows."));
        } else {
            Note(T("X11 mode: the startup monitor and last window position are restored."));
        }
    }
    EndCard(table);
}

void SettingsScreen::CancelShortcutCapture() {
    editingShortcut_ = -1;
    shortcutNotice_ = ShortcutNotice::None;
}

bool SettingsScreen::CaptureShortcut(AppSettings& settings, input::Binding binding) {
    if (editingShortcut_ < 0) return false;
    const size_t index = static_cast<size_t>(editingShortcut_);
    if (!input::IsValid(binding)) {
        shortcutNotice_ = ShortcutNotice::Reserved;
        return false;
    }
    for (size_t i = 0; i < input::kActionCount; ++i) {
        if (i != index && settings.shortcuts[i] == binding) {
            shortcutNotice_ = ShortcutNotice::Duplicate;
            return false;
        }
    }
    settings.shortcuts[index] = binding;
    CancelShortcutCapture();
    return true;
}

void SettingsScreen::DrawShortcuts(AppSettings& s) {
    const Palette& palette = CurrentPalette();
    const bool table = BeginCard("##shortcuts", T("Keyboard shortcuts"));
    if (!table) {
        EndCard(table);
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float keyWidth = 80.0f * g_scale;
    for (const input::Binding binding : s.shortcuts) {
        keyWidth = std::max(keyWidth, ImGui::CalcTextSize(input::Label(binding).c_str()).x + style.FramePadding.x * 2.0f +
                                          8.0f * g_scale);
    }

    bool keyHovered = false;
    for (const auto& shortcut : kShortcuts) {
        const int index = static_cast<int>(shortcut.action);
        const bool editing = editingShortcut_ == index;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(index);
        if (editing) {
            ImGui::PushStyleColor(ImGuiCol_Button, palette.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.primaryHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.primaryPressed);
            ImGui::PushStyleColor(ImGuiCol_Text, palette.onAccent);
        }
        const std::string label = editing ? std::string("…") : input::Label(s.shortcuts[static_cast<size_t>(index)]);
        const float width = std::min(keyWidth, ImGui::GetContentRegionAvail().x);
        if (ImGui::Button((label + "###key").c_str(), ImVec2(width, 0.0f))) {
            if (editing) {
                CancelShortcutCapture();
            } else {
                editingShortcut_ = index;
                shortcutNotice_ = ShortcutNotice::None;
            }
        }
        keyHovered |= ImGui::IsItemHovered();
        if (editing) ImGui::PopStyleColor(4);
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        std::string text = T(shortcut.text);
        if (shortcut.videoOnly) text += T(" (video mode)");
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", text.c_str());
    }

    Row(kMenuShortcut.key);
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s%s", T(kMenuShortcut.text),
                       T(" (fixed)"));

    ValueRow();
    if (editingShortcut_ < 0) {
        Note(T("Click a key to change it · Ctrl / Alt / Shift combinations are supported"));
    } else if (shortcutNotice_ == ShortcutNotice::Reserved) {
        Note(T("Reserved or unsupported key. Try another."),
             palette.warning);
    } else if (shortcutNotice_ == ShortcutNotice::Duplicate) {
        Note(T("Already assigned. Try another key."), palette.warning);
    } else {
        Note(T("Press a key, optionally with Ctrl / Alt / Shift · click the key button again to cancel."),
             palette.accent);
    }
    ValueRow();
    if (ImGui::Button(T("Reset defaults##shortcuts"))) {
        CancelShortcutCapture();
        s.shortcuts = input::kDefaultBindings;
    }
    keyHovered |= ImGui::IsItemHovered();
    if (editingShortcut_ >= 0 && !keyHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) CancelShortcutCapture();
    EndCard(table);
}

void SettingsScreen::DrawGuide(AppSettings& s, const SettingsContext& context) {
    DrawShortcuts(s);

    bool table = BeginCard("##mouse", T("Mouse"));
    if (table) {
        for (const auto& hint : kMouseHints) {
            Row(T(hint.gesture));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(T(hint.text));
        }
    }
    EndCard(table);

    table = BeginCard("##logs", T("Diagnostic logs"));
    if (table) {
        ValueRow();
        if (ImGui::Checkbox(T("Save diagnostic log"), &s.saveLog)) {
            action_ = SettingsAction::LogToggled;
        }
        bool open = false;
        PathRow(T("Log location"), context.logDirectory,
                T("Open logs folder##logs"), open);
        if (open) action_ = SettingsAction::OpenLogFolder;
    }
    EndCard(table);

    table = BeginCard("##capture40", T("40 fps capture"));
    if (table) {
        for (const auto& line : kCapture40Guide) {
            ValueRow();
            Note(Keys(T(line)));
        }
    }
    EndCard(table);

    table = BeginCard("##troubleshooting", T("Troubleshooting"));
    if (table) {
        const char* notes[] = {
            T("If there is no picture, close OBS or any other app that uses the capture device."),
            T("High-bandwidth modes such as 4K 60 fps may only be offered as MJPEG."),
            T("If video works but audio does not, check Audio → Capture audio device."),
            T("The Tab diagnostics overlay shows dropped frames and audio underruns."),
        };
        for (const char* note : notes) {
            ValueRow();
            Note(Keys(note));
        }
    }
    EndCard(table);
}

void SettingsScreen::DrawApp(AppSettings& s, const SettingsContext& context) {
    bool table = BeginCard("##language", T("Language"));
    if (table) {
        Row(T("Display language"));
        if (EnumCombo<settings::UiLanguage>("##uiLanguage", s.uiLanguage,
                                            {{settings::UiLanguage::Auto, T("Auto (system)")},
                                             {settings::UiLanguage::Korean, "한국어"},
                                             {settings::UiLanguage::English, "English"},
                                             {settings::UiLanguage::Japanese, "日本語"},
                                             {settings::UiLanguage::Spanish, "Español"},
                                             {settings::UiLanguage::Portuguese, "Português (Brasil)"}})) {
            action_ = SettingsAction::LanguageChanged;
        }
    }
    EndCard(table);

    table = BeginCard("##theme", T("App theme"));
    if (table) {
        Row(T("Theme"));
        bool light = s.lightTheme;
        if (EnumCombo<bool>("##themeCombo", light, {{false, T("Dark")}, {true, T("Light")}})) {
            s.lightTheme = light;
            action_ = SettingsAction::ThemeChanged;
        }
        ValueRow();
        Note(T("Overlays drawn over video stay dark for contrast."));
    }
    EndCard(table);

    table = BeginCard("##backend", T("Display backend"));
    if (table) {
        Row(T("Backend"));
        EnumCombo<settings::DisplayBackend>(
            "##displayBackend", s.displayBackend,
            {{settings::DisplayBackend::Auto, T("Auto (Wayland first)")},
             {settings::DisplayBackend::Wayland, "Wayland"},
             {settings::DisplayBackend::X11, "X11 (XWayland)"}});
        ValueRow();
        Note(platform::Format(T("Current: %s · changes apply on the next launch. You can also run llcv --x11."),
                              context.videoDriver.c_str()));
    }
    EndCard(table);

    table = BeginCard("##startup", T("Startup"));
    if (table) {
        ValueRow();
        ImGui::Checkbox(Keys(T("Skip this settings screen on next launch (F2 reopens it)###skip"))
                            .c_str(),
                        &s.skipStartupSettings);
    }
    EndCard(table);

    table = BeginCard("##power", T("Power"));
    if (table) {
        ValueRow();
        ImGui::Checkbox(T("Prevent sleep during playback"), &s.preventSleep);
        ValueRow();
        Note(T("Video keeps the screen on. Audio-only allows the screen to turn off. "
               "Released when playback stops or the capture device disconnects; system power settings are not changed."));
        if (!platform::SleepInhibitor::BlocksSuspend()) {
            ValueRow();
            Note(T("This build lacks D-Bus, so it only keeps the screen on during video and cannot block automatic suspend."),
                 CurrentPalette().warning);
        }
    }
    EndCard(table);

    table = BeginCard("##about", T("About"));
    if (table) {
        Row(T("Version"));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s %s · Linux", app::kAppName, app::kVersion);
        Row(T("Components"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("V4L2 · OpenGL 3.3 · SDL3 · Dear ImGui · libjpeg-turbo");
        Row(T("License"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("GPL-3.0-or-later · Pretendard, Noto Sans JP (OFL-1.1)");
    }
    EndCard(table);
}

}
