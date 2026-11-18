#include "doctest.h"

#include "core/settings_file.h"

using gp::GameRateOverride;
using gp::RememberedSettings;
using gp::Resolution;
using gp::SettingsFromText;
using gp::SettingsToText;
using gp::WindowRect;

namespace {

// Every value away from its default.
RememberedSettings Changed() {
    RememberedSettings s;
    s.viewer.captureCard = L"Live Gamer Ultra 2.1 é";
    s.viewer.captureCardId = L"\\\\?\\usb#vid_07ca&pid_0553&mi_00#7&1a2b3c&0&0000#{e5323777}\\global";
    s.viewer.resolution = Resolution{1280, 720};
    s.viewer.frameRate = GameRateOverride::Fps30;
    s.viewer.audio.volumePercent = 35;
    s.viewer.audio.muted = true;
    s.viewer.audio.audioDelayMs = 150;
    s.viewer.alwaysOnTop = true;
    s.viewer.generation = gp::GenerationFactor::Double;
    s.viewer.targetRate = 120;
    s.viewer.customTargetRate = 100;
    s.viewer.vsync = true;
    s.viewer.fpsCounter = true;
    s.window = gp::SavedWindow{WindowRect{-1800, 120, -1800 + 1296, 120 + 759}, true};
    return s;
}

void CheckDefaults(const RememberedSettings& s) {
    CHECK(s.viewer.captureCard.empty());
    CHECK(s.viewer.captureCardId.empty());
    CHECK_FALSE(s.viewer.resolution.has_value());
    CHECK(s.viewer.frameRate == GameRateOverride::Auto);
    CHECK(s.viewer.audio.volumePercent == 100);
    CHECK_FALSE(s.viewer.audio.muted);
    CHECK(s.viewer.audio.audioDelayMs == 0);
    CHECK_FALSE(s.viewer.alwaysOnTop);
    CHECK_FALSE(s.viewer.vsync);
    CHECK_FALSE(s.viewer.fpsCounter);
    CHECK_FALSE(s.window.has_value());
}

}  // namespace

TEST_CASE("every setting survives a save and a load") {
    const RememberedSettings saved = Changed();
    const RememberedSettings loaded = SettingsFromText(SettingsToText(saved));
    CHECK(loaded == saved);
}

TEST_CASE("the defaults survive a save and a load too") {
    CHECK(SettingsFromText(SettingsToText(RememberedSettings{})) == RememberedSettings{});
    SUBCASE("4x frame generation") {
        RememberedSettings s;
        s.viewer.generation = gp::GenerationFactor::Quadruple;
        CHECK(SettingsToText(s).find("frameGeneration=4x\n") != std::string::npos);
        CHECK(SettingsFromText(SettingsToText(s)) == s);
    }
    SUBCASE("Target frame generation") {
        RememberedSettings s;
        s.viewer.generation = gp::GenerationFactor::Target;
        CHECK(SettingsToText(s).find("frameGeneration=target\n") != std::string::npos);
        CHECK(SettingsFromText(SettingsToText(s)) == s);
    }
    SUBCASE("each Target rate") {
        for (int rate : {gp::kTargetRateMonitor, 120, 72, 60}) {
            RememberedSettings s;
            s.viewer.targetRate = rate;
            CHECK(SettingsFromText(SettingsToText(s)) == s);
        }
        RememberedSettings s;
        CHECK(SettingsToText(s).find("target_rate=monitor\n") != std::string::npos);
        s.viewer.targetRate = 72;
        CHECK(SettingsToText(s).find("target_rate=72\n") != std::string::npos);
    }
    SUBCASE("each frame rate") {
        for (GameRateOverride rate : {GameRateOverride::Auto, GameRateOverride::Fps30, GameRateOverride::Fps60}) {
            RememberedSettings s;
            s.viewer.frameRate = rate;
            CHECK(SettingsFromText(SettingsToText(s)).viewer.frameRate == rate);
        }
    }
    SUBCASE("a window that was not maximised") {
        RememberedSettings s;
        s.window = gp::SavedWindow{WindowRect{10, 20, 1306, 779}, false};
        CHECK(SettingsFromText(SettingsToText(s)) == s);
    }
}

