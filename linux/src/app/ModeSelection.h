#pragma once

#include "capture/V4l2Devices.h"
#include "settings/AppSettings.h"

#include <optional>

namespace llcv::app {

std::optional<capture::PixelFormat> FormatFromChoice(settings::PixelFormatChoice choice);
settings::PixelFormatChoice ChoiceFromFormat(capture::PixelFormat format);
capture::ModeRequest RequestFromSettings(const settings::AppSettings& settings);

}
