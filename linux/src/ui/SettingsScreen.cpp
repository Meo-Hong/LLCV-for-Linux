#include "ui/SettingsScreen.h"

#include "app/AppInfo.h"
#include "app/ModeSelection.h"
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

std::string FormatLabel(capture::PixelFormat format) {
    switch (format) {
    case capture::PixelFormat::Nv12: return "NV12 (4:2:0)";
    case capture::PixelFormat::Yuyv: return "YUY2 (4:2:2)";
    case capture::PixelFormat::Mjpeg:
        return T("MJPEG (압축 · 고해상도/고주사율)", "MJPEG (compressed · high resolution/refresh)");
    case capture::PixelFormat::Bgr24: return "BGR24 (RGB)";
    case capture::PixelFormat::P010: return T("P010 (10비트 HDR10)", "P010 (10-bit HDR10)");
    }
    return "?";
}

std::string PcmTargetLabel(int ms) {
    const char* suffix = "";
    switch (ms) {
    case 10: suffix = T(" (최저 지연)", " (minimum latency)"); break;
    case 15: suffix = T(" (저지연 목표)", " (low-latency target)"); break;
    case 20: suffix = T(" (안정 목표)", " (stability target)"); break;
    case 25: suffix = T(" (권장 · 기본)", " (recommended · default)"); break;
    case 30: suffix = T(" (안정성 우선)", " (stability first)"); break;
    case 40: suffix = T(" (최대 안정)", " (maximum stability)"); break;
    default: break;
    }
    return platform::Format("%d ms%s", ms, suffix);
}

std::string DeviceFramesLabel(int frames) {
    const double ms = frames * 1000.0 / 48000.0;
    const char* suffix = frames == 256 ? T(" (권장)", " (recommended)") : "";
    return platform::Format(T("%d 프레임 · %.1f ms%s", "%d frames · %.1f ms%s"), frames, ms, suffix);
}

}

SettingsAction SettingsScreen::Draw(AppSettings& settings, const SettingsContext& context) {
    action_ = SettingsAction::None;
    g_scale = context.scale;
    g_fonts = context.fonts;
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
    Note(T("저지연 캡처 뷰어", "Low Latency Capture Viewer"));
    ImGui::Dummy(ImVec2(0.0f, 14.0f * g_scale));

    struct Item {
        Page page;
        const char* label;
    };
    const Item items[] = {
        {Page::Video, T("영상##video", "Video##video")},
        {Page::Audio, T("오디오##audio", "Audio##audio")},
        {Page::Window, T("창##window", "Window##window")},
        {Page::Guide, T("안내 · 로그##guide", "Guide & logs##guide")},
        {Page::App, T("앱##app", "App##app")},
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
    if (ImGui::Button(T("종료##quit", "Quit##quit"), ImVec2(buttonWidth, 0.0f))) {
        action_ = SettingsAction::Quit;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.canStart);
    ImGui::PushStyleColor(ImGuiCol_Button, palette.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.primaryHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.primaryPressed);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.onAccent);
    if (ImGui::Button(T("시작##start", "Start##start"), ImVec2(buttonWidth, 0.0f))) {
        action_ = SettingsAction::Start;
    }
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
}

