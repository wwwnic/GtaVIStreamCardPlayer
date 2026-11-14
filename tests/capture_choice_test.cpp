#include "doctest.h"

#include <string>
#include <vector>

#include "core/capture_choice.h"

using gp::CaptureMode;
using gp::ChooseCaptureDevice;
using gp::ChooseCaptureMode;
using gp::PixelFormat;

namespace {
const std::optional<PixelFormat> kOther;  // MJPEG, RGB24, P010...
}

TEST_CASE("the GC553 Pro is chosen by name even when it is not listed first") {
    std::vector<std::wstring> names = {L"Integrated Webcam", L"Live Gamer ULTRA S GC553Pro", L"OBS Virtual Camera"};
    CHECK(ChooseCaptureDevice(names) == 1);
}

TEST_CASE("the GC553 Pro name match ignores letter case") {
    std::vector<std::wstring> names = {L"Integrated Webcam", L"live gamer ultra s gc553pro"};
    CHECK(ChooseCaptureDevice(names) == 1);
}

TEST_CASE("without a GC553 Pro the first capture device is chosen") {
    std::vector<std::wstring> names = {L"Integrated Webcam", L"Some Other Card"};
    CHECK(ChooseCaptureDevice(names) == 0);
}

TEST_CASE("with no capture devices nothing is chosen") {
    CHECK(ChooseCaptureDevice({}) == std::nullopt);
}

TEST_CASE("YUY2 1080p60 is chosen over NV12 1080p60 wherever it sits in the list") {
    std::vector<CaptureMode> modes = {
        {PixelFormat::NV12, 1920, 1080, 60, 1},
        {PixelFormat::YUY2, 1280, 720, 60, 1},
        {PixelFormat::YUY2, 1920, 1080, 60, 1},
    };
    CHECK(ChooseCaptureMode(modes) == 2);
}

TEST_CASE("NV12 1080p60 is chosen when there is no YUY2 1080p60") {
    std::vector<CaptureMode> modes = {
        {PixelFormat::YUY2, 1920, 1080, 30, 1},
        {kOther, 1920, 1080, 60, 1},
        {PixelFormat::NV12, 1920, 1080, 60, 1},
    };
    CHECK(ChooseCaptureMode(modes) == 2);
}

TEST_CASE("exactly 60 fps is chosen over 59.94 fps in the same format") {
    std::vector<CaptureMode> modes = {
        {PixelFormat::YUY2, 1920, 1080, 60000, 1001},
        {PixelFormat::YUY2, 1920, 1080, 60, 1},
    };
    CHECK(ChooseCaptureMode(modes) == 1);
}

TEST_CASE("59.94 fps is accepted when exactly 60 is not offered") {
    std::vector<CaptureMode> modes = {
        {PixelFormat::YUY2, 1920, 1080, 50, 1},
        {PixelFormat::YUY2, 1920, 1080, 60000, 1001},
    };
    CHECK(ChooseCaptureMode(modes) == 1);
}

TEST_CASE("YUY2 at 59.94 fps is chosen over NV12 at exactly 60") {
    std::vector<CaptureMode> modes = {
        {PixelFormat::NV12, 1920, 1080, 60, 1},
        {PixelFormat::YUY2, 1920, 1080, 60000, 1001},
    };
    CHECK(ChooseCaptureMode(modes) == 1);
}

TEST_CASE("compressed, other-size and other-rate modes are never chosen") {
    std::vector<CaptureMode> modes = {
        {kOther, 1920, 1080, 60, 1},               // MJPEG
        {PixelFormat::NV12, 1920, 1080, 120, 1},   // wrong rate
        {PixelFormat::YUY2, 2560, 1440, 60, 1},    // wrong size
        {PixelFormat::YUY2, 1920, 1080, 30, 1},    // wrong rate
        {PixelFormat::YUY2, 1920, 1080, 60, 0},    // broken rate
    };
    CHECK(ChooseCaptureMode(modes) == std::nullopt);
}
