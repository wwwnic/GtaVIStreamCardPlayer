// Text drawn by the Viewer inside the Video Area: what it says and how big.
// The drawing itself is the Presenter's (Presenter::ShowText).
#pragma once

#include <cstdint>
#include <string>

namespace gp {

// Where a text sits. Each place holds one text at a time.
enum class TextPlace {
    Centre,  // the Video Area state message (ticket 06); hides nothing by itself
    Top,     // brief feedback, e.g. volume or Audio Delay (ticket 07)
    Bottom,  // a small lasting note, e.g. "no sound"
    TopLeft, // the FPS counter (ticket 04): small, left-aligned in the corner
    TopRight, // local clock, elapsed session time and their reminders
    SessionPause, // persistent reminder below the centre of the picture
};

// One line saying what is wrong, optionally a smaller line saying what to do.
// Light text, centred. Empty headline and detail: nothing shown.
struct VideoAreaText {
    std::wstring headline;
    std::wstring detail;
    float backingOpacity = 0.6f;
    bool Empty() const { return headline.empty() && detail.empty(); }
    bool operator==(const VideoAreaText&) const = default;
};

// Font sizes and margins, in pixels, for a Video Area of this size.
struct TextSizes {
    static constexpr float kMinHeadline = 14.0f;
    static constexpr float kMinDetail = 11.0f;
    float headline = 0;
    float detail = 0;
    float margin = 0;    // from the Video Area's edges
    float maxWidth = 0;  // lines wrap at this width
};

TextSizes SizeText(TextPlace place, int areaWidth, int areaHeight);
std::wstring SessionTimeText(const std::wstring& clock, uint64_t elapsedMinutes);
std::wstring DurationText(uint64_t minutes);

// Observe local wall-clock minutes even while the game is in the background.
// A missed reminder is shown once when the game is visible again.
class HalfHourReminder {
public:
    bool Due(int minuteOfDay, bool canShow);

private:
    int previousMinute_ = -1;
    bool pending_ = false;
};

class SessionReminder {
public:
    // Enabling mid-session starts with the next multiple of 30 minutes.
    void Reset(uint64_t elapsedMinutes) { delivered_ = elapsedMinutes / 30; }
    bool Due(uint64_t elapsedMinutes, bool canShow);

private:
    uint64_t delivered_ = 0;
};

}  // namespace gp
