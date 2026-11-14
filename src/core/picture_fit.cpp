#include "core/picture_fit.h"

#include <cstdint>

namespace gp {

PictureRect FitPicture(int areaWidth, int areaHeight, int aspectW, int aspectH) {
    if (areaWidth <= 0 || areaHeight <= 0 || aspectW <= 0 || aspectH <= 0) return {};
    PictureRect r;
    // Compare areaWidth/areaHeight with aspectW/aspectH without dividing.
    if (static_cast<int64_t>(areaWidth) * aspectH <= static_cast<int64_t>(areaHeight) * aspectW) {
        // Area is as tall as the picture or taller: full width, bars above and below.
        r.width = areaWidth;
        r.height = static_cast<int>(static_cast<int64_t>(areaWidth) * aspectH / aspectW);
    } else {
        r.height = areaHeight;
        r.width = static_cast<int>(static_cast<int64_t>(areaHeight) * aspectW / aspectH);
    }
    r.x = (areaWidth - r.width) / 2;
    r.y = (areaHeight - r.height) / 2;
    return r;
}

}  // namespace gp
