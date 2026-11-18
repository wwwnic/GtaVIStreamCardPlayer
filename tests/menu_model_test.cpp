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

TEST_CASE("the Target rate menu: Monitor with its refresh, three presets, Custom") {
    std::vector<MenuItem> items = gp::TargetRateItems(gp::kTargetRateMonitor, gp::kNoCustomTargetRate, 144);
    REQUIRE(items.size() == 5);
    CHECK(items[0].label == L"Monitor (144)");
    CHECK(items[1].label == L"120");
    CHECK(items[2].label == L"72");
    CHECK(items[3].label == L"60");
    CHECK(items[4].label == L"Custom…");  // nothing typed yet
    CHECK(items[0].checked);
    CHECK(CheckedCount(items) == 1);
    for (int i = 0; i < 5; i++) CHECK(items[i].enabled);

    items = gp::TargetRateItems(72, gp::kNoCustomTargetRate, 59);
    CHECK(items[0].label == L"Monitor (59)");
    CHECK(items[2].checked);
    CHECK(CheckedCount(items) == 1);
}

TEST_CASE("the Target rate menu shows the custom number, kept under a preset") {
    std::vector<MenuItem> items = gp::TargetRateItems(100, 100, 144);
    CHECK(items[4].label == L"Custom (100)");
    CHECK(items[4].checked);
    CHECK(CheckedCount(items) == 1);

    items = gp::TargetRateItems(60, 100, 144);  // a preset picked later
    CHECK(items[4].label == L"Custom (100)");
    CHECK_FALSE(items[4].checked);
    CHECK(items[3].checked);
    CHECK(CheckedCount(items) == 1);

    items = gp::TargetRateItems(gp::kTargetRateMonitor, 100, 144);
    CHECK(items[4].label == L"Custom (100)");
    CHECK(items[0].checked);
    CHECK(CheckedCount(items) == 1);

    items = gp::TargetRateItems(120, 120, 144);  // a preset typed in the box: the preset is checked
    CHECK(items[1].checked);
    CHECK(items[4].label == L"Custom (120)");
    CHECK(CheckedCount(items) == 1);
}