void SettingsScreen::DrawVideo(AppSettings& s, const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const auto& devices = *context.captureDevices;

    bool table = BeginCard("##capture", T("캡처", "Capture"));
    if (table) {
        Row(T("캡처 장치", "Capture device"));
        std::vector<std::string> names{T("자동 선택 (권장)", "Auto select (recommended)")};
        int index = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            names.push_back(devices[i].DisplayName());
            if (!s.captureDevice.empty() &&
                (devices[i].id == s.captureDevice || devices[i].node == s.captureDevice)) {
                index = static_cast<int>(i) + 1;
            }
        }
        if (!s.captureDevice.empty() && index == 0) {
            names.push_back(s.captureDevice + T(" (연결 안 됨)", " (not connected)"));
            index = static_cast<int>(names.size()) - 1;
        }
        const char* refresh = T("새로고침##refresh", "Refresh##refresh");
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
                     ? T("캡처 장치가 없습니다. USB 연결을 확인하고 새로고침을 누르세요.",
                         "No capture device found. Check the USB connection and select Refresh.")
                     : T("선택한 장치가 연결되어 있지 않습니다.", "The selected device is not connected."),
                 palette.warning);
        } else {
            const auto resolutions = capture::Resolutions(*device);
            Row(T("캡처 해상도", "Capture resolution"));
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
                                                     T(" (지원 안 함)", " (unsupported)")));
                sizeIndex = static_cast<int>(sizeItems.size()) - 1;
            }
            if (Combo("##resolution", sizeIndex, sizeItems) &&
                sizeIndex < static_cast<int>(resolutions.size())) {
                s.videoWidth = resolutions[static_cast<size_t>(sizeIndex)].first;
                s.videoHeight = resolutions[static_cast<size_t>(sizeIndex)].second;
            }

            Row(T("픽셀 형식", "Pixel format"));
            const auto formats = capture::FormatsAt(*device, s.videoWidth, s.videoHeight);
            std::vector<std::string> formatItems{T("자동 (NV12 우선 · 권장)", "Auto (NV12 first · recommended)")};
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
                                      T(" (지원 안 함)", " (unsupported)"));
                formatChoices.push_back(s.pixelFormat);
                formatIndex = static_cast<int>(formatItems.size()) - 1;
            }
            if (Combo("##pixelFormat", formatIndex, formatItems)) {
                s.pixelFormat = formatChoices[static_cast<size_t>(formatIndex)];
            }

            Row(T("프레임레이트", "Frame rate"));
            const auto rates = capture::RatesAt(*device, s.videoWidth, s.videoHeight,
                                                app::FormatFromChoice(s.pixelFormat));
            std::vector<std::string> rateItems{T("자동 (최고 비압축 프레임레이트)", "Auto (highest uncompressed rate)")};
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
                                                     T(" (지원 안 함)", " (unsupported)")));
                rateValues.push_back(s.frameRateMilli);
                rateIndex = static_cast<int>(rateItems.size()) - 1;
            }
            if (Combo("##frameRate", rateIndex, rateItems)) {
                s.frameRateMilli = rateValues[static_cast<size_t>(rateIndex)];
            }

            ValueRow();
            if (const auto mode = capture::ResolveMode(*device, app::RequestFromSettings(s))) {
                Note(platform::Format(T("적용 모드: %d × %d · %s · %s fps", "Selected mode: %d × %d · %s · %s fps"),
                                      mode->width, mode->height, capture::PixelFormatName(mode->format),
                                      capture::FormatRate(mode->rate).c_str()));
            } else {
                Note(T("지원 모드 없음: 다른 해상도나 형식을 선택하세요.",
                       "No supported mode: choose another resolution or format."),
                     palette.warning);
            }
        }
    }
    EndCard(table);

    table = BeginCard("##display", T("표시", "Display"));
    if (table) {
        Row(T("화면 표시 방식", "Presentation mode"));
        EnumCombo<settings::PresentationMode>(
            "##presentation", s.presentationMode,
            {{settings::PresentationMode::Immediate, T("저지연 (즉시 표시 · 권장)", "Immediate (minimum latency · recommended)")},
             {settings::PresentationMode::VSync, T("VSync (티어링 감소)", "VSync (reduced tearing)")}});
        Row(T("화면 확대 방식", "Scaling mode"));
        EnumCombo<settings::ScalingMode>("##scaling", s.scalingMode,
                                         {{settings::ScalingMode::Smooth, T("부드럽게", "Smooth")},
                                          {settings::ScalingMode::Sharp, T("선명하게", "Sharp")}});
        Row(T("색상 해석", "Color interpretation"));
        EnumCombo<video::ColorOverride>("##color", s.colorOverride,
                                        {{video::ColorOverride::Auto, T("자동 (권장)", "Auto (recommended)")},
                                         {video::ColorOverride::Bt709Limited, "BT.709 Limited"},
                                         {video::ColorOverride::Bt709Full, "BT.709 Full"},
                                         {video::ColorOverride::Bt601Limited, "BT.601 Limited"},
                                         {video::ColorOverride::Bt601Full, "BT.601 Full"}});
    }
    EndCard(table);

    DrawHdr(s, context);

    table = BeginCard("##screenshot", T("스크린샷 (F12)", "Screenshots (F12)"));
    if (table) {
        bool open = false;
        PathRow(T("저장 위치", "Save location"), context.screenshotDirectory,
                T("폴더 열기##shots", "Open folder##shots"), open);
        if (open) action_ = SettingsAction::OpenScreenshotFolder;
        ValueRow();
        ImGui::Checkbox(T("클립보드에도 PNG 복사", "Also copy the PNG to the clipboard"), &s.screenshotClipboard);
        ValueRow();
        Note(T("입력 해상도 그대로 PNG로 저장합니다.", "Saved as PNG at the capture resolution."));
    }
    EndCard(table);
}

