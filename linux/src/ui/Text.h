#pragma once

namespace llcv::ui {

enum class Language { Korean, English, Japanese, Spanish, Portuguese };

void SetLanguage(Language language);
Language CurrentLanguage();
Language SystemLanguage();

const char* T(const char* text);

constexpr const char* N_(const char* text) {
    return text;
}

}
