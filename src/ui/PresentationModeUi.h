#pragma once

#include "settings/AppSettings.h"

namespace llcv::presentation_ui {

inline constexpr int ModeCount = 4;

const wchar_t* ImmediateLabel(bool english);
const wchar_t* VSyncLabel(bool english);
const wchar_t* CompatibilityLabel(bool english);
const wchar_t* ExclusiveFullscreenLabel(bool english);
const wchar_t* HelpText(bool english);
settings::PresentationMode ModeFromSelection(int selection);
int SelectionFromMode(settings::PresentationMode mode);

}  // namespace llcv::presentation_ui
