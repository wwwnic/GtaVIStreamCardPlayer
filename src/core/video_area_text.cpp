#include "core/video_area_text.h"

#include <algorithm>

namespace gp {

TextSizes SizeText(TextPlace place, int areaWidth, int areaHeight) {
    const float width = static_cast<float>(std::max(areaWidth, 1));
    const float height = static_cast<float>(std::max(areaHeight, 1));
    const bool centre = place == TextPlace::Centre;
    // A share of the Video Area's height (readable from a sofa at 1080p),
    // limited by its width so a narrow window wraps to few lines.
    const bool corner = place == TextPlace::TopLeft;
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
