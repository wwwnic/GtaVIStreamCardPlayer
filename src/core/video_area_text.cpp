#include "core/video_area_text.h"

#include <algorithm>

namespace gp {

bool SessionReminder::Due(uint64_t elapsedMinutes, bool canShow) {
    const uint64_t halfHours = elapsedMinutes / 30;
    if (!canShow || halfHours <= delivered_) return false;
    delivered_ = halfHours;
    return true;
}

bool HalfHourReminder::Due(int minuteOfDay, bool canShow) {
    if (minuteOfDay != previousMinute_) {
        const bool crossed = previousMinute_ >= 0 && minuteOfDay / 30 == (previousMinute_ / 30 + 1) % 48;
        if (minuteOfDay % 30 == 0 || crossed) pending_ = true;
        previousMinute_ = minuteOfDay;
    }
    if (!pending_ || !canShow) return false;
    pending_ = false;
    return true;
}

std::wstring SessionTimeText(const std::wstring& clock, uint64_t elapsedMinutes) {
    std::wstring text = clock.empty() ? L"Session " : clock + L" · Session ";
    return text + DurationText(elapsedMinutes);
}

std::wstring DurationText(uint64_t minutes) {
    std::wstring text;
    if (minutes >= 60) text = std::to_wstring(minutes / 60) + L"h ";
    return text + std::to_wstring(minutes % 60) + L"m";
}

TextSizes SizeText(TextPlace place, int areaWidth, int areaHeight) {
    const float width = static_cast<float>(std::max(areaWidth, 1));
    const float height = static_cast<float>(std::max(areaHeight, 1));
    const bool centre = place == TextPlace::Centre;
    // A share of the Video Area's height (readable from a sofa at 1080p),
    // limited by its width so a narrow window wraps to few lines.
    const bool corner = place == TextPlace::TopLeft || place == TextPlace::TopRight;
    const float byHeight = height * (centre ? 0.065f : corner ? 0.025f : 0.04f);
    const float byWidth = width * (centre ? 0.045f : corner ? 0.02f : 0.03f);
    TextSizes sizes;
    sizes.headline = std::max(std::min(byHeight, byWidth), TextSizes::kMinHeadline);
    sizes.detail = std::max(sizes.headline * 0.55f, TextSizes::kMinDetail);
    sizes.margin = std::max(4.0f, std::min(width, height) * 0.04f);
    sizes.maxWidth = std::max(1.0f, width - 2 * sizes.margin);
    return sizes;
}

}  // namespace gp