void SettingsScreen::DrawHdr(AppSettings& s, const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const bool table = BeginCard("##hdr", "HDR");
    if (table) {
        Row(T("HDR 입력 해석", "HDR input"));
        EnumCombo<video::HdrInput>(
            "##hdrInput", s.hdrInput,
            {{video::HdrInput::Auto, T("자동 (P010 = HDR10 · 권장)", "Auto (P010 = HDR10 · recommended)")},
             {video::HdrInput::ForceHdr10, T("HDR10 강제 (8비트 입력도 PQ/BT.2020)", "Force HDR10 (8-bit input as PQ/BT.2020)")},
             {video::HdrInput::ForceSdr, T("SDR로 해석", "Treat as SDR")}});
        Row(T("HDR 표시 방식", "HDR presentation"));
        EnumCombo<settings::HdrOutputMode>(
            "##hdrOutput", s.hdrOutput,
            {{settings::HdrOutputMode::Auto, T("자동 (HDR 모니터면 HDR 출력, 아니면 톤매핑)", "Auto (HDR output on HDR displays, else tone mapping)")},
             {settings::HdrOutputMode::ToneMap, T("항상 SDR 톤매핑", "Always SDR tone mapping")},
             {settings::HdrOutputMode::Hdr, T("항상 HDR 출력 (Wayland)", "Always HDR output (Wayland)")}});
        Row(T("SDR 기준 흰색", "SDR reference white"));
        std::vector<std::pair<int, std::string>> whiteOptions;
        for (const int nits : settings::kSdrWhiteOptions) {
            whiteOptions.emplace_back(nits, platform::Format("%d nit%s", nits,
                                                             nits == 203 ? T(" (권장 · BT.2408)", " (recommended · BT.2408)") : ""));
        }
        EnumCombo<int>("##sdrWhite", s.sdrWhiteNits, whiteOptions);
        ValueRow();
        Note(T("톤매핑 밝기와 HDR 출력 중 UI 밝기의 기준입니다.",
               "Sets tone-mapping brightness and UI brightness during HDR output."));

        const HdrSupport& hdr = context.hdr;
        if (!hdr.input && !hdr.output) {
            ValueRow();
            Note(T("이 환경은 HDR을 지원하지 않습니다. HDR 신호는 SDR로 변환해 표시합니다.",
                   "HDR is not supported in this environment. HDR signals are converted to SDR."),
                 palette.warning);
        }
        const auto status = [&](const char* label, bool supported, const std::string& detail) {
            Row(label);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, supported ? palette.text : palette.warning);
            ImGui::TextUnformatted(supported ? T("지원", "Supported") : T("미지원", "Not supported"));
            ImGui::PopStyleColor();
            ValueRow();
            Note(detail, supported ? palette.secondary : palette.warning);
        };
        status(T("HDR 입력", "HDR input"), hdr.input, hdr.inputText);
        status(T("HDR 출력", "HDR output"), hdr.output, hdr.outputText);
        if (s.hdrOutput == settings::HdrOutputMode::ToneMap) {
            ValueRow();
            Note(T("다른 값으로 바꾸면 10비트 화면 버퍼는 다음 실행부터 적용됩니다.",
                   "If you switch to another mode, the 10-bit framebuffer applies from the next launch."));
        }
    }
    EndCard(table);
}

