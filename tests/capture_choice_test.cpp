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

namespace {
const uint32_t k120 = 10000000, k120d = 83333;  // 120.000 fps as the GC553 Pro lists it
CaptureMode Mjpeg(uint32_t w, uint32_t h, uint32_t n, uint32_t d) { return {kOther, w, h, n, d, true}; }

// Excerpt of the GC553 Pro's real list (GamePlayer.log), in the card's order.
std::vector<CaptureMode> CardModes() {
    return {
        {PixelFormat::YUY2, 1920, 1080, 60, 1},              // 0
        {PixelFormat::YUY2, 1920, 1080, 60000, 1001},        // 1
        {PixelFormat::YUY2, 1280, 720, 60, 1},               // 2
        {PixelFormat::YUY2, 720, 576, 50, 1},                // 3
        {PixelFormat::YUY2, 720, 576, 30000, 1001},          // 4
        {PixelFormat::YUY2, 2560, 1440, 50, 1},              // 5
        {PixelFormat::NV12, 1920, 1080, 5000000, 20833},     // 6  240 fps, MJPEG-paired
        Mjpeg(1920, 1080, 5000000, 20833),                   // 7
        {PixelFormat::NV12, 3840, 2160, 60, 1},              // 8  MJPEG-paired
        Mjpeg(3840, 2160, 60, 1),                            // 9
        {PixelFormat::NV12, 1920, 1080, k120, k120d},        // 10 the unpaired 1080p120
        {PixelFormat::NV12, 1280, 720, 60, 1},               // 11
        {PixelFormat::NV12, 2560, 1440, 60, 1},              // 12
        {PixelFormat::NV12, 3840, 2160, 30, 1},              // 13
        {kOther, 640, 480, 60, 1},                           // 14 RGB24
    };
}
}  // namespace

TEST_CASE("the Resolution menu lists each uncompressed size at 60 fps or below, smallest first") {
    const std::vector<CaptureMode> modes = CardModes();
    const std::vector<gp::OfferedResolution> offered = gp::OfferedResolutions(modes);
    REQUIRE(offered.size() == 5);
    CHECK(offered[0].resolution == gp::Resolution{720, 576});
    CHECK(offered[1].resolution == gp::Resolution{1280, 720});
    CHECK(offered[2].resolution == gp::Resolution{1920, 1080});
    CHECK(offered[3].resolution == gp::Resolution{2560, 1440});
    CHECK(offered[4].resolution == gp::Resolution{3840, 2160});
}

TEST_CASE("each offered resolution captures at the rate nearest 60, YUY2 before NV12") {
    const std::vector<CaptureMode> modes = CardModes();
    const std::vector<gp::OfferedResolution> offered = gp::OfferedResolutions(modes);
    REQUIRE(offered.size() == 5);
    CHECK(offered[0].modeIndex == 3);   // 720x576 only at 50 and 29.97: 50
    CHECK(offered[1].modeIndex == 2);   // 1280x720 YUY2 60 over NV12 60
    CHECK(offered[2].modeIndex == 0);   // 1080p YUY2 60
    CHECK(offered[3].modeIndex == 12);  // 2560x1440 NV12 60 over YUY2 50
    CHECK(offered[4].modeIndex == 13);  // 4K: NV12 30, never the MJPEG-paired 60
}

TEST_CASE("MJPEG modes, their uncompressed twins and anything above 60 fps are never offered") {
    std::vector<CaptureMode> modes = {
        {PixelFormat::NV12, 1920, 1080, 5000000, 20833},
        Mjpeg(1920, 1080, 5000000, 20833),
        {PixelFormat::NV12, 3440, 1440, 60, 1},
        Mjpeg(3440, 1440, 60, 1),
        {PixelFormat::NV12, 1920, 1080, k120, k120d},
        Mjpeg(1280, 720, 30, 1),
    };
    CHECK(gp::OfferedResolutions(modes).empty());
}

TEST_CASE("a chosen resolution picks its offered mode; the default (none) is 1080p60") {
    const std::vector<CaptureMode> modes = CardModes();
    CHECK(ChooseCaptureMode(modes, gp::Resolution{2560, 1440}) == 12);
    CHECK(ChooseCaptureMode(modes, gp::Resolution{720, 576}) == 3);
    CHECK(ChooseCaptureMode(modes, std::nullopt) == 0);
}

TEST_CASE("a chosen resolution the card does not offer falls back to the default mode") {
    const std::vector<CaptureMode> modes = CardModes();
    CHECK(ChooseCaptureMode(modes, gp::Resolution{3440, 1440}) == 0);
    CHECK(ChooseCaptureMode(modes, gp::Resolution{640, 480}) == 0);  // RGB24 only here
}

TEST_CASE("a chosen Capture Card is used when present, otherwise the automatic choice") {
    std::vector<std::wstring> names = {L"Integrated Webcam", L"Live Gamer ULTRA S GC553Pro"};
    CHECK(ChooseCaptureDevice(names, L"Integrated Webcam") == 0);
    CHECK(ChooseCaptureDevice(names, L"Unplugged Card") == 1);
    CHECK(ChooseCaptureDevice(names, L"") == 1);
}

TEST_CASE("a remembered Capture Card is found by its identifier first, then by its name") {
    const std::vector<std::wstring> names = {L"USB Video", L"Live Gamer ULTRA S GC553Pro", L"USB Video"};
    const std::vector<std::wstring> ids = {L"\\\\?\\usb#a", L"\\\\?\\usb#gc553", L"\\\\?\\usb#b"};
    SUBCASE("the identifier tells two cards of the same name apart") {
        CHECK(ChooseCaptureDevice(names, ids, L"USB Video", L"\\\\?\\usb#b") == 2);
    }
    SUBCASE("the identifier wins even if the name changed") {
        CHECK(ChooseCaptureDevice(names, ids, L"Old name", L"\\\\?\\usb#a") == 0);
    }
    SUBCASE("a new identifier (another USB port): the name") {
        CHECK(ChooseCaptureDevice(names, ids, L"Live Gamer ULTRA S GC553Pro", L"\\\\?\\usb#moved") == 1);
    }
    SUBCASE("no identifier known yet: the name") {
        CHECK(ChooseCaptureDevice(names, ids, L"USB Video", L"") == 0);
    }
    SUBCASE("neither present: the automatic choice") {
        CHECK(ChooseCaptureDevice(names, ids, L"Unplugged Card", L"\\\\?\\usb#gone") == 1);
    }
    SUBCASE("nothing remembered: the automatic choice") {
        CHECK(ChooseCaptureDevice(names, ids, L"", L"") == 1);
    }
}
