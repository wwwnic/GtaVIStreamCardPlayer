#include "doctest.h"

#include <string>
#include <vector>

#include "core/menu_model.h"

using gp::MenuItem;
using gp::PixelFormat;

namespace {
size_t CheckedCount(const std::vector<MenuItem>& items) {
    size_t count = 0;
    for (const MenuItem& item : items) count += item.checked ? 1 : 0;
    return count;
}
bool Contains(const std::wstring& text, const wchar_t* part) { return text.find(part) != std::wstring::npos; }
}  // namespace

TEST_CASE("the settings start at the zero-configuration defaults") {
    const gp::ViewerSettings settings;
    CHECK(settings.captureCard.empty());
    CHECK(settings.resolution == std::nullopt);
    CHECK(settings.frameRate == gp::GameRateOverride::Auto);
    CHECK(settings.audio.volumePercent == 100);
    CHECK_FALSE(settings.audio.muted);
    CHECK(settings.audio.audioDelayMs == 0);
    CHECK_FALSE(settings.alwaysOnTop);
    CHECK_FALSE(settings.vsync);
    CHECK_FALSE(settings.fpsCounter);
    CHECK(settings.captureCardId.empty());
}

TEST_CASE("Capture Card lists the present devices and checks the one in use") {
    const std::vector<gp::CaptureCardEntry> present = {{L"Integrated Webcam", L"\\\\?\\usb#cam"},
                                                       {L"Live Gamer ULTRA S GC553Pro", L"\\\\?\\usb#gc553"}};
    const std::vector<MenuItem> items =
        gp::CaptureCardItems(present, {L"Live Gamer ULTRA S GC553Pro", L"\\\\?\\usb#gc553"});
    REQUIRE(items.size() == 2);
    CHECK(items[0].label == L"Integrated Webcam");
    CHECK_FALSE(items[0].checked);
    CHECK(items[1].label == L"Live Gamer ULTRA S GC553Pro");
    CHECK(items[1].checked);
    CHECK(items[1].enabled);
}

TEST_CASE("two Capture Cards of the same name: numbered, and only the one in use is checked") {
    const std::vector<gp::CaptureCardEntry> present = {
        {L"USB Video", L"\\\\?\\usb#b"}, {L"Integrated Webcam", L"\\\\?\\usb#cam"}, {L"USB Video", L"\\\\?\\usb#a"}};
    const std::vector<MenuItem> items = gp::CaptureCardItems(present, {L"USB Video", L"\\\\?\\usb#a"});
    REQUIRE(items.size() == 3);
    CHECK(items[0].label == L"USB Video (1)");
    CHECK(items[1].label == L"Integrated Webcam");
    CHECK(items[2].label == L"USB Video (2)");
    CHECK_FALSE(items[0].checked);
    CHECK(items[2].checked);
    CHECK(CheckedCount(items) == 1);
}

TEST_CASE("a Capture Card with no identifier is checked by its name") {
    const std::vector<gp::CaptureCardEntry> present = {{L"Integrated Webcam", L""}, {L"USB Video", L""}};
    const std::vector<MenuItem> items = gp::CaptureCardItems(present, {L"USB Video", L"\\\\?\\usb#a"});
    CHECK(items[1].checked);
    CHECK(CheckedCount(items) == 1);
    CHECK(CheckedCount(gp::CaptureCardItems(present, {})) == 0);
}

TEST_CASE("Capture Card with no device shows one greyed line") {
    const std::vector<MenuItem> items = gp::CaptureCardItems({}, {});
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].enabled);
    CHECK_FALSE(items[0].checked);
}

TEST_CASE("Resolution starts with the checked default, then the offered sizes with their rates") {
    const std::vector<gp::OfferedResolution> offered = {
        {{720, 576}, 3, 50.0},
        {{1280, 720}, 2, 60.0},
        {{1920, 1080}, 0, 60.0},
    };
    const std::vector<MenuItem> items = gp::ResolutionItems(offered, std::nullopt);
    REQUIRE(items.size() == 4);
    CHECK(items[0].label == L"Default (1080p)");
    CHECK(items[0].checked);
    CHECK(items[1].label == L"720x576 (50 fps)");
    CHECK(items[2].label == L"1280x720 (60 fps)");
    CHECK(CheckedCount(items) == 1);
}