void SettingsScreen::DrawAudio(AppSettings& s, const SettingsContext& context) {
    const Palette& palette = CurrentPalette();
    const auto& recording = *context.recordingDevices;
    const auto& playback = *context.playbackDevices;

    bool table = BeginCard("##audioCapture", T("캡처", "Capture"));
    if (table) {
        Row(T("캡처 오디오 장치", "Capture audio device"));
        std::vector<std::string> items{
            T("자동 (영상 장치와 같은 USB 장치 · 권장)", "Auto (same USB device as video · recommended)"),
            T("사용 안 함 (영상만)", "Disabled (video only)")};
        int index = 0;
        if (s.captureAudioDevice == settings::kCaptureAudioDisabled) index = 1;
        for (size_t i = 0; i < recording.size(); ++i) {
            items.push_back(recording[i].name);
            if (s.captureAudioDevice == recording[i].name) index = static_cast<int>(i) + 2;
        }
        if (!s.captureAudioDevice.empty() && s.captureAudioDevice != settings::kCaptureAudioDisabled &&
            index == 0) {
            items.push_back(s.captureAudioDevice + T(" (연결 안 됨)", " (not connected)"));
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
                Note(T("자동 감지 실패 · 캡처 오디오 장치를 직접 선택하세요.",
                       "Auto detection failed · select the capture audio device manually."),
                     palette.warning);
            } else {
                Note(std::string(T("자동 감지: ", "Detected: ")) + context.autoCaptureAudio);
            }
        }
    }
    EndCard(table);

    table = BeginCard("##audioOutput", T("출력", "Output"));
    if (table) {
        Row(T("오디오 출력 장치", "Audio output device"));
        std::vector<std::string> items{T("시스템 기본 출력 따라가기 (권장)", "Follow the system default output (recommended)")};
        int index = 0;
        for (size_t i = 0; i < playback.size(); ++i) {
            items.push_back(playback[i].name);
            if (s.audioOutputDevice == playback[i].name) index = static_cast<int>(i) + 1;
        }
        if (!s.audioOutputDevice.empty() && index == 0) {
            items.push_back(s.audioOutputDevice + T(" (연결 안 됨)", " (not connected)"));
            index = static_cast<int>(items.size()) - 1;
        }
        if (Combo("##audioOutput", index, items)) {
            if (index == 0) {
                s.audioOutputDevice.clear();
            } else if (index - 1 < static_cast<int>(playback.size())) {
                s.audioOutputDevice = playback[static_cast<size_t>(index - 1)].name;
            }
        }

        Row(T("오디오 출력 버퍼", "Audio output buffer"));
        std::vector<std::pair<int, std::string>> frameOptions;
        for (const int frames : settings::kDeviceFrameOptions) frameOptions.emplace_back(frames, DeviceFramesLabel(frames));
        EnumCombo<int>("##deviceFrames", s.audioDeviceFrames, frameOptions);
        ValueRow();
        Note(platform::Format(T("오디오 서버: %s · 장치 버퍼는 PipeWire 그래프 quantum으로 요청됩니다.",
                                "Audio server: %s · the buffer is requested as the PipeWire graph quantum."),
                              context.audioDriver.empty() ? "?" : context.audioDriver.c_str()));
    }
    EndCard(table);

    table = BeginCard("##audioSync", T("동기화 · 안정성", "Sync & stability"));
    if (table) {
        Row(T("PCM 버퍼 목표", "PCM buffer target"));
        std::vector<std::pair<int, std::string>> targetOptions;
        for (const int ms : settings::kPcmTargetOptions) targetOptions.emplace_back(ms, PcmTargetLabel(ms));
        EnumCombo<int>("##pcmTarget", s.pcmQueueTargetMs, targetOptions);
        Row(T("클록 드리프트 보정", "Clock-drift correction"));
        EnumCombo<settings::DriftCorrection>(
            "##drift", s.driftCorrection,
            {{settings::DriftCorrection::Auto, T("자동 (권장 · 필요 시 보정)", "Auto (recommended · correct only when needed)")},
             {settings::DriftCorrection::Off, T("끔 (원본 PCM · 음질 우선)", "Off (unaltered PCM · quality first)")}});
        ValueRow();
        Note(T("소리가 가끔 끊기면 PCM 버퍼 목표를 5 ms씩 올려 보세요.",
               "If sound breaks up occasionally, raise the PCM target in 5 ms steps."));
    }
    EndCard(table);

    table = BeginCard("##audioPlayback", T("재생 · 편의", "Playback & convenience"));
    if (table) {
        Row(T("음량", "Volume"));
        ImGui::SliderInt("##volume", &s.volumePercent, 0, s.allowVolumeBoost ? 200 : 100, "%d%%");
        Row(T("좌 / 우 음량", "Left / right volume"));
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(half);
        ImGui::SliderInt("##left", &s.leftVolumePercent, 0, 100, T("왼쪽 %d%%", "Left %d%%"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half);
        ImGui::SliderInt("##right", &s.rightVolumePercent, 0, 100, T("오른쪽 %d%%", "Right %d%%"));
        ValueRow();
        if (ImGui::Checkbox(T("100% 이상 볼륨 증폭 허용 (최대 200%)", "Allow volume boost above 100% (up to 200%)"),
                            &s.allowVolumeBoost) &&
            !s.allowVolumeBoost) {
            s.volumePercent = std::min(s.volumePercent, 100);
        }
        ValueRow();
        ImGui::Checkbox(T("백그라운드에서 자동 음소거", "Mute automatically in background"), &s.muteWhenBackground);
        Row(T("음량 표시 위치", "Volume indicator position"));
        EnumCombo<settings::VolumeHudPosition>(
            "##hud", s.volumeHudPosition,
            {{settings::VolumeHudPosition::TopLeft, T("좌측 상단 (기본)", "Top-left (default)")},
             {settings::VolumeHudPosition::TopRight, T("우측 상단", "Top-right")},
             {settings::VolumeHudPosition::BottomLeft, T("좌측 하단", "Bottom-left")},
             {settings::VolumeHudPosition::BottomRight, T("우측 하단", "Bottom-right")}});
        ValueRow();
        ImGui::Checkbox(T("오디오 전용 모드 (영상 없이 소리만 재생)", "Audio-only mode (sound without video)"), &s.audioOnly);
    }
    EndCard(table);
}

