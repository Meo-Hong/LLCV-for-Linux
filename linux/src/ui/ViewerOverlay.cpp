#include "ui/ViewerOverlay.h"

#include "app/AppInfo.h"
#include "platform/Strings.h"
#include "audio/AudioMix.h"
#include "ui/Shortcuts.h"
#include "ui/Text.h"

#include <algorithm>

namespace llcv::ui {
namespace {

constexpr ImGuiWindowFlags kPassiveFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_NoMove;

constexpr ImGuiWindowFlags kInteractiveFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

class OverlayStyle {
public:
    explicit OverlayStyle(float scale) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, OverlayColors::kPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, OverlayColors::kPanelEdge);
        ImGui::PushStyleColor(ImGuiCol_Text, OverlayColors::kText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, OverlayColors::kSecondary);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, OverlayColors::kTrack);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(66, 66, 66, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(80, 80, 80, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(225, 225, 225, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(195, 195, 195, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(39, 39, 39, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(51, 51, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(66, 66, 66, 255));
        ImGui::PushStyleColor(ImGuiCol_Separator, OverlayColors::kPanelEdge);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * scale, 12.0f * scale));
    }
    ~OverlayStyle() {
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(13);
    }
    OverlayStyle(const OverlayStyle&) = delete;
    OverlayStyle& operator=(const OverlayStyle&) = delete;
};

ImVec2 Corner(settings::VolumeHudPosition position, float margin, ImVec2& pivot) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float left = viewport->WorkPos.x + margin;
    const float top = viewport->WorkPos.y + margin;
    const float right = viewport->WorkPos.x + viewport->WorkSize.x - margin;
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y - margin;
    switch (position) {
    case settings::VolumeHudPosition::TopRight:
        pivot = ImVec2(1.0f, 0.0f);
        return ImVec2(right, top);
    case settings::VolumeHudPosition::BottomLeft:
        pivot = ImVec2(0.0f, 1.0f);
        return ImVec2(left, bottom);
    case settings::VolumeHudPosition::BottomRight:
        pivot = ImVec2(1.0f, 1.0f);
        return ImVec2(right, bottom);
    case settings::VolumeHudPosition::TopLeft:
        break;
    }
    pivot = ImVec2(0.0f, 0.0f);
    return ImVec2(left, top);
}

void Bar(float fraction, ImU32 color, float height, float scale) {
    const float width = ImGui::GetContentRegionAvail().x;
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float y = origin.y + (lineHeight - height) * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + width, y + height), OverlayColors::kTrack,
                            3.0f * scale);
    const float filled = width * std::clamp(fraction, 0.0f, 1.0f);
    if (filled > 0.5f) {
        drawList->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + filled, y + height), color, 3.0f * scale);
    }
    ImGui::Dummy(ImVec2(width, lineHeight));
}

void Meter(const char* label, int peak, float scale) {
    const double dbfs = audio::PeakToDbfs(peak);
    const float fraction = static_cast<float>((dbfs + 60.0) / 60.0);
    const ImU32 color = dbfs > -3.0 ? OverlayColors::kDanger
                                    : dbfs > -12.0 ? OverlayColors::kMeterHot : OverlayColors::kMeter;
    ImGui::TextUnformatted(label);
    ImGui::SameLine(28.0f * scale);
    Bar(fraction, color, 9.0f * scale, scale);
}

