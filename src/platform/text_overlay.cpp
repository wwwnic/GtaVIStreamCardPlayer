#include "platform/text_overlay.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <map>

#include "platform/log.h"

using Microsoft::WRL::ComPtr;

namespace gp {

struct TextOverlay::Impl {
    ComPtr<ID2D1Factory1> factory;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1Bitmap1> target;  // the back buffer
    ComPtr<ID2D1SolidColorBrush> headlineBrush, detailBrush, backingBrush;
    std::map<std::pair<float, bool>, ComPtr<IDWriteTextFormat>> formats;  // (size, bold)

    IDWriteTextFormat* Format(float size, bool bold) {
        if (formats.size() > 16) formats.clear();  // sizes follow the window; keep only recent ones
        ComPtr<IDWriteTextFormat>& format = formats[{size, bold}];
        if (!format) {
            if (FAILED(writeFactory->CreateTextFormat(L"Segoe UI", nullptr,
                                                      bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-gb",
                                                      &format))) {
                return nullptr;
            }
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        return format.Get();
    }

    ComPtr<IDWriteTextLayout> Layout(const std::wstring& text, float size, bool bold, float maxWidth) {
        ComPtr<IDWriteTextLayout> layout;
        IDWriteTextFormat* format = Format(size, bold);
        if (text.empty() || !format) return layout;
        writeFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, maxWidth, 10000.0f,
                                       &layout);
        return layout;
    }

    bool EnsureTarget(IDXGISwapChain1* swapChain) {
        if (target) return true;
        ComPtr<IDXGISurface> surface;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        const HRESULT hr = context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target);
        if (FAILED(hr)) {
            Log("Text: no Direct2D target hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }

    void DrawOne(TextPlace place, const VideoAreaText& text, uint32_t width, uint32_t height) {
        const TextSizes sizes = SizeText(place, static_cast<int>(width), static_cast<int>(height));
        ComPtr<IDWriteTextLayout> headline = Layout(text.headline, sizes.headline, true, sizes.maxWidth);
        ComPtr<IDWriteTextLayout> detail = Layout(text.detail, sizes.detail, false, sizes.maxWidth);
        DWRITE_TEXT_METRICS headlineSize = {}, detailSize = {};
        if (headline) headline->GetMetrics(&headlineSize);
        if (detail) detail->GetMetrics(&detailSize);
        const float gap = headline && detail ? sizes.detail * 0.5f : 0.0f;
        const float blockHeight = headlineSize.height + gap + detailSize.height;
        const float blockWidth = std::max(headlineSize.widthIncludingTrailingWhitespace,
                                          detailSize.widthIncludingTrailingWhitespace);
        float top = 0;
        switch (place) {
        case TextPlace::Centre: top = (static_cast<float>(height) - blockHeight) / 2; break;
        case TextPlace::Top: top = sizes.margin; break;
        case TextPlace::Bottom: top = static_cast<float>(height) - sizes.margin - blockHeight; break;
        }
        const float left = sizes.margin;
        // A dark rounded backing, so the text reads over a bright picture too.
        const float pad = sizes.detail * 0.6f;
        const float centreX = static_cast<float>(width) / 2;
        const D2D1_ROUNDED_RECT backing = {
            D2D1::RectF(centreX - blockWidth / 2 - pad, top - pad / 2, centreX + blockWidth / 2 + pad,
                        top + blockHeight + pad / 2),
            pad / 2, pad / 2};
        context->FillRoundedRectangle(backing, backingBrush.Get());
        if (headline) context->DrawTextLayout(D2D1::Point2F(left, top), headline.Get(), headlineBrush.Get());
        if (detail) {
            context->DrawTextLayout(D2D1::Point2F(left, top + headlineSize.height + gap), detail.Get(),
                                    detailBrush.Get());
        }
    }
};

TextOverlay::TextOverlay() = default;
TextOverlay::~TextOverlay() { Detach(); }

bool TextOverlay::Attach(ID3D11Device* device) {
    Detach();
    auto impl = std::make_unique<Impl>();
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                                   reinterpret_cast<void**>(impl->factory.GetAddressOf()));
    if (SUCCEEDED(hr)) {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(impl->writeFactory.GetAddressOf()));
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    if (SUCCEEDED(hr)) hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr)) hr = impl->factory->CreateDevice(dxgiDevice.Get(), &impl->device);
    if (SUCCEEDED(hr)) hr = impl->device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &impl->context);
    if (SUCCEEDED(hr)) hr = impl->context->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.93f, 0.93f), &impl->headlineBrush);
    if (SUCCEEDED(hr)) hr = impl->context->CreateSolidColorBrush(D2D1::ColorF(0.72f, 0.72f, 0.72f), &impl->detailBrush);
    if (SUCCEEDED(hr)) hr = impl->context->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &impl->backingBrush);
    if (FAILED(hr)) {
        Log("Text: Direct2D/DirectWrite not available hr=0x%08lX; no text in the Video Area",
            static_cast<unsigned long>(hr));
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

void TextOverlay::Detach() {
    if (!impl_) return;
    if (impl_->context) impl_->context->SetTarget(nullptr);
    impl_.reset();
}

void TextOverlay::ReleaseTarget() {
    if (!impl_) return;
    impl_->context->SetTarget(nullptr);
    impl_->target.Reset();
}

void TextOverlay::Draw(IDXGISwapChain1* swapChain, const Texts& texts, uint32_t width, uint32_t height) {
    if (!impl_ || width == 0 || height == 0) return;
    if (std::all_of(texts.begin(), texts.end(), [](const VideoAreaText& text) { return text.Empty(); })) return;
    if (!impl_->EnsureTarget(swapChain)) return;
    ID2D1DeviceContext* context = impl_->context.Get();
    context->SetTarget(impl_->target.Get());
    context->BeginDraw();
    for (size_t i = 0; i < kPlaces; i++) {
        if (!texts[i].Empty()) impl_->DrawOne(static_cast<TextPlace>(i), texts[i], width, height);
    }
    const HRESULT hr = context->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseTarget();
    } else if (FAILED(hr)) {
        Log("Text: EndDraw failed hr=0x%08lX", static_cast<unsigned long>(hr));
    }
    context->SetTarget(nullptr);
}

}  // namespace gp
