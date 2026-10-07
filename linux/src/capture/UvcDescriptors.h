#pragma once

#include <string>

namespace llcv::capture {

bool UvcAdvertisesP010(const std::string& nodeName);
std::string KernelRelease();

}
