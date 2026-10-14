#include "ui/Text.h"

#include "diagnostics/Logger.h"
#include "platform/Paths.h"
#include "platform/Strings.h"

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace llcv::ui {
namespace {

struct Catalog {
    std::deque<std::string> storage;
    std::unordered_map<std::string_view, const char*> entries;
};

constexpr size_t kLanguageCount = 5;

std::atomic<Language> g_language{Language::Korean};
std::atomic<const Catalog*> g_catalog{nullptr};
std::array<std::unique_ptr<Catalog>, kLanguageCount> g_catalogs;
std::array<bool, kLanguageCount> g_attempted{};

const char* CatalogFile(Language language) {
    switch (language) {
    case Language::Korean: return "ko.po";
    case Language::Japanese: return "ja.po";
    case Language::Spanish: return "es.po";
    case Language::Portuguese: return "pt_BR.po";
    case Language::English: break;
    }
    return nullptr;
}

void AppendQuoted(std::string_view line, std::string& target) {
    const size_t open = line.find('"');
    const size_t close = line.rfind('"');
    if (open == std::string_view::npos || close <= open) return;
    for (size_t i = open + 1; i < close; ++i) {
        char c = line[i];
        if (c == '\\' && i + 1 < close) {
            switch (line[++i]) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default: c = line[i]; break;
            }
        }
        target.push_back(c);
    }
}

std::unique_ptr<Catalog> LoadCatalog(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return nullptr;
    auto catalog = std::make_unique<Catalog>();
    std::string msgid;
    std::string msgstr;
    std::string* current = nullptr;
    bool fuzzy = false;
    bool skipped = false;
    const auto flush = [&] {
        if (!msgid.empty() && !msgstr.empty() && !fuzzy && !skipped) {
            const std::string& key = catalog->storage.emplace_back(std::move(msgid));
            const std::string& value = catalog->storage.emplace_back(std::move(msgstr));
            catalog->entries.emplace(key, value.c_str());
        }
        msgid.clear();
        msgstr.clear();
        current = nullptr;
        fuzzy = false;
        skipped = false;
    };
    std::string line;
    while (std::getline(file, line)) {
        const std::string text = platform::Trim(line);
        if (text.empty()) {
            flush();
        } else if (text.front() == '#') {
            if (current == &msgstr) flush();
            if (text.starts_with("#,") && text.find("fuzzy") != std::string::npos) fuzzy = true;
        } else if (text.starts_with("msgctxt")) {
            if (current == &msgstr) flush();
            skipped = true;
            current = nullptr;
        } else if (text.starts_with("msgid_plural") || text.starts_with("msgstr[")) {
            skipped = true;
            current = nullptr;
        } else if (text.starts_with("msgid")) {
            if (current == &msgstr) flush();
            current = &msgid;
            AppendQuoted(text, msgid);
        } else if (text.starts_with("msgstr")) {
            current = &msgstr;
            AppendQuoted(text, msgstr);
        } else if (text.front() == '"' && current) {
            AppendQuoted(text, *current);
        }
    }
    flush();
    return catalog;
}

Language LanguageFromLocale(const char* value) {
    if (!value) return Language::English;
    if (std::strncmp(value, "ko", 2) == 0) return Language::Korean;
    if (std::strncmp(value, "ja", 2) == 0) return Language::Japanese;
    if (std::strncmp(value, "es", 2) == 0) return Language::Spanish;
    if (std::strncmp(value, "pt", 2) == 0) return Language::Portuguese;
    return Language::English;
}

}

void SetLanguage(Language language) {
    const Catalog* catalog = nullptr;
    if (const char* fileName = CatalogFile(language)) {
        const size_t index = static_cast<size_t>(language);
        if (!g_attempted[index]) {
            g_attempted[index] = true;
            const std::filesystem::path path = platform::FindTranslation(fileName);
            if (!path.empty()) g_catalogs[index] = LoadCatalog(path);
            if (g_catalogs[index]) {
                diagnostics::Log("[ui] %zu translations from %s", g_catalogs[index]->entries.size(), path.c_str());
            } else {
                diagnostics::Log("[ui] translation %s not found; showing English", fileName);
            }
        }
        catalog = g_catalogs[index].get();
    }
    g_catalog.store(catalog, std::memory_order_release);
    g_language.store(language, std::memory_order_relaxed);
}

Language CurrentLanguage() {
    return g_language.load(std::memory_order_relaxed);
}

Language SystemLanguage() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    if (locales) {
        const Language language = LanguageFromLocale(count > 0 && locales[0] ? locales[0]->language : nullptr);
        SDL_free(locales);
        return language;
    }
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable);
        if (value && *value) return LanguageFromLocale(value);
    }
    return Language::English;
}

const char* T(const char* text) {
    const Catalog* catalog = g_catalog.load(std::memory_order_acquire);
    if (!catalog) return text;
    const auto entry = catalog->entries.find(text);
    return entry == catalog->entries.end() ? text : entry->second;
}

}
