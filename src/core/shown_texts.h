// The texts the Video Area shows, one per place, and when the brief ones
// (feedback such as "Volume 80%") end. The Presenter keeps one of these
// under its lock and passes the clock in; it draws what this says and
// presents only when this says the picture changed. Times in milliseconds.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/video_area_text.h"

namespace gp {

class ShownTexts {
public:
    static constexpr size_t kPlaces = 4;  // one per TextPlace
    using Texts = std::array<VideoAreaText, kPlaces>;  // indexed by TextPlace

    // Shows `text` at `place`: until replaced (durationMs <= 0), or until
    // `durationMs` after now. A new text replaces the old in place. True when
    // the picture must be drawn again to show it; false when the same text
    // already shows (a held key: only its end moves).
    bool Show(TextPlace place, VideoAreaText text, int durationMs, uint64_t nowMs);

    // Changes the text without asking for a drawing (it goes out with the
    // next frame). It stays until replaced.
    void ShowWithNextFrame(TextPlace place, VideoAreaText text);

    // Milliseconds until the next brief text ends (0: already due); none when
    // no brief text shows.
    std::optional<uint64_t> MsUntilNextEnd(uint64_t nowMs) const;

    // Clears the brief texts whose time is up. True when any was cleared (the
    // picture must be drawn again).
    bool EndExpired(uint64_t nowMs);

    const Texts& All() const { return texts_; }

private:
    Texts texts_;
    std::array<uint64_t, kPlaces> endsAt_ = {};  // 0: stays until replaced
};

}  // namespace gp
