#pragma once

namespace llcv::ui {

void SetEnglish(bool english);
bool IsEnglish();
bool SystemPrefersKorean();

inline const char* T(const char* korean, const char* english) {
    return IsEnglish() ? english : korean;
}

}