TEST_CASE("a chosen resolution is the one checked; one the card lacks falls back to the default's check") {
    const std::vector<gp::OfferedResolution> offered = {{{1280, 720}, 2, 59.94}, {{1920, 1080}, 0, 60.0}};
    std::vector<MenuItem> items = gp::ResolutionItems(offered, gp::Resolution{1280, 720});
    CHECK(items[1].checked);
    CHECK(items[1].label == L"1280x720 (59.94 fps)");
    CHECK(CheckedCount(items) == 1);
    items = gp::ResolutionItems(offered, gp::Resolution{3840, 2160});
    CHECK(items[0].checked);
    CHECK(CheckedCount(items) == 1);
}

TEST_CASE("Frame rate offers Auto, 30 and 60 only, with the override checked") {
    std::vector<MenuItem> items = gp::FrameRateItems(gp::GameRateOverride::Auto);
    REQUIRE(items.size() == 3);
    CHECK(Contains(items[0].label, L"Auto"));
    CHECK(Contains(items[1].label, L"30"));
    CHECK(Contains(items[2].label, L"60"));
    CHECK(items[0].checked);
    CHECK(CheckedCount(items) == 1);
    items = gp::FrameRateItems(gp::GameRateOverride::Fps60);
    CHECK(items[2].checked);
    CHECK(CheckedCount(items) == 1);
}

TEST_CASE("the Audio menu shows mute, volume and the Audio Delay as they are now") {
    gp::AudioSettings audio;
    audio.volumePercent = 65;
    audio.muted = true;
    audio.audioDelayMs = 150;
    const gp::AudioMenu menu = gp::AudioItems(audio);
    CHECK(menu.mute.checked);
    CHECK(Contains(menu.mute.label, L"\tM"));
    CHECK(menu.volume.label == L"Volume: 65% (muted)");
    CHECK_FALSE(menu.volume.enabled);
    CHECK(menu.audioDelay.label == L"Audio Delay: +150 ms");
    CHECK_FALSE(menu.audioDelay.enabled);
    CHECK(menu.resetDelay.enabled);
}

TEST_CASE("with no Audio Delay the reset is greyed and nothing is muted") {
    const gp::AudioMenu menu = gp::AudioItems(gp::AudioSettings{});
    CHECK_FALSE(menu.mute.checked);
    CHECK(menu.volume.label == L"Volume: 100%");
    CHECK(menu.audioDelay.label == L"Audio Delay: 0 ms");
    CHECK_FALSE(menu.resetDelay.enabled);
}

TEST_CASE("the View menu checks follow fullscreen, always on top and V-sync") {
    gp::ViewerSettings settings;
    gp::ViewMenu view = gp::ViewItems(settings, false);
    CHECK_FALSE(view.fullscreen.checked);
    CHECK(Contains(view.fullscreen.label, L"\tF11"));
    CHECK_FALSE(view.alwaysOnTop.checked);
    CHECK_FALSE(view.vsync.checked);
    settings.alwaysOnTop = true;
    settings.vsync = true;
    view = gp::ViewItems(settings, true);
    CHECK(view.fullscreen.checked);
    CHECK(view.alwaysOnTop.checked);
    CHECK(view.vsync.checked);
}

TEST_CASE("View > FPS counter shows the F key and is checked while the counter shows") {
    gp::ViewerSettings settings;
    gp::ViewMenu view = gp::ViewItems(settings, false);
    CHECK(view.fpsCounter.label == L"FPS counter\tF");
    CHECK_FALSE(view.fpsCounter.checked);
    CHECK(view.fpsCounter.enabled);
    settings.fpsCounter = true;
    view = gp::ViewItems(settings, false);
    CHECK(view.fpsCounter.checked);
}

