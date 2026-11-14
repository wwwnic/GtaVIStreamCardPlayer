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

namespace {
const std::wstring kCard = L"Live Gamer ULTRA S GC553Pro";
}

TEST_CASE("the card's HDMI sound endpoint is chosen even when \"Not To Use\" is listed first") {
    std::vector<std::wstring> endpoints = {
        L"Microphone (Realtek Audio)",
        L"Not To Use (Live Gamer ULTRA S GC553Pro)",
        L"HDMI (Live Gamer ULTRA S GC553Pro)",
    };
    const auto choice = gp::ChooseSoundEndpoint(endpoints, kCard);
    REQUIRE(choice);
    CHECK(choice->index == 2);
    CHECK(choice->byCardName);
}

TEST_CASE("the card's HDMI sound endpoint is chosen when listed first too") {
    std::vector<std::wstring> endpoints = {
        L"HDMI (Live Gamer ULTRA S GC553Pro)",
        L"Not To Use (Live Gamer ULTRA S GC553Pro)",
    };
    const auto choice = gp::ChooseSoundEndpoint(endpoints, kCard);
    REQUIRE(choice);
    CHECK(choice->index == 0);
}

TEST_CASE("\"Not To Use\" is never chosen, even when it is the only endpoint matching the card") {
    std::vector<std::wstring> endpoints = {
        L"Microphone (Realtek Audio)",
        L"Not To Use (Live Gamer ULTRA S GC553Pro)",
    };
    CHECK(gp::ChooseSoundEndpoint(endpoints, kCard) == std::nullopt);
}

TEST_CASE("without an HDMI endpoint the first endpoint matching the card is chosen, ignoring case") {
    std::vector<std::wstring> endpoints = {
        L"Microphone (Realtek Audio)",
        L"not to use (live gamer ultra s gc553pro)",
        L"Line (live gamer ultra s gc553pro)",
        L"Other (Live Gamer ULTRA S GC553Pro)",
    };
    const auto choice = gp::ChooseSoundEndpoint(endpoints, kCard);
    REQUIRE(choice);
    CHECK(choice->index == 2);
    CHECK(choice->byCardName);
}

TEST_CASE("when no endpoint matches the card, the first that looks like a capture device is the fallback") {
    std::vector<std::wstring> endpoints = {
        L"Microphone (Realtek Audio)",
        L"Not To Use (Some Capture Card)",
        L"HDMI (Some Capture Card)",
    };
    const auto choice = gp::ChooseSoundEndpoint(endpoints, kCard);
    REQUIRE(choice);
    CHECK(choice->index == 2);
    CHECK_FALSE(choice->byCardName);
}

TEST_CASE("the fallback also works when no Capture Card name is known") {
    std::vector<std::wstring> endpoints = {L"Microphone (Realtek Audio)", L"Digital Audio Interface (USB Capture)"};
    const auto choice = gp::ChooseSoundEndpoint(endpoints, L"");
    REQUIRE(choice);
    CHECK(choice->index == 1);
    CHECK_FALSE(choice->byCardName);
}

TEST_CASE("a plain microphone is never chosen as the Feed sound") {
    std::vector<std::wstring> endpoints = {L"Microphone (Realtek Audio)", L"Headset Microphone (USB Headset)"};
    CHECK(gp::ChooseSoundEndpoint(endpoints, kCard) == std::nullopt);
    CHECK(gp::ChooseSoundEndpoint({}, kCard) == std::nullopt);
}

TEST_CASE("with the card unplugged, the fallback skips microphones and virtual devices that only mention the maker") {
    // The real endpoint list on the user's PC, minus the card's own two.
    std::vector<std::wstring> endpoints = {
        L"Réseau de microphones (Voice.ai Audio Cable)",
        L"Microphone (Realtek USB2.0 Audio)",
        L"Virtual Microphone (AVerMedia Streaming Center)",
        L"Microphone (HD Pro Webcam C920)",
    };
    CHECK(gp::ChooseSoundEndpoint(endpoints, kCard) == std::nullopt);
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