TEST_CASE("the Target rate menu ids follow the presets") {
    CHECK(gp::kTargetRatePresets.size() == 3);
    CHECK(gp::kTargetRatePresets[0] == 120);
    CHECK(gp::kTargetRatePresets[1] == 72);
    CHECK(gp::kTargetRatePresets[2] == 60);
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

TEST_CASE("G cycles from off to 2x to 4x to Target and back") {
    const auto enabled = gp::NextGeneration(gp::GenerationFactor::Off);
    CHECK(enabled == gp::GenerationFactor::Double);
    const auto four = gp::NextGeneration(enabled);
    CHECK(four == gp::GenerationFactor::Quadruple);
    const auto target = gp::NextGeneration(four);
    CHECK(target == gp::GenerationFactor::Target);
    CHECK(gp::NextGeneration(target) == gp::GenerationFactor::Off);
}

TEST_CASE("the feedback line for Target names the rate in use") {
    CHECK(gp::GenerationLine(gp::GenerationFactor::Target, 144) == "Frame generation Target (144)");
    CHECK(gp::GenerationLine(gp::GenerationFactor::Target, 60) == "Frame generation Target (60)");
    CHECK(gp::GenerationLine(gp::GenerationFactor::Quadruple, 144) == "Frame generation 4x");
    CHECK(gp::GenerationLine(gp::GenerationFactor::Off, 144) == "Frame generation off");
}

TEST_CASE("in Target V-sync shows on and greyed out, whatever the user's own setting") {
    gp::ViewerSettings settings;
    settings.generation = gp::GenerationFactor::Target;
    auto view = gp::ViewItems(settings, false);
    CHECK(view.generation.label == L"Frame generation Target\tG");
    CHECK(view.generation.checked);
    CHECK(view.vsync.checked);
    CHECK_FALSE(view.vsync.enabled);
    CHECK_FALSE(settings.vsync);  // the user's own setting is untouched
    settings.generation = gp::GenerationFactor::Off;
    view = gp::ViewItems(settings, false);
    CHECK_FALSE(view.vsync.checked);  // restored: the user's own
    CHECK(view.vsync.enabled);
    settings.generation = gp::GenerationFactor::Quadruple;
    CHECK_FALSE(gp::ViewItems(settings, false).vsync.checked);  // 4x as before
    CHECK(gp::HotkeysText().find(L"G\tFrame generation off / 2x / 4x / Target") != std::wstring::npos);
}

TEST_CASE("About names Target and its rate") {
    gp::AboutInfo info;
    info.presentation.runningFactor = 4;
    info.presentation.targetRate = 144;
    info.presentation.shownRate = 144;
    const std::wstring text = gp::AboutText(info);
    CHECK(Contains(text, L"Frame Generation: Target (144)"));
    CHECK(Contains(text, L"Shown: 144 fps"));
}

TEST_CASE("View shows Frame generation, its key, and whether V-sync is available") {
    gp::ViewerSettings settings;
    CHECK_FALSE(gp::ViewItems(settings, false).generation.checked);
    settings.generation = gp::GenerationFactor::Double;
    auto view = gp::ViewItems(settings, false);
    CHECK(view.generation.label == L"Frame generation 2x\tG");
    settings.generation = gp::GenerationFactor::Quadruple;
    CHECK(gp::ViewItems(settings, false).generation.label == L"Frame generation 4x\tG");
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

TEST_CASE("Sharp upscale toggles once per press and the View and Hotkeys menus describe it") {
    gp::ViewerSettings s;
    CHECK_FALSE(gp::ViewItems(s, false).sharpUpscale.checked);
    CHECK(gp::ToggleSharpUpscale(s, false));
    CHECK_FALSE(gp::ToggleSharpUpscale(s, true));
    const auto item = gp::ViewItems(s, false).sharpUpscale;
    CHECK(item.checked);
    CHECK(item.label == L"Sharp upscale\tU");
    CHECK(gp::HotkeysText().find(L"U\tSharp upscale") != std::wstring::npos);
    CHECK(gp::ToggleSharpUpscale(s, false));
    CHECK_FALSE(s.sharpUpscale);
}

TEST_CASE("About reports actual generation and Sharp Upscale states") {
    gp::AboutInfo info;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: off"));
    CHECK(Contains(gp::AboutText(info), L"Sharp Upscale: off"));
    info.presentation = {4, 240, true, 1280, 720, 1920, 1080};
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: 4x"));
    CHECK(Contains(gp::AboutText(info), L"Sharp Upscale: on (1280x720 -> 1920x1080)"));
    info.presentation.runningFactor = 2;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: 2x"));
    info.presentation.runningFactor = 0;
    info.generationAvailability = gp::GenerationAvailability::MissingRuntime;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: off (needs an AMD graphics card and driver)"));
    info.generationAvailability = gp::GenerationAvailability::Failed;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: off (not available this session)"));
}

TEST_CASE("About distinguishes starting generation from off and unused upscale") {
    gp::AboutInfo info;
    info.generationChosen = true;
    info.sharpUpscaleChosen = true;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: off (starting)"));
    CHECK(Contains(gp::AboutText(info), L"Sharp Upscale: off (not in use for this picture)"));
}

TEST_CASE("About with generation labels the override as Game and all presents as Shown") {
    gp::AboutInfo info;
    info.frameRate = gp::GameRateOverride::Fps30;
    info.rates = {60,60,30,true};
    info.presentation = {4,120};
    info.presentation.realRate = 30;
    CHECK(Contains(gp::AboutText(info), L"Game: 30 fps (Frame rate set to 30)"));
    CHECK(Contains(gp::AboutText(info), L"Shown: 120 fps (all frames presented)"));
    CHECK_FALSE(Contains(gp::AboutText(info), L"Shown: 30 fps"));
}

#include "core/generation_notice.h"
TEST_CASE("generation notices own reasons while crossing to the window thread") {
    gp::GenerationNotices notices;
    std::string reason = "FRC Init AMF_RESULT 13";
    notices.Publish(7, {gp::GenerationOutcome::Failed, reason});
    reason.clear();
    const auto pending = notices.Take();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].request == 7);
    CHECK(pending[0].notice.outcome == gp::GenerationOutcome::Failed);
    CHECK(pending[0].notice.reason == "FRC Init AMF_RESULT 13");
    CHECK(notices.Take().empty());
    notices.Publish(8, {gp::GenerationOutcome::FallbackToDouble, "second FRC Init AMF_RESULT 1"});
    CHECK(notices.Take()[0].notice.outcome == gp::GenerationOutcome::FallbackToDouble);
}
TEST_CASE("About reports the actual driver failure and preserves fallback to 2x") {
    gp::AboutInfo info;
    info.generationAvailability = gp::GenerationAvailability::Failed;
    info.generationReason = L"FRC Init AMF_RESULT 13";
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: off (FRC Init AMF_RESULT 13)"));
    info.generationAvailability = gp::GenerationAvailability::Available;
    info.presentation.runningFactor = 2;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: 2x (4x unavailable: FRC Init AMF_RESULT 13)"));
    info.presentation.runningFactor = 0;
    CHECK(Contains(gp::AboutText(info), L"Frame Generation: off (4x unavailable: FRC Init AMF_RESULT 13)"));
}
