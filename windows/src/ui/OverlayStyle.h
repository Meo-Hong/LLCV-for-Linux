#pragma once
#include "AppPalette.h"
#include <d2d1.h>
#include <dwrite_3.h>
#include <wrl/client.h>

namespace llcv::overlay_ui {
// In-video UI stays dark, even when application chrome is light. HDR panels
// retain neutral black and 90% opacity; the existing compositor controls nits.
inline D2D1_COLOR_F Color(COLORREF value, float alpha = 1.0f) {
    return D2D1::ColorF(GetRValue(value) / 255.0f, GetGValue(value) / 255.0f,
                       GetBValue(value) / 255.0f, alpha);
}
inline constexpr auto kPalette = ui::PaletteForTheme(false);
inline D2D1_COLOR_F Background(bool hdr, float sdrAlpha = 0.90f) {
    return hdr ? D2D1::ColorF(0, 0.90f) : Color(kPalette.kBackground, sdrAlpha);
}

// Renderer-owned, initialization-only DirectWrite font collection. GDI's
// AddFontMemResourceEx does not expose private fonts to DirectWrite.
// No installed fonts, temporary files, network calls or per-frame loading.
class Fonts {
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<IDWriteFactory5> factory_;
    ComPtr<IDWriteInMemoryFontFileLoader> loader_;
    ComPtr<IDWriteFontCollection1> collection_;
    bool registered_ = false;
public:
    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    ~Fonts() { Reset(); }
    void Reset() {
        collection_.Reset();
        if (registered_) factory_->UnregisterFontFileLoader(loader_.Get());
        registered_ = false;
        loader_.Reset();
        factory_.Reset();
    }
    bool Available() const { return collection_ != nullptr; }
    HRESULT Initialize(IDWriteFactory* factory) {
        Reset();
        if (!factory) return E_INVALIDARG;
        HRESULT hr = factory->QueryInterface(IID_PPV_ARGS(&factory_));
        if (FAILED(hr)) return hr; // Older Windows can use the system fallback.
        hr = factory_->CreateInMemoryFontFileLoader(&loader_);
        if (FAILED(hr)) { Reset(); return hr; }
        hr = factory_->RegisterFontFileLoader(loader_.Get());
        if (FAILED(hr)) { Reset(); return hr; }
        registered_ = true;
        ComPtr<IDWriteFontSetBuilder1> builder;
        hr = factory_->CreateFontSetBuilder(&builder);
        const HMODULE module = GetModuleHandleW(nullptr);
        for (int id : {9001, 9004, 9002}) {
            if (FAILED(hr)) break;
            const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
            const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
            const void* bytes = loaded ? LockResource(loaded) : nullptr;
            if (!bytes) { hr = HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND); break; }
            ComPtr<IDWriteFontFile> file;
            // With no owner object DirectWrite copies the data. Loader lifetime
            // is explicit and never depends on a pointer to a temporary buffer.
            hr = loader_->CreateInMemoryFontFileReference(factory, bytes,
                SizeofResource(module, resource), nullptr, &file);
            if (SUCCEEDED(hr)) hr = builder->AddFontFile(file.Get());
        }
        ComPtr<IDWriteFontSet> set;
        if (SUCCEEDED(hr)) hr = builder->CreateFontSet(&set);
        if (SUCCEEDED(hr)) hr = factory_->CreateFontCollectionFromFontSet(set.Get(), &collection_);
        if (FAILED(hr)) Reset();
        return hr;
    }
    HRESULT CreateFormat(IDWriteFactory* factory, DWRITE_FONT_WEIGHT weight,
                         float size, bool english, IDWriteTextFormat** output) const {
        const wchar_t* family = english ? L"Segoe UI" : L"Malgun Gothic";
        IDWriteFontCollection* collection = nullptr;
        if (collection_) {
            const wchar_t* preferred = weight >= DWRITE_FONT_WEIGHT_SEMI_BOLD
                ? L"Pretendard SemiBold" : weight >= DWRITE_FONT_WEIGHT_MEDIUM
                ? L"Pretendard Medium" : L"Pretendard";
            for (const wchar_t* candidate : {preferred, L"Pretendard"}) {
                UINT32 index = 0; BOOL exists = FALSE;
                if (SUCCEEDED(collection_->FindFamilyName(candidate, &index, &exists)) && exists) {
                    family = candidate; collection = collection_.Get(); break;
                }
            }
        }
        return factory->CreateTextFormat(family, collection, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
            english ? L"en-US" : L"ko-KR", output);
    }
};
} // namespace llcv::overlay_ui
