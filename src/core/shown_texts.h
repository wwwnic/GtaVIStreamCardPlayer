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
    static constexpr size_t kPlaces = static_cast<size_t>(TextPlace::SessionPause) + 1;
    using Texts = std::array<VideoAreaText, kPlaces>;  // indexed by TextPlace

    // Shows `text` at `place`: until replaced (durationMs <= 0), or until
    // `durationMs` after now. A new text replaces the old in place. True when
    // the picture must be drawn again to show it; false when the same text
    // already shows (a held key: only its end moves).
    bool Show(TextPlace place, VideoAreaText text, int durationMs, uint64_t nowMs);

    // Changes the text without asking for a drawing (it goes out with the
    // next frame). It stays until replaced.
    void ShowWithNextFrame(TextPlace place, VideoAreaText text);

    // A notice at `place` for `durationMs` beside its text, not replacing it
    // nor replaced by it (Target's uneven-rate notice and the feedback line
    // come together): the smaller second line under the text's headline, or
    // the headline itself while no text shows there. True: draw again.
    bool ShowNotice(TextPlace place, std::wstring notice, int durationMs, uint64_t nowMs);

    // Milliseconds until the next brief text or notice ends (0: already
    // due); none when none shows.
    std::optional<uint64_t> MsUntilNextEnd(uint64_t nowMs) const;

    // Clears the brief texts and notices whose time is up. True when any was
    // cleared (the picture must be drawn again).
    bool EndExpired(uint64_t nowMs);

    // What each place shows: its text with its notice.
    Texts All() const;

private:
    Texts texts_;
    std::array<uint64_t, kPlaces> endsAt_ = {};  // 0: stays until replaced
    std::array<std::wstring, kPlaces> notices_;
    std::array<uint64_t, kPlaces> noticeEndsAt_ = {};
};

}  // namespace gp