void SettingsScreen::DrawWindow(AppSettings& s, const SettingsContext& context) {
    bool table = BeginCard("##windowSize", T("창 크기 · 표시", "Window size & display"));
    if (table) {
        ValueRow();
        ImGui::Checkbox(T("Pixel-perfect (원본 크기 1:1 · 크기 고정)", "Pixel-perfect (1:1 source size · locked)"),
                        &s.pixelPerfect);
        ValueRow();
        Note(T("끄면 창 크기를 자유롭게 바꿀 수 있고 영상 비율은 유지됩니다. F5로 1:1 크기로 돌아갑니다.",
               "When off, the window can be resized freely while keeping the aspect ratio. F5 restores 1:1."));
        ValueRow();
        ImGui::Checkbox(T("테두리 없는 창", "Borderless window"), &s.borderlessWindow);
        ValueRow();
        ImGui::Checkbox(T("전체화면으로 시작", "Start in fullscreen"), &s.startFullscreen);
        Row(T("전체화면 커서", "Fullscreen cursor"));
        EnumCombo<settings::FullscreenCursor>(
            "##cursor", s.fullscreenCursor,
            {{settings::FullscreenCursor::AutoHide, T("자동 숨김 (권장)", "Auto-hide (recommended)")},
             {settings::FullscreenCursor::AlwaysVisible, T("항상 표시", "Always visible")}});
    }
    EndCard(table);

    table = BeginCard("##monitor", T("모니터", "Monitor"));
    if (table) {
        Row(T("시작 모니터", "Startup monitor"));
        const auto& displays = *context.displays;
        std::vector<std::string> items{T("자동 (마지막 위치)", "Auto (last position)")};
        int index = 0;
        for (size_t i = 0; i < displays.size(); ++i) {
            items.push_back(displays[i].name);
            if (s.preferredDisplay == displays[i].name) index = static_cast<int>(i) + 1;
        }
        if (!s.preferredDisplay.empty() && index == 0) {
            items.push_back(s.preferredDisplay + T(" (연결 안 됨)", " (not connected)"));
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
            Note(T("Wayland에서는 창 위치를 컴포지터가 정합니다. 선택한 모니터는 전체화면에서 적용됩니다. "
                   "창 위치 지정이 필요하면 앱 → 디스플레이 백엔드에서 X11을 선택하세요.",
                   "On Wayland the compositor places windows. The selected monitor applies to fullscreen. "
                   "Choose X11 under App → Display backend to position windows."));
        } else {
            Note(T("X11 모드: 시작 모니터와 마지막 창 위치를 복원합니다.",
                   "X11 mode: the startup monitor and last window position are restored."));
        }
    }
    EndCard(table);
}