bool AudioControls(AudioMeterState& state, float scale) {
    if (!state.available) {
        ImGui::PushStyleColor(ImGuiCol_Text, OverlayColors::kMeterHot);
        ImGui::TextWrapped("%s", state.status.empty() ? T("No capture audio") : state.status.c_str());
        ImGui::PopStyleColor();
    }
    Meter("L", state.peakLeft, scale);
    Meter("R", state.peakRight, scale);
    if (state.clipActive) {
        ImGui::PushStyleColor(ImGuiCol_Text, OverlayColors::kDanger);
        ImGui::Text(T("Clipping active (%llu events)"),
                    static_cast<unsigned long long>(state.clipEvents));
        ImGui::PopStyleColor();
    } else if (state.clipEvents > 0) {
        ImGui::TextDisabled(T("Clipping recorded (%llu events)"),
                            static_cast<unsigned long long>(state.clipEvents));
    } else {
        ImGui::TextDisabled("%s", T("No clipping"));
    }
    ImGui::Spacing();
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderInt("##osdMaster", &state.master, 0, state.boost ? 200 : 100,
                                T("Volume %d%%"));
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::SetNextItemWidth(half);
    changed |= ImGui::SliderInt("##osdLeft", &state.left, 0, 100, T("Left %d%%"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    changed |= ImGui::SliderInt("##osdRight", &state.right, 0, 100, T("Right %d%%"));
    if (state.muted) ImGui::TextDisabled("%s", T("Muted in background"));
    return changed;
}

}

void DrawDiagnostics(const std::vector<DiagnosticsLine>& lines, const input::Bindings& shortcuts, const FontSet& fonts,
                     float scale) {
    OverlayStyle style(scale);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 12.0f * scale, viewport->WorkPos.y + 12.0f * scale),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if (ImGui::Begin("##diagnostics", nullptr, kPassiveFlags)) {
        ImGui::PushFont(fonts.semibold, 0.0f);
        ImGui::TextUnformatted(T("Capture diagnostics"));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", input::ExpandKeyLabels(T("[Tab close]"), shortcuts).c_str());
        ImGui::Separator();
        if (ImGui::BeginTable("##diagnosticsTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (const auto& line : lines) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", line.label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(line.value.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DrawVolumeHud(int volume, bool muted, settings::VolumeHudPosition position, float alpha, float scale) {
    if (alpha <= 0.0f) return;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    {
        OverlayStyle style(scale);
        ImVec2 pivot;
        const ImVec2 position2 = Corner(position, 16.0f * scale, pivot);
        ImGui::SetNextWindowPos(position2, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowSize(ImVec2(230.0f * scale, 0.0f));
        if (ImGui::Begin("##volumeHud", nullptr, kPassiveFlags)) {
            if (muted) {
                ImGui::Text(T("Volume  %d%% · muted"), volume);
            } else {
                ImGui::Text(T("Volume  %d%%"), volume);
            }
            const float maximum = volume > 100 ? 200.0f : 100.0f;
            Bar(static_cast<float>(volume) / maximum, IM_COL32(225, 225, 225, 255), 6.0f * scale, scale);
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();
}

void DrawToast(const std::string& text, bool error, float alpha, float scale) {
    if (alpha <= 0.0f || text.empty()) return;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    {
        OverlayStyle style(scale);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                       viewport->Pos.y + viewport->Size.y - 24.0f * scale),
                                ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(viewport->Size.x * 0.9f, viewport->Size.y));
        if (ImGui::Begin("##toast", nullptr, kPassiveFlags)) {
            ImGui::PushTextWrapPos(viewport->Size.x * 0.85f);
            if (error) ImGui::PushStyleColor(ImGuiCol_Text, OverlayColors::kDanger);
            ImGui::TextWrapped("%s", text.c_str());
            if (error) ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();
}

void DrawCenterNotice(const std::string& title, const std::string& detail, const FontSet& fonts, float scale) {
    OverlayStyle style(scale);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                   viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("##notice", nullptr, kPassiveFlags)) {
        ImGui::PushFont(fonts.semibold, 20.0f);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        if (!detail.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f * scale);
            ImGui::TextDisabled("%s", detail.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::End();
}

bool DrawAudioOsd(AudioMeterState& state, const input::Bindings& shortcuts, const FontSet& fonts, float scale) {
    OverlayStyle style(scale);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f * scale,
                                   viewport->WorkPos.y + 12.0f * scale),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(320.0f * scale, 0.0f));
    bool changed = false;
    if (ImGui::Begin("##audioOsd", nullptr, kInteractiveFlags)) {
        ImGui::PushFont(fonts.semibold, 0.0f);
        ImGui::TextUnformatted(T("Audio"));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", input::ExpandKeyLabels(T("[F3 close]"), shortcuts).c_str());
        ImGui::Separator();
        changed = AudioControls(state, scale);
    }
    ImGui::End();
    return changed;
}

bool DrawAudioOnlyView(AudioMeterState& state, const input::Bindings& shortcuts, const FontSet& fonts, float scale) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    bool changed = false;
    OverlayStyle style(scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToVec4(CurrentPalette().background));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(CurrentPalette().text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ToVec4(CurrentPalette().secondary));
    if (ImGui::Begin("##audioOnly", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::PushFont(fonts.semibold, 18.0f);
        ImGui::TextUnformatted(T("Audio only"));
        ImGui::PopFont();
        ImGui::TextDisabled("%s", state.captureName.empty() ? "-" : state.captureName.c_str());
        ImGui::TextDisabled("→ %s", state.outputName.empty() ? "-" : state.outputName.c_str());
        ImGui::Separator();
        changed = AudioControls(state, scale);
        ImGui::Spacing();
        const std::string hint = input::ExpandKeyLabels(
            T("F2 settings · Tab diagnostics · F1 help · wheel volume"), shortcuts);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", hint.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return changed;
}

void DrawHelp(bool* open, bool audioOnly, const HdrSupport& hdr, const input::Bindings& shortcuts, const FontSet& fonts,
              float scale) {
    OverlayStyle style(scale);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                                   viewport->Pos.y + viewport->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * scale, 0.0f));
    if (ImGui::Begin("##help", nullptr, kInteractiveFlags)) {
        ImGui::PushFont(fonts.semibold, 20.0f);
        ImGui::Text("%s %s", app::kAppName, app::kVersion);
        ImGui::PopFont();
        ImGui::TextDisabled("%s", T("Low Latency Capture Viewer · Linux (V4L2 · OpenGL · SDL3)"));
        ImGui::Separator();
        if (ImGui::BeginTable("##helpShortcuts", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kMenuShortcut.key);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(T(kMenuShortcut.text));
            for (const auto& shortcut : kShortcuts) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(input::Label(input::BindingFor(shortcuts, shortcut.action)).c_str());
                ImGui::TableSetColumnIndex(1);
                const char* text = T(shortcut.text);
                if (shortcut.videoOnly && audioOnly) {
                    ImGui::TextDisabled("%s%s", text, T(" (video mode)"));
                } else {
                    ImGui::TextUnformatted(text);
                }
            }
            for (const auto& hint : kMouseHints) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(T(hint.gesture));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(T(hint.text));
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        const float wrap = 430.0f * scale;
        const auto status = [&](const char* label, bool supported, const std::string& detail) {
            ImGui::PushStyleColor(ImGuiCol_Text, supported ? OverlayColors::kText : OverlayColors::kMeterHot);
            ImGui::Text("%s: %s", label, supported ? T("supported") : T("not supported"));
            ImGui::PopStyleColor();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            ImGui::TextDisabled("%s", detail.c_str());
            ImGui::PopTextWrapPos();
        };
        status(T("HDR input"), hdr.input, hdr.inputText);
        status(T("HDR output"), hdr.output, hdr.outputText);
        ImGui::Spacing();
        const std::string closeLabel =
            input::ExpandKeyLabels(T("Close (F1 / Esc)###close"), shortcuts);
        if (ImGui::Button(closeLabel.c_str())) *open = false;
    }
    ImGui::End();
}


MenuCommand DrawMenuBar(const MenuState& state, const input::Bindings& shortcuts, float scale) {
    MenuCommand command = MenuCommand::None;
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, IM_COL32(17, 17, 17, 238));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 24, 246));
    ImGui::PushStyleColor(ImGuiCol_Text, OverlayColors::kText);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, OverlayColors::kSecondary);
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(51, 51, 51, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(66, 66, 66, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(80, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, OverlayColors::kPanelEdge);
    ImGui::PushStyleColor(ImGuiCol_Separator, OverlayColors::kPanelEdge);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, OverlayColors::kText);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * scale, 7.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f * scale, 8.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

    const auto item = [&](const char* label, const char* shortcut, bool selected, MenuCommand value,
                          bool enabled = true) {
        if (ImGui::MenuItem(label, shortcut, selected, enabled)) command = value;
    };
    const auto key = [&](input::Action action) { return input::Label(input::BindingFor(shortcuts, action)); };

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(T("View"))) {
            item(T("Fullscreen"), key(input::Action::Fullscreen).c_str(), state.fullscreen,
                 MenuCommand::Fullscreen);
            if (!state.audioOnly) {
                item(T("Original size (1:1)"), key(input::Action::PixelPerfect).c_str(), false,
                     MenuCommand::RestoreSize);
            }
            item(T("Borderless window"), nullptr, state.borderless, MenuCommand::Borderless);
            ImGui::Separator();
            if (!state.audioOnly) {
                item(T("Smooth scaling"), nullptr, !state.sharp, MenuCommand::ScalingSmooth);
                item(T("Sharp scaling"), nullptr, state.sharp, MenuCommand::ScalingSharp);
                ImGui::Separator();
                item(T("Immediate presentation"), nullptr, !state.vsync, MenuCommand::PresentImmediate);
                item("VSync", nullptr, state.vsync, MenuCommand::PresentVSync);
                ImGui::Separator();
            }
            item(T("Live diagnostics"), key(input::Action::Diagnostics).c_str(), state.diagnostics,
                 MenuCommand::Diagnostics);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(T("Audio"))) {
            if (!state.audioOnly) {
                item(T("Audio OSD"), key(input::Action::AudioOsd).c_str(), state.audioOsd,
                     MenuCommand::AudioOsd);
                ImGui::Separator();
            }
            const std::string up = platform::Format(T("Volume up (now %d%%)"), state.volume);
            item(up.c_str(), T("Wheel up"), false, MenuCommand::VolumeUp);
            item(T("Volume down"), T("Wheel down"), false, MenuCommand::VolumeDown);
            ImGui::Separator();
            item(T("Mute in background"), nullptr, state.backgroundMute,
                 MenuCommand::BackgroundMute);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(T("Tools"))) {
            item(T("Save screenshot"), key(input::Action::Screenshot).c_str(), false,
                 MenuCommand::Screenshot, !state.audioOnly);
            item(T("Open screenshots folder"), nullptr, false, MenuCommand::OpenScreenshots);
            item(T("Open logs folder"), nullptr, false, MenuCommand::OpenLogs);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(T("Settings"))) {
            item(T("Open settings"), key(input::Action::Settings).c_str(), false, MenuCommand::Settings);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(T("Help"))) {
            item(T("App information & shortcuts"), key(input::Action::Help).c_str(), false,
                 MenuCommand::Help);
            ImGui::Separator();
            item(T("Quit"), key(input::Action::Exit).c_str(), false, MenuCommand::Quit);
            ImGui::EndMenu();
        }
        const std::string hintText = input::ExpandKeyLabels(T("Alt · Esc: close menu"), shortcuts);
        const char* hint = hintText.c_str();
        const float hintWidth = ImGui::CalcTextSize(hint).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > hintWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - hintWidth);
            ImGui::TextDisabled("%s", hint);
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(10);
    return command;
}

}
