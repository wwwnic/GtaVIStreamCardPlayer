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
    s.viewer.vsync = true;
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

TEST_CASE("fullscreen and the FPS counter are not part of the file") {
    const std::string text = SettingsToText(Changed());
    CHECK(text.find("fullscreen") == std::string::npos);
    CHECK(text.find("fps") == std::string::npos);
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
    SUBCASE("a failed write is tried again the next second") {
        CHECK_FALSE(timing.Second("c"));
        CHECK(timing.Second("c"));
        CHECK(timing.Second("c"));
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
