#pragma once

#include <string>

namespace llcv::ui {

struct HdrSupport {
    bool input = false;
    std::string inputText;
    bool output = false;
    std::string outputText;
};

}