TEST_CASE("Hotkeys lists all eight keys, each at the start of its line") {
    const std::wstring text = L"\n" + gp::HotkeysText();
    for (const wchar_t* key : {L"\nF11\t", L"\nEsc\t", L"\nM\t", L"\nUp / Down\t", L"\nJ / K\t", L"\nF\t"}) {
        CAPTURE(std::wstring(key));
        CHECK(Contains(text, key));
    }
}

TEST_CASE("About shows name, version, device, capture mode, Signal Rate and Game Rate") {
    gp::AboutInfo info;
    info.device = L"Live Gamer ULTRA S GC553Pro";
    info.mode = gp::CaptureMode{PixelFormat::YUY2, 1920, 1080, 60, 1};
    info.rates = {60, 30};
    const std::wstring text = gp::AboutText(info);
    CHECK(Contains(text, L"GamePlayer"));
    CHECK(Contains(text, gp::kViewerVersion));
    CHECK(Contains(text, L"Live Gamer ULTRA S GC553Pro"));
    CHECK(Contains(text, L"1920x1080 YUY2 at 60 fps"));
    CHECK(Contains(text, L"Signal Rate: 60"));
    CHECK(Contains(text, L"Game Rate: 30"));
}

TEST_CASE("About on Auto does not add a shown rate") {
    gp::AboutInfo info;
    info.rates = {60, 30, 30};
    CHECK_FALSE(Contains(gp::AboutText(info), L"Shown"));
}

TEST_CASE("About with a Frame rate override keeps the detected Game Rate and says what is shown") {
    gp::AboutInfo info;
    info.rates = {60, 60, 30};
    info.frameRate = gp::GameRateOverride::Fps30;
    const std::wstring text = gp::AboutText(info);
    CHECK(Contains(text, L"Signal Rate: 60"));
    CHECK(Contains(text, L"Game Rate: 60"));
    CHECK(Contains(text, L"Shown: 30 fps (Frame rate set to 30)"));
}

TEST_CASE("About says so when no card or mode is in use") {
    const std::wstring text = gp::AboutText(gp::AboutInfo{});
    CHECK(Contains(text, L"Capture Card: none\n"));
    CHECK(Contains(text, L"Capture mode: none"));
}

TEST_CASE("About names a card that is there but not playing (busy, failed)") {
    gp::AboutInfo info;
    info.device = L"Live Gamer ULTRA S GC553Pro";
    const std::wstring text = gp::AboutText(info);
    CHECK(Contains(text, L"Capture Card: Live Gamer ULTRA S GC553Pro (not playing)\n"));
    CHECK(Contains(text, L"Capture mode: none"));
    info.mode = gp::CaptureMode{PixelFormat::YUY2, 1920, 1080, 60, 1};
    CHECK_FALSE(Contains(gp::AboutText(info), L"not playing"));
}

TEST_CASE("G cycles from off to 2x and back") {
    const auto enabled = gp::NextGeneration(gp::GenerationFactor::Off);
    CHECK(enabled == gp::GenerationFactor::Double);
    CHECK(gp::NextGeneration(enabled) == gp::GenerationFactor::Off);
}

TEST_CASE("View shows Frame generation, its key, and whether V-sync is available") {
    gp::ViewerSettings settings;
    CHECK_FALSE(gp::ViewItems(settings, false).generation.checked);
    settings.generation = gp::GenerationFactor::Double;
    auto view = gp::ViewItems(settings, false);
    CHECK(view.generation.label == L"Frame generation\tG");
    CHECK(view.generation.checked);
    CHECK_FALSE(view.vsync.enabled);
    CHECK(gp::HotkeysText().find(L"G\tFrame generation") != std::wstring::npos);
    view = gp::ViewItems(settings, false, gp::GenerationAvailability::MissingRuntime);
    CHECK_FALSE(view.generation.enabled);
    CHECK_FALSE(view.generation.checked);
    CHECK(view.generation.label.find(L"AMD") != std::wstring::npos);
    CHECK(view.vsync.enabled);
    view = gp::ViewItems(settings, false, gp::GenerationAvailability::Failed);
    CHECK_FALSE(view.generation.enabled);
    CHECK(view.generation.label.find(L"not available") != std::wstring::npos);
}
