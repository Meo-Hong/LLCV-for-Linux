#include "ui/Text.h"

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace llcv::ui {
namespace {

std::atomic<bool> g_english{false};

bool StartsWithKorean(const char* value) {
    return value && std::strncmp(value, "ko", 2) == 0;
}

}

void SetEnglish(bool english) {
    g_english.store(english, std::memory_order_relaxed);
}

bool IsEnglish() {
    return g_english.load(std::memory_order_relaxed);
}

bool SystemPrefersKorean() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    bool korean = false;
    if (locales) {
        korean = count > 0 && locales[0] && StartsWithKorean(locales[0]->language);
        SDL_free(locales);
        return korean;
    }
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable);
        if (value && *value) return StartsWithKorean(value);
    }
    return false;
}

}
