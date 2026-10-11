#pragma once
#include "screenshot/ScreenshotPixels.h"
#include <vector>

void HdrScreenshotReferenceTests();
std::vector<std::uint8_t> HdrScreenshotPattern(const llcv::screenshot::Description& desc);