TEST_CASE("a missing or bad Target rate means Monitor") {
    const auto load = [](const std::string& line) {
        return SettingsFromText("version=1\n" + line).viewer.targetRate;
    };
    CHECK(load("") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=monitor\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=120\n") == 120);
    CHECK(load("target_rate=fast\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=0\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=-72\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=1001\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=72.5\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=\n") == gp::kTargetRateMonitor);
    CHECK(load("target_rate=1000\n") == 1000);  // any number 1..1000 (Q10)
    CHECK(load("target_rate=1\n") == 1);
}

TEST_CASE("the Custom Target rate is kept, also while a preset is chosen") {
    RememberedSettings s;
    s.viewer.targetRate = 100;
    s.viewer.customTargetRate = 100;
    CHECK(SettingsToText(s).find("target_rate=100\n") != std::string::npos);
    CHECK(SettingsToText(s).find("target_rate_custom=100\n") != std::string::npos);
    CHECK(SettingsFromText(SettingsToText(s)) == s);

    s.viewer.targetRate = 60;  // a preset picked later: Custom (100) stays
    CHECK(SettingsFromText(SettingsToText(s)) == s);
    s.viewer.targetRate = gp::kTargetRateMonitor;
    CHECK(SettingsFromText(SettingsToText(s)) == s);

    RememberedSettings none;  // never typed
    CHECK(SettingsToText(none).find("target_rate_custom=none\n") != std::string::npos);
    CHECK(SettingsFromText(SettingsToText(none)).viewer.customTargetRate == gp::kNoCustomTargetRate);
}

TEST_CASE("a bad Custom Target rate is forgotten; a lone custom number becomes it") {
    const auto load = [](const std::string& lines) { return SettingsFromText("version=1\n" + lines).viewer; };
    CHECK(load("").customTargetRate == gp::kNoCustomTargetRate);
    CHECK(load("target_rate_custom=0\n").customTargetRate == gp::kNoCustomTargetRate);
    CHECK(load("target_rate_custom=1001\n").customTargetRate == gp::kNoCustomTargetRate);
    CHECK(load("target_rate_custom=abc\n").customTargetRate == gp::kNoCustomTargetRate);
    CHECK(load("target_rate_custom=1000\n").customTargetRate == 1000);
    CHECK(load("target_rate_custom=1\n").customTargetRate == 1);

    // A bad saved custom value in use means Monitor; the custom one is gone too.
    const gp::ViewerSettings bad = load("target_rate=2000\ntarget_rate_custom=2000\n");
    CHECK(bad.targetRate == gp::kTargetRateMonitor);
    CHECK(bad.customTargetRate == gp::kNoCustomTargetRate);

    // A number that is no preset is the custom one (a file from ticket 03, a hand edit).
    CHECK(load("target_rate=100\n").customTargetRate == 100);
    const gp::ViewerSettings odd = load("target_rate=100\ntarget_rate_custom=90\n");
    CHECK(odd.targetRate == 100);
    CHECK(odd.customTargetRate == 100);
    // A preset leaves the custom one alone.
    CHECK(load("target_rate=72\n").customTargetRate == gp::kNoCustomTargetRate);
    CHECK(load("target_rate=72\ntarget_rate_custom=90\n").customTargetRate == 90);
}

TEST_CASE("the text is readable key=value lines with a format version") {
    const std::string text = SettingsToText(Changed());
    CHECK(text.find("version=1\n") != std::string::npos);
    CHECK(text.find("volume=35\n") != std::string::npos);
    CHECK(text.find("muted=true\n") != std::string::npos);
    CHECK(text.find("audio_delay_ms=150\n") != std::string::npos);
    CHECK(text.find("resolution=1280x720\n") != std::string::npos);
    CHECK(text.find("frame_rate=30\n") != std::string::npos);
    CHECK(text.find("window=-1800,120,-504,879\n") != std::string::npos);
    CHECK(text.find("maximised=true\n") != std::string::npos);
    CHECK(text.find("capture_card=Live Gamer Ultra 2.1 \xC3\xA9\n") != std::string::npos);  // UTF-8
}

TEST_CASE("no text, or nothing usable, gives every default") {
    CheckDefaults(SettingsFromText(""));
    CheckDefaults(SettingsFromText("\n\n   \n"));
    CheckDefaults(SettingsFromText(std::string("\0\0\0\xFF\xFE garbage \x01", 16)));
    CheckDefaults(SettingsFromText("{\"volume\": 30}"));
}

TEST_CASE("a missing or unknown version gives every default") {
    std::string text = SettingsToText(Changed());
    SUBCASE("missing") {
        text.replace(text.find("version=1"), 9, "");
        CheckDefaults(SettingsFromText(text));
    }
    SUBCASE("newer") {
        text.replace(text.find("version=1"), 9, "version=2");
        CheckDefaults(SettingsFromText(text));
    }
    SUBCASE("older") {
        text.replace(text.find("version=1"), 9, "version=0");
        CheckDefaults(SettingsFromText(text));
    }
    SUBCASE("not a number") {
        text.replace(text.find("version=1"), 9, "version=one");
        CheckDefaults(SettingsFromText(text));
    }
}

TEST_CASE("a truncated file keeps what was complete and defaults the rest") {
    const std::string text = SettingsToText(Changed());
    // Cut in the middle of the capture card line: earlier values survive.
    const size_t cut = text.find("capture_card=") + 8;
    const RememberedSettings s = SettingsFromText(text.substr(0, cut));
    CHECK(s.viewer.audio.volumePercent == 35);
    CHECK(s.viewer.audio.muted);
    CHECK(s.window.has_value());
    CHECK(s.viewer.captureCard.empty());
    CHECK_FALSE(s.viewer.resolution.has_value());
    CHECK(s.viewer.frameRate == GameRateOverride::Auto);
    CHECK_FALSE(s.viewer.vsync);
    SUBCASE("every possible cut point loads without trouble") {
        for (size_t n = 0; n <= text.size(); n++) (void)SettingsFromText(text.substr(0, n));
    }
}

TEST_CASE("each invalid value falls back to its own default, the others are kept") {
    const auto with = [](const char* line) {
        return SettingsFromText(std::string("version=1\nvolume=40\n") + line + "\n");
    };
    CHECK(with("muted=perhaps").viewer.audio.muted == false);
    CHECK(with("muted=perhaps").viewer.audio.volumePercent == 40);
    CHECK(with("audio_delay_ms=soon").viewer.audio.audioDelayMs == 0);
    CHECK(with("audio_delay_ms=12.5").viewer.audio.audioDelayMs == 0);
    CHECK_FALSE(with("resolution=big").viewer.resolution.has_value());
    CHECK_FALSE(with("resolution=0x720").viewer.resolution.has_value());
    CHECK_FALSE(with("resolution=-1280x720").viewer.resolution.has_value());
    CHECK_FALSE(with("resolution=99999x99999").viewer.resolution.has_value());
    CHECK_FALSE(with("resolution=default").viewer.resolution.has_value());
    CHECK(with("frame_rate=45").viewer.frameRate == GameRateOverride::Auto);
    CHECK(with("vsync=yes please").viewer.vsync == false);
    CHECK(with("always_on_top=2").viewer.alwaysOnTop == false);
    CHECK_FALSE(with("window=1,2,3").window.has_value());
    CHECK_FALSE(with("window=100,100,50,200").window.has_value());  // right before left
    CHECK_FALSE(with("window=a,b,c,d").window.has_value());
    CHECK_FALSE(with("window=0,0,99999999,500").window.has_value());
    CHECK(SettingsFromText("version=1\nvolume=loud\n").viewer.audio.volumePercent == 100);
    CHECK(with("volume=60").viewer.audio.volumePercent == 60);  // a repeated key: the last one counts
}

TEST_CASE("out-of-range numbers are clamped to their range") {
    CHECK(SettingsFromText("version=1\nvolume=250\n").viewer.audio.volumePercent == 100);
    CHECK(SettingsFromText("version=1\nvolume=-5\n").viewer.audio.volumePercent == 0);
    CHECK(SettingsFromText("version=1\naudio_delay_ms=9000\n").viewer.audio.audioDelayMs == 500);
    CHECK(SettingsFromText("version=1\naudio_delay_ms=-100\n").viewer.audio.audioDelayMs == 0);
    CHECK(SettingsFromText("version=1\nvolume=99999999999999999999\n").viewer.audio.volumePercent == 100);
}

TEST_CASE("a capture card name that is not valid UTF-8 means the automatic choice") {
    const RememberedSettings s =
        SettingsFromText("version=1\ncapture_card=Live\xC3\ncapture_card_id=\\\\?\\usb#abc\n");
    CHECK(s.viewer.captureCard.empty());
    CHECK(s.viewer.captureCardId.empty());
}

TEST_CASE("overlong UTF-8 is not valid UTF-8: no value") {
    // '/' written in two, three and four bytes, and the C0 / C1 leads.
    for (const char* bad : {"\xC0\xAF", "\xC1\xBF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF", "Live\xC0\x80"}) {
        CAPTURE(bad);
        const RememberedSettings name =
            SettingsFromText(std::string("version=1\ncapture_card=") + bad + "\ncapture_card_id=abc\n");
        CHECK(name.viewer.captureCard.empty());
        CHECK(name.viewer.captureCardId.empty());
        const RememberedSettings id =
            SettingsFromText(std::string("version=1\ncapture_card=Live\ncapture_card_id=") + bad + "\n");
        CHECK(id.viewer.captureCard == L"Live");
        CHECK(id.viewer.captureCardId.empty());
    }
    SUBCASE("the shortest forms at each length still read") {
        const RememberedSettings s =
            SettingsFromText("version=1\ncapture_card=\xC2\x80\xE0\xA0\x80\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\n");
        CHECK(s.viewer.captureCard == std::wstring{L'\x80', L'\x800', L'\xD800', L'\xDC00', L'\xDBFF', L'\xDFFF'});
    }
}

TEST_CASE("a name with a lone surrogate (not text Windows gives) loads as the automatic choice") {
    RememberedSettings s;
    s.viewer.captureCard = std::wstring(L"Card ") + L'\xD800';
    s.viewer.captureCardId = L"abc";
    const RememberedSettings loaded = SettingsFromText(SettingsToText(s));
    CHECK(loaded.viewer.captureCard.empty());
    CHECK(loaded.viewer.captureCardId.empty());
}

TEST_CASE("the window and maximised values") {
    SUBCASE("an invalid maximised keeps the window, not maximised") {
        const RememberedSettings s = SettingsFromText("version=1\nwindow=10,20,1306,779\nmaximised=perhaps\n");
        REQUIRE(s.window.has_value());
        CHECK(s.window->normal == WindowRect{10, 20, 1306, 779});
        CHECK_FALSE(s.window->maximised);
    }
    SUBCASE("maximised without a window is the default window") {
        CHECK_FALSE(SettingsFromText("version=1\nmaximised=true\n").window.has_value());
    }
    SUBCASE("coordinates up to 100000 either way, not beyond") {
        CHECK(SettingsFromText("version=1\nwindow=-100000,-100000,100000,100000\n").window.has_value());
        CHECK_FALSE(SettingsFromText("version=1\nwindow=-100001,0,500,500\n").window.has_value());
        CHECK_FALSE(SettingsFromText("version=1\nwindow=0,0,500,100001\n").window.has_value());
    }
}

TEST_CASE("an identifier without a name is dropped: no card was chosen") {
    CHECK(SettingsFromText("version=1\ncapture_card=\ncapture_card_id=\\\\?\\usb#abc\n").viewer.captureCardId.empty());
}

TEST_CASE("unknown keys, comments, spaces, Windows line ends and a BOM are all fine") {
    const RememberedSettings s = SettingsFromText(
        "\xEF\xBB\xBF# edited by hand\r\n"
        "version = 1\r\n"
        "; another comment\r\n"
        "lossless_scaling=on\r\n"
        "no equals sign here\r\n"
        "  volume =  55  \r\n"
        "vsync=true\r\n");
    CHECK(s.viewer.audio.volumePercent == 55);
    CHECK(s.viewer.vsync);
    CHECK_FALSE(s.viewer.alwaysOnTop);
}

TEST_CASE("fullscreen is not part of the file") {
    const std::string text = SettingsToText(Changed());
    CHECK(text.find("fullscreen") == std::string::npos);
}

TEST_CASE("the FPS counter is remembered, off unless the file says on") {
    CHECK(SettingsToText(Changed()).find("fps_counter=true\n") != std::string::npos);
    CHECK(SettingsFromText("version=1\nfps_counter=true\n").viewer.fpsCounter);
    CHECK_FALSE(SettingsFromText("version=1\nfps_counter=sometimes\n").viewer.fpsCounter);
    CHECK_FALSE(SettingsFromText("version=1\n").viewer.fpsCounter);
}

TEST_CASE("the file is written once the settings have stayed the same for a second") {
    gp::SettingsSaveTiming timing("old");
    CHECK_FALSE(timing.Second("a"));  // just changed
    CHECK(timing.Second("a"));        // a second later, unchanged
    timing.Written("a");
    CHECK_FALSE(timing.Second("a"));  // already on disk

    SUBCASE("a drag that keeps changing is written only when it stops") {
        CHECK_FALSE(timing.Second("b1"));
        CHECK_FALSE(timing.Second("b2"));
        CHECK_FALSE(timing.Second("b3"));
        CHECK(timing.Second("b3"));
    }
    SUBCASE("a failed write is tried again only after a while, not every second") {
        CHECK_FALSE(timing.Second("c"));
        CHECK(timing.Second("c"));
        timing.Failed("c");
        for (int s = 1; s < gp::SettingsSaveTiming::kRetrySeconds; s++) CHECK_FALSE(timing.Second("c"));
        CHECK(timing.Second("c"));  // tried again
        timing.Failed("c");
        CHECK_FALSE(timing.Second("c"));
        CHECK(timing.Differs("c"));  // so exit still tries
    }
    SUBCASE("after a failed write, a new change is tried once it settles") {
        CHECK_FALSE(timing.Second("c"));
        CHECK(timing.Second("c"));
        timing.Failed("c");
        CHECK_FALSE(timing.Second("e"));
        CHECK(timing.Second("e"));
    }
    SUBCASE("changing back to what is on disk writes nothing") {
        CHECK_FALSE(timing.Second("d"));
        CHECK_FALSE(timing.Second("a"));
        CHECK_FALSE(timing.Second("a"));
    }
}

TEST_CASE("a damaged or missing file is replaced soon after launch") {
    gp::SettingsSaveTiming timing("garbage");
    const std::string now = SettingsToText(SettingsFromText("garbage"));
    CHECK_FALSE(timing.Second(now));
    CHECK(timing.Second(now));
}

TEST_CASE("at exit the file is written only if it differs") {
    gp::SettingsSaveTiming timing("same");
    CHECK_FALSE(timing.Differs("same"));
    CHECK(timing.Differs("other"));
}

TEST_CASE("remembered Frame generation defaults to off on missing or damaged values") {
    CHECK(SettingsFromText("version=1\nframeGeneration=bogus\n").viewer.generation == gp::GenerationFactor::Off);
    CHECK(SettingsFromText("").viewer.generation == gp::GenerationFactor::Off);
    CHECK(SettingsFromText("version=1\nframeGeneration=target\n").viewer.generation == gp::GenerationFactor::Target);
    CHECK(SettingsFromText("version=1\nframeGeneration=Target 144\n").viewer.generation == gp::GenerationFactor::Off);
}

TEST_CASE("Sharp upscale starts off and only valid choices survive a restart") {
    gp::RememberedSettings settings;
    CHECK_FALSE(settings.viewer.sharpUpscale);
    settings.viewer.sharpUpscale = true;
    CHECK(SettingsFromText(SettingsToText(settings)).viewer.sharpUpscale);
    CHECK_FALSE(SettingsFromText("version=1\nsharpUpscale=damaged\n").viewer.sharpUpscale);
}
