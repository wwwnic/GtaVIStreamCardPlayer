// Where the Feed picture goes inside the Video Area.
#pragma once

namespace gp {

struct PictureRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PictureRect&) const = default;
};

// Largest rectangle of shape aspectW:aspectH that fits inside an area of
// areaWidth x areaHeight, centred. The rest of the area is left black.
// The picture is never stretched. An empty area gives an empty rectangle.
PictureRect FitPicture(int areaWidth, int areaHeight, int aspectW = 16, int aspectH = 9);

constexpr bool UseSharpUpscale(bool enabled, int feedWidth, int feedHeight, PictureRect shown) {
    return enabled && feedWidth > 0 && feedHeight > 0 &&
           shown.width >= feedWidth && shown.height >= feedHeight &&
           (shown.width > feedWidth || shown.height > feedHeight);
}

}  // namespace gp
