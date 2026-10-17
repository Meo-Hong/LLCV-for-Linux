#include "recording/RecordingGpuIndicator.h"
#include "recording/Recorder.h"
#include "recording/RecordingStatusPresentation.h"
#include "ui/AppPalette.h"
#include "ui/OverlayStyle.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace llcv::recording {
namespace {
constexpr UINT kWidth = 380, kHeight = 128;
struct Frame {
    wchar_t text[1024]{};
    wchar_t actions[96]{};
    COLORREF card = 0, edge = 0, foreground = 0, actionColor = 0;
    UINT dpi = 96;
    bool visible = false, terminal = false, problem = false;
    bool canOpenVideo = false, english = false;
};
struct Publication {
    std::mutex mutex;
    Frame frame{};
    Snapshot previous{}; // UI-only transition signature; never copied by render.
    uint64_t terminalUntil = 0;
    std::atomic<uint64_t> revision{0};
};
Publication publication;

bool Terminal(const Snapshot& snapshot) {
    return !snapshot.busy && (snapshot.phase == Phase::Saved ||
                              snapshot.phase == Phase::Failed);
}

bool SameFrame(const Frame& a, const Frame& b) {
    return a.dpi == b.dpi && a.visible == b.visible &&
        a.terminal == b.terminal && a.problem == b.problem &&
        a.canOpenVideo == b.canOpenVideo && a.english == b.english &&
        a.card == b.card && a.edge == b.edge &&
        a.foreground == b.foreground && a.actionColor == b.actionColor &&
        wcscmp(a.text, b.text) == 0 && wcscmp(a.actions, b.actions) == 0;
}

Frame ReadFrame() {
    std::lock_guard<std::mutex> lock(publication.mutex);
    return publication.frame;
}

float Scale(UINT dpi, UINT width, UINT height) {
    // Existing HDR overlay scratch is 700 x 440. Keep the transparent quad
    // within that allocation even at high DPI, without stretching its text.
    return (std::min)({dpi / 96.0f,
        700.0f / kWidth, 440.0f / kHeight,
        width / static_cast<float>(kWidth),
        height / static_cast<float>(kHeight)});
}

RECT Quad(UINT dpi, UINT width, UINT height) {
    if (!width || !height) return {};
    const float scale = Scale(dpi, width, height);
    const LONG quadWidth = (std::max)(1L, static_cast<LONG>(kWidth * scale));
    const LONG quadHeight = (std::max)(1L, static_cast<LONG>(kHeight * scale));
    const LONG margin = static_cast<LONG>(12 * scale);
    const LONG left = (std::max)(0L, static_cast<LONG>(width) - quadWidth - margin);
    const LONG top = (std::max)(0L, static_cast<LONG>(height) - quadHeight - margin);
    return {left, top, left + quadWidth, top + quadHeight};
}

D2D1_RECT_F Card(const Frame& frame) {
    const float width = frame.terminal ? 380.0f : 200.0f;
    const float height = frame.terminal ? (frame.problem ? 128.0f : 68.0f) : 36.0f;
    return D2D1::RectF(kWidth - width, kHeight - height,
                       static_cast<float>(kWidth), static_cast<float>(kHeight));
}

bool Hit(const Frame& frame, UINT width, UINT height, int x, int y,
         float& localX, float& localY) {
    if (!frame.visible || !width || !height || x < 0 || y < 0 ||
        static_cast<UINT>(x) >= width || static_cast<UINT>(y) >= height) return false;
    const RECT quad = Quad(frame.dpi, width, height);
    const float scale = Scale(frame.dpi, width, height);
    localX = (x - quad.left) / scale;
    localY = (y - quad.top) / scale;
    const auto card = Card(frame);
    return localX >= card.left && localX < card.right &&
        localY >= card.top && localY < card.bottom;
}
} // namespace

void PublishGpuIndicator(const Snapshot& snapshot, bool english, bool light, UINT dpi) {
    const bool transition = publication.previous.phase != snapshot.phase ||
        publication.previous.path != snapshot.path ||
        publication.previous.failure != snapshot.failure ||
        publication.previous.busy != snapshot.busy;
    const uint64_t now = GetTickCount64();
    if (transition && Terminal(snapshot)) publication.terminalUntil = now + 10000;
    publication.previous = snapshot;

    Frame frame{};
    frame.dpi = dpi ? dpi : 96;
    frame.english = english;
    frame.terminal = Terminal(snapshot);
    frame.problem = snapshot.failure != Failure::None || snapshot.phase == Phase::Failed;
    frame.visible = snapshot.busy || (frame.terminal && now < publication.terminalUntil);
    const auto palette = ui::ResolvePalette(light, ui::HighContrastEnabled());
    frame.card = palette.kCard;
    frame.edge = palette.kCardEdge;
    frame.foreground = snapshot.phase == Phase::Recording || frame.problem
        ? palette.kDanger : palette.kText;
    frame.actionColor = palette.kText;
    if (frame.visible) {
        const auto view = presentation::Build(snapshot, english);
        frame.canOpenVideo = view.canOpenVideo;
        std::wstring text;
        if (snapshot.phase == Phase::Recording) {
            text = (snapshot.paused ? (english ? L"Ⅱ PAUSED  " : L"Ⅱ 일시정지  ") : L"● REC  ") +
                presentation::Elapsed(snapshot.elapsedMs);
        } else {
            text = view.title;
            if (frame.terminal && frame.problem) text += L"\n" + view.description;
        }
        wcsncpy_s(frame.text, text.c_str(), _TRUNCATE);
        if (frame.terminal && view.canOpenVideo)
            wcscpy_s(frame.actions, english ? L"Open video    ·    Open folder"
                                           : L"영상 열기    ·    폴더 열기");
    }
    std::lock_guard<std::mutex> lock(publication.mutex);
    if (SameFrame(frame, publication.frame)) return;
    publication.frame = frame;
    publication.revision.fetch_add(1, std::memory_order_release);
}

