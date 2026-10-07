#include "ui/Theme.h"

#include "diagnostics/Logger.h"
#include "platform/Paths.h"

#include <filesystem>
#include <system_error>

namespace llcv::ui {
namespace {

constexpr Palette kDark{
    IM_COL32(22, 22, 22, 255),    IM_COL32(17, 17, 17, 255),    IM_COL32(29, 29, 29, 255),
    IM_COL32(39, 39, 39, 255),    IM_COL32(31, 31, 31, 255),    IM_COL32(51, 51, 51, 255),
    IM_COL32(66, 66, 66, 255),    IM_COL32(47, 47, 47, 255),    IM_COL32(48, 48, 48, 255),
    IM_COL32(235, 235, 235, 255), IM_COL32(176, 176, 176, 255), IM_COL32(133, 133, 133, 255),
    IM_COL32(225, 225, 225, 255), IM_COL32(101, 101, 101, 255), IM_COL32(242, 242, 242, 255),
    IM_COL32(195, 195, 195, 255), IM_COL32(22, 22, 22, 255),    IM_COL32(226, 176, 117, 255),
    IM_COL32(237, 98, 84, 255),
};

constexpr Palette kLight{
    IM_COL32(247, 247, 247, 255), IM_COL32(240, 240, 240, 255), IM_COL32(242, 242, 242, 255),
    IM_COL32(255, 255, 255, 255), IM_COL32(235, 235, 235, 255), IM_COL32(230, 230, 230, 255),
    IM_COL32(180, 180, 180, 255), IM_COL32(215, 215, 215, 255), IM_COL32(225, 235, 252, 255),
    IM_COL32(49, 64, 82, 255),    IM_COL32(72, 88, 106, 255),   IM_COL32(118, 132, 148, 255),
    IM_COL32(37, 99, 205, 255),   IM_COL32(73, 123, 207, 255),  IM_COL32(29, 82, 182, 255),
    IM_COL32(25, 67, 149, 255),   IM_COL32(255, 255, 255, 255), IM_COL32(143, 87, 16, 255),
    IM_COL32(187, 43, 43, 255),
};

const Palette* g_current = &kDark;

struct SystemFont {
    std::filesystem::path path;
    int index = 0;
};

SystemFont FindSystemKoreanFont() {
    const SystemFont candidates[] = {
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 1},
        {"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", 1},
        {"/usr/share/fonts/truetype/nanum/NanumGothic.ttf", 0},
        {"/usr/share/fonts/truetype/unfonts-core/UnDotum.ttf", 0},
    };
    std::error_code error;
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate.path, error)) return candidate;
    }
    return {};
}

ImFont* AddFont(const std::filesystem::path& path, int index) {
    if (path.empty()) return nullptr;
    ImFontConfig config;
    config.FontNo = index;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), kBaseFontSize, &config);
}

}

const Palette& PaletteFor(bool light) {
    return light ? kLight : kDark;
}

const Palette& CurrentPalette() {
    return *g_current;
}

ImVec4 ToVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

FontSet LoadFonts() {
    FontSet fonts;
    fonts.regular = AddFont(platform::FindFont("Pretendard-Regular.ttf"), 0);
    fonts.semibold = AddFont(platform::FindFont("Pretendard-SemiBold.ttf"), 0);
    if (!fonts.regular) {
        const SystemFont system = FindSystemKoreanFont();
        fonts.regular = AddFont(system.path, system.index);
        diagnostics::Log("[ui] Pretendard not found; fallback font %s",
                         system.path.empty() ? "(built-in)" : system.path.c_str());
    }
    if (!fonts.regular) fonts.regular = ImGui::GetIO().Fonts->AddFontDefault();
    if (!fonts.semibold) fonts.semibold = fonts.regular;
    ImGui::GetIO().FontDefault = fonts.regular;
    return fonts;
}

void ApplyTheme(bool light, float scale) {
    g_current = &PaletteFor(light);
    const Palette& p = *g_current;

    ImGuiStyle style;
    if (light) {
        ImGui::StyleColorsLight(&style);
    } else {
        ImGui::StyleColorsDark(&style);
    }
    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(6.0f, 5.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ToVec4(p.text);
    colors[ImGuiCol_TextDisabled] = ToVec4(p.disabled);
    colors[ImGuiCol_WindowBg] = ToVec4(p.background);
    colors[ImGuiCol_ChildBg] = ToVec4(p.card);
    colors[ImGuiCol_PopupBg] = ToVec4(p.control);
    colors[ImGuiCol_Border] = ToVec4(p.cardEdge);
    colors[ImGuiCol_FrameBg] = ToVec4(p.control);
    colors[ImGuiCol_FrameBgHovered] = ToVec4(p.hover);
    colors[ImGuiCol_FrameBgActive] = ToVec4(p.selected);
    colors[ImGuiCol_Button] = ToVec4(p.control);
    colors[ImGuiCol_ButtonHovered] = ToVec4(p.hover);
    colors[ImGuiCol_ButtonActive] = ToVec4(p.selected);
    colors[ImGuiCol_Header] = ToVec4(p.selected);
    colors[ImGuiCol_HeaderHovered] = ToVec4(p.hover);
    colors[ImGuiCol_HeaderActive] = ToVec4(p.selected);
    colors[ImGuiCol_CheckMark] = ToVec4(p.accent);
    colors[ImGuiCol_SliderGrab] = ToVec4(p.accent);
    colors[ImGuiCol_SliderGrabActive] = ToVec4(p.primaryPressed);
    colors[ImGuiCol_ScrollbarBg] = ToVec4(p.background);
    colors[ImGuiCol_ScrollbarGrab] = ToVec4(p.edge);
    colors[ImGuiCol_ScrollbarGrabHovered] = ToVec4(p.hoverEdge);
    colors[ImGuiCol_ScrollbarGrabActive] = ToVec4(p.hoverEdge);
    colors[ImGuiCol_Separator] = ToVec4(p.cardEdge);
    colors[ImGuiCol_TableBorderLight] = ToVec4(p.cardEdge);
    colors[ImGuiCol_TableBorderStrong] = ToVec4(p.edge);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0);

    style.ScaleAllSizes(scale);
    style.FontSizeBase = kBaseFontSize;
    style.FontScaleDpi = scale;
    ImGui::GetStyle() = style;
}

}
