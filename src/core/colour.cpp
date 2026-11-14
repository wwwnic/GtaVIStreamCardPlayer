#include "core/colour.h"

namespace gp {

std::array<float, 3> YuvToRgb::Apply(float y, float u, float v) const {
    std::array<float, 3> rgb{};
    for (int i = 0; i < 3; i++) rgb[i] = rows[i][0] * y + rows[i][1] * u + rows[i][2] * v + rows[i][3];
    return rgb;
}

YuvToRgb MakeYuvToRgb(Colour colour) {
    // Luma weights of the standard; green's is what remains.
    const bool bt601 = colour.matrix == ColourMatrix::BT601;
    const double kr = bt601 ? 0.299 : 0.2126;
    const double kb = bt601 ? 0.114 : 0.0722;
    const double kg = 1.0 - kr - kb;

    // Stored codes -> luma 0..1 and chroma -0.5..0.5.
    const bool full = colour.range == ColourRange::Full;
    const double yOffset = full ? 0.0 : 16.0 / 255.0;
    const double yScale = full ? 1.0 : 255.0 / 219.0;
    const double cOffset = 128.0 / 255.0;
    const double cScale = full ? 1.0 : 255.0 / 224.0;

    // Per-channel weights of (luma, Cb, Cr).
    const double weights[3][3] = {
        {1.0, 0.0, 2.0 * (1.0 - kr)},
        {1.0, -2.0 * kb * (1.0 - kb) / kg, -2.0 * kr * (1.0 - kr) / kg},
        {1.0, 2.0 * (1.0 - kb), 0.0},
    };

    YuvToRgb result;
    for (int i = 0; i < 3; i++) {
        const double y = weights[i][0] * yScale;
        const double u = weights[i][1] * cScale;
        const double v = weights[i][2] * cScale;
        result.rows[i] = {static_cast<float>(y), static_cast<float>(u), static_cast<float>(v),
                          static_cast<float>(-(y * yOffset + (u + v) * cOffset))};
    }
    return result;
}

}  // namespace gp
