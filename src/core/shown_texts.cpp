#include "core/shown_texts.h"

#include <algorithm>
#include <utility>

namespace gp {

bool ShownTexts::Show(TextPlace place, VideoAreaText text, int durationMs, uint64_t nowMs) {
    const size_t i = static_cast<size_t>(place);
    if (i >= kPlaces) return false;
    const bool brief = durationMs > 0;
    const uint64_t endsAt = brief ? nowMs + static_cast<uint64_t>(durationMs) : 0;
    // The same text, lasting as before or brief as before: nothing to draw.
    if (texts_[i] == text && brief == (endsAt_[i] != 0)) {
        endsAt_[i] = endsAt;
        return false;
    }
    texts_[i] = std::move(text);
    endsAt_[i] = endsAt;
    return true;
}

void ShownTexts::ShowWithNextFrame(TextPlace place, VideoAreaText text) {
    const size_t i = static_cast<size_t>(place);
    if (i >= kPlaces) return;
    texts_[i] = std::move(text);
    endsAt_[i] = 0;
}

std::optional<uint64_t> ShownTexts::MsUntilNextEnd(uint64_t nowMs) const {
    std::optional<uint64_t> wait;
    for (uint64_t endsAt : endsAt_) {
        if (!endsAt) continue;
        const uint64_t left = endsAt > nowMs ? endsAt - nowMs : 0;
        wait = wait ? std::min(*wait, left) : left;
    }
    return wait;
}

bool ShownTexts::EndExpired(uint64_t nowMs) {
    bool ended = false;
    for (size_t i = 0; i < kPlaces; i++) {
        if (endsAt_[i] && endsAt_[i] <= nowMs) {
            texts_[i] = {};
            endsAt_[i] = 0;
            ended = true;
        }
    }
    return ended;
}

}  // namespace gp