void SettingsScreen::DrawGuide(AppSettings& s, const SettingsContext& context) {
    const bool english = IsEnglish();
    bool table = BeginCard("##shortcuts", T("단축키", "Keyboard shortcuts"));
    if (table) {
        for (const auto& shortcut : kShortcuts) {
            Row(shortcut.key);
            std::string text = english ? shortcut.english : shortcut.korean;
            if (shortcut.videoOnly) text += T(" (영상 모드)", " (video mode)");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text.c_str());
        }
    }
    EndCard(table);

    table = BeginCard("##mouse", T("마우스", "Mouse"));
    if (table) {
        for (const auto& hint : kMouseHints) {
            Row(english ? hint.englishGesture : hint.koreanGesture);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(english ? hint.english : hint.korean);
        }
    }
    EndCard(table);

    table = BeginCard("##logs", T("진단 로그", "Diagnostic logs"));
    if (table) {
        ValueRow();
        if (ImGui::Checkbox(T("진단 로그 저장", "Save diagnostic log"), &s.saveLog)) {
            action_ = SettingsAction::LogToggled;
        }
        bool open = false;
        PathRow(T("로그 위치", "Log location"), context.logDirectory,
                T("로그 폴더 열기##logs", "Open logs folder##logs"), open);
        if (open) action_ = SettingsAction::OpenLogFolder;
    }
    EndCard(table);

    table = BeginCard("##troubleshooting", T("문제 해결", "Troubleshooting"));
    if (table) {
        const char* notes[] = {
            T("영상이 나오지 않으면 OBS 등 같은 캡처 장치를 쓰는 앱을 종료하세요.",
              "If there is no picture, close OBS or any other app that uses the capture device."),
            T("4K 60 fps 같은 고대역 모드는 장치가 MJPEG로만 제공할 수 있습니다.",
              "High-bandwidth modes such as 4K 60 fps may only be offered as MJPEG."),
            T("영상은 나오는데 소리가 없으면 오디오 → 캡처 오디오 장치를 확인하세요.",
              "If video works but audio does not, check Audio → Capture audio device."),
            T("Tab 진단 화면에서 프레임 손실과 오디오 underrun을 확인할 수 있습니다.",
              "The Tab diagnostics overlay shows dropped frames and audio underruns."),
        };
        for (const char* note : notes) {
            ValueRow();
            Note(note);
        }
    }
    EndCard(table);
}

void SettingsScreen::DrawApp(AppSettings& s, const SettingsContext& context) {
    bool table = BeginCard("##language", T("언어", "Language"));
    if (table) {
        Row(T("표시 언어", "Display language"));
        if (EnumCombo<settings::UiLanguage>("##uiLanguage", s.uiLanguage,
                                            {{settings::UiLanguage::Auto, T("자동 (시스템)", "Auto (system)")},
                                             {settings::UiLanguage::Korean, "한국어"},
                                             {settings::UiLanguage::English, "English"}})) {
            action_ = SettingsAction::LanguageChanged;
        }
    }
    EndCard(table);

    table = BeginCard("##theme", T("앱 테마", "App theme"));
    if (table) {
        Row(T("테마", "Theme"));
        bool light = s.lightTheme;
        if (EnumCombo<bool>("##themeCombo", light, {{false, T("다크", "Dark")}, {true, T("라이트", "Light")}})) {
            s.lightTheme = light;
            action_ = SettingsAction::ThemeChanged;
        }
        ValueRow();
        Note(T("영상 위 오버레이는 대비를 위해 항상 어둡게 표시됩니다.",
               "Overlays drawn over video stay dark for contrast."));
    }
    EndCard(table);

    table = BeginCard("##backend", T("디스플레이 백엔드", "Display backend"));
    if (table) {
        Row(T("백엔드", "Backend"));
        EnumCombo<settings::DisplayBackend>(
            "##displayBackend", s.displayBackend,
            {{settings::DisplayBackend::Auto, T("자동 (Wayland 우선)", "Auto (Wayland first)")},
             {settings::DisplayBackend::Wayland, "Wayland"},
             {settings::DisplayBackend::X11, "X11 (XWayland)"}});
        ValueRow();
        Note(platform::Format(T("현재: %s · 변경 사항은 다음 실행부터 적용됩니다. llcv --x11 로도 실행할 수 있습니다.",
                                "Current: %s · changes apply on the next launch. You can also run llcv --x11."),
                              context.videoDriver.c_str()));
    }
    EndCard(table);

    table = BeginCard("##startup", T("시작", "Startup"));
    if (table) {
        ValueRow();
        ImGui::Checkbox(T("다음 실행부터 설정 화면 건너뛰기 (F2로 다시 열기)",
                          "Skip this settings screen on next launch (F2 reopens it)"),
                        &s.skipStartupSettings);
    }
    EndCard(table);

    table = BeginCard("##about", T("정보", "About"));
    if (table) {
        Row(T("버전", "Version"));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s %s · Linux", app::kAppName, app::kVersion);
        Row(T("구성", "Components"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("V4L2 · OpenGL 3.3 · SDL3 · Dear ImGui · libjpeg-turbo");
        Row(T("라이선스", "License"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("GPL-3.0-or-later · Pretendard (OFL-1.1)");
    }
    EndCard(table);
}

}
