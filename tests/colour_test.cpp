#include "doctest.h"

#include <array>
#include <cmath>

#include "core/colour.h"

using gp::Colour;
using gp::ColourMatrix;
using gp::ColourRange;
using gp::MakeYuvToRgb;

namespace {

// Expected values are the published 8-bit YCbCr codes for reference colours,
// not numbers derived from the code under test.
std::array<float, 3> Convert(Colour colour, int y, int cb, int cr) {
    return MakeYuvToRgb(colour).Apply(y / 255.0f, cb / 255.0f, cr / 255.0f);
}

void CheckRgb(const std::array<float, 3>& got, float r, float g, float b) {
    const float tolerance = 0.012f;  // about 3 of 255: the 8-bit codes are themselves rounded
    CHECK(std::fabs(got[0] - r) <= tolerance);
    CHECK(std::fabs(got[1] - g) <= tolerance);
    CHECK(std::fabs(got[2] - b) <= tolerance);
}

const Colour kLimited709{ColourRange::Limited, ColourMatrix::BT709};
const Colour kLimited601{ColourRange::Limited, ColourMatrix::BT601};
const Colour kFull709{ColourRange::Full, ColourMatrix::BT709};
const Colour kFull601{ColourRange::Full, ColourMatrix::BT601};

}  // namespace

TEST_CASE("limited range: code 16 is black and code 235 is white") {
    CheckRgb(Convert(kLimited709, 16, 128, 128), 0, 0, 0);
    CheckRgb(Convert(kLimited709, 235, 128, 128), 1, 1, 1);
    CheckRgb(Convert(kLimited601, 16, 128, 128), 0, 0, 0);
    CheckRgb(Convert(kLimited601, 235, 128, 128), 1, 1, 1);
}

TEST_CASE("full range: code 0 is black and code 255 is white") {
    CheckRgb(Convert(kFull709, 0, 128, 128), 0, 0, 0);
    CheckRgb(Convert(kFull709, 255, 128, 128), 1, 1, 1);
    CheckRgb(Convert(kFull601, 0, 128, 128), 0, 0, 0);
    CheckRgb(Convert(kFull601, 255, 128, 128), 1, 1, 1);
}

TEST_CASE("limited range BT.709 primaries convert to pure red, green and blue") {
    CheckRgb(Convert(kLimited709, 63, 102, 240), 1, 0, 0);
    CheckRgb(Convert(kLimited709, 173, 42, 26), 0, 1, 0);
    CheckRgb(Convert(kLimited709, 32, 240, 118), 0, 0, 1);
}

TEST_CASE("limited range BT.601 primaries convert to pure red, green and blue") {
    CheckRgb(Convert(kLimited601, 81, 90, 240), 1, 0, 0);
    CheckRgb(Convert(kLimited601, 145, 54, 34), 0, 1, 0);
    CheckRgb(Convert(kLimited601, 41, 240, 110), 0, 0, 1);
}

TEST_CASE("full range BT.601 (JPEG) primaries convert to pure red, green and blue") {
    CheckRgb(Convert(kFull601, 76, 85, 255), 1, 0, 0);
    CheckRgb(Convert(kFull601, 150, 44, 21), 0, 1, 0);
    CheckRgb(Convert(kFull601, 29, 255, 107), 0, 0, 1);
}

TEST_CASE("full range BT.709 red converts to pure red") {
    CheckRgb(Convert(kFull709, 54, 99, 255), 1, 0, 0);
}

TEST_CASE("an unspecified colour description is treated as limited range BT.709") {
    Colour unspecified;
    CheckRgb(Convert(unspecified, 16, 128, 128), 0, 0, 0);
    CheckRgb(Convert(unspecified, 235, 128, 128), 1, 1, 1);
    CheckRgb(Convert(unspecified, 63, 102, 240), 1, 0, 0);
}

TEST_CASE("the same bytes give a different picture when the matrix differs") {
    // BT.709 red read as BT.601 is visibly not pure red: this is why the
    // matrix must come from the capture mode.
    std::array<float, 3> wrong = Convert(kLimited601, 63, 102, 240);
    CHECK(std::fabs(wrong[1]) > 0.05f);
}
