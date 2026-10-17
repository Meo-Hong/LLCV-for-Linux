#pragma once
#include "ui/SettingsView.h"
namespace llcv::settings_ui {
inline constexpr int kRecordingControlFirst=2061,kRecordingControlLast=2070;
inline constexpr int kRecordingToggleCommand=2071,kRecordingOpenFolder=2073;
inline constexpr int kRecordingResolution=2070,kRecordingSplit=2074,kRecordingSplitValue=2075,kRecordingPause=2076;
inline constexpr int kRecordingVolume=2077;
void CreateRecordingSettings(SettingsControls*,HWND);
void LayoutRecordingSettings(SettingsControls*,UINT);
void RefreshRecordingSettings(SettingsControls*);
void ShowRecordingSettings(SettingsControls*);
bool CanToggleRecordingSettings(const SettingsControls*);
bool RecordingSettingsCommand(SettingsControls*,HWND,int,int);
}