uint64_t GpuIndicatorRevision() noexcept {
    return publication.revision.load(std::memory_order_acquire);
}

bool GpuIndicatorHitTest(UINT width, UINT height, int x, int y) {
    const auto frame = ReadFrame();
    float localX{}, localY{};
    return Hit(frame, width, height, x, y, localX, localY);
}

bool HandleGpuIndicatorClick(HWND viewer, UINT width, UINT height, int x, int y) {
    const auto frame = ReadFrame();
    float localX{}, localY{};
    if (!Hit(frame, width, height, x, y, localX, localY)) return false;
    const auto card = Card(frame);
    if (frame.terminal) {
        if (localY >= card.bottom - 32 && frame.canOpenVideo && localX - card.left < 220)
            OpenRecording(viewer, localX - card.left < 105, frame.english);
    } else {
        PostMessageW(viewer, kOpenRecordingSettings, 0, 0);
    }
    return true;
}

HRESULT GpuIndicator::Initialize(ID3D11Device* device, ID2D1Factory* d2d,
                                 IDWriteFactory* dwrite,
                                 const overlay_ui::Fonts& fonts, bool english) {
    Reset();
    if (!device || !d2d || !dwrite) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth; desc.Height = kHeight;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture_);
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(texture_.Get(), nullptr, &view_);
    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    if (SUCCEEDED(hr)) hr = texture_.As(&surface);
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (SUCCEEDED(hr)) hr = d2d->CreateDxgiSurfaceRenderTarget(surface.Get(), properties, &target_);
    if (SUCCEEDED(hr)) hr = target_->CreateSolidColorBrush(D2D1::ColorF(0, 1.0f), &background_);
    if (SUCCEEDED(hr)) hr = target_->CreateSolidColorBrush(D2D1::ColorF(0, 1.0f), &edge_);
    if (SUCCEEDED(hr)) hr = target_->CreateSolidColorBrush(D2D1::ColorF(0, 1.0f), &text_);
    if (SUCCEEDED(hr)) hr = fonts.CreateFormat(dwrite, DWRITE_FONT_WEIGHT_MEDIUM,
        40.0f / 3.0f, english, &format_);
    if (SUCCEEDED(hr)) {
        format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    } else {
        Reset();
    }
    return hr;
}

void GpuIndicator::Reset() noexcept {
    format_.Reset(); text_.Reset(); edge_.Reset(); background_.Reset();
    target_.Reset(); view_.Reset(); texture_.Reset();
    revision_ = UINT64_MAX; visible_ = false; dpi_ = 96;
}

HRESULT GpuIndicator::Refresh() {
    const uint64_t revision = GpuIndicatorRevision();
    if (revision == revision_) return S_OK;
    const auto frame = ReadFrame();
    visible_ = frame.visible;
    dpi_ = frame.dpi;
    if (!visible_) { revision_ = revision; return S_OK; }
    if (!target_ || !format_) return E_UNEXPECTED;
    const auto card = Card(frame);
    background_->SetColor(overlay_ui::Color(frame.card));
    edge_->SetColor(overlay_ui::Color(frame.edge));
    text_->SetColor(overlay_ui::Color(frame.foreground));
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0.0f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(card, 7.0f, 7.0f), background_.Get());
    const auto edge = D2D1::RectF(card.left + .5f, card.top + .5f,
                                 card.right - .5f, card.bottom - .5f);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(edge, 7.0f, 7.0f), edge_.Get(), 1.0f);
    const auto text = D2D1::RectF(card.left + 12, card.top + 8,
        card.right - 12, card.bottom - (frame.terminal ? 36.0f : 8.0f));
    target_->DrawTextW(frame.text, static_cast<UINT32>(wcslen(frame.text)),
        format_.Get(), text, text_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (frame.actions[0]) {
        text_->SetColor(overlay_ui::Color(frame.actionColor));
        const auto actions = D2D1::RectF(card.left + 12, card.bottom - 28,
                                        card.right - 12, card.bottom);
        target_->DrawTextW(frame.actions, static_cast<UINT32>(wcslen(frame.actions)),
            format_.Get(), actions, text_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    const HRESULT hr = target_->EndDraw();
    if (SUCCEEDED(hr)) revision_ = revision;
    return hr;
}

RECT GpuIndicator::QuadRect(UINT width, UINT height) const noexcept {
    return Quad(dpi_, width, height);
}

} // namespace llcv::recording
