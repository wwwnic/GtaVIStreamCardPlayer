// Draws Video Area texts (core/video_area_text.h) into the swap chain's back
// buffer with Direct2D and DirectWrite: light text, centred, with a dark
// backing so it stays readable over the picture. Used by the Presenter on its
// own thread; other code shows text through Presenter::ShowText.
#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>

#include <array>
#include <memory>

#include "core/shown_texts.h"
#include "core/video_area_text.h"

namespace gp {

class TextOverlay {
public:
    static constexpr size_t kPlaces = ShownTexts::kPlaces;  // one per TextPlace
    using Texts = ShownTexts::Texts;  // indexed by TextPlace

    TextOverlay();
    ~TextOverlay();
    TextOverlay(const TextOverlay&) = delete;
    TextOverlay& operator=(const TextOverlay&) = delete;

    // Ties the overlay to this Direct3D device (created with BGRA support).
    // False (reason logged) if Direct2D is not available: texts are then not drawn.
    bool Attach(ID3D11Device* device);
    // Lets go of every Direct2D object, before the device is released.
    void Detach();
    // Lets go of the back buffer, before the swap chain's buffers are resized.
    void ReleaseTarget();

    // Draws the non-empty texts over whatever the back buffer holds.
    void Draw(IDXGISwapChain1* swapChain, const Texts& texts, uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gp
