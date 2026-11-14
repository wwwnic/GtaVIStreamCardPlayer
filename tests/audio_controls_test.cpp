#include "doctest.h"

#include "core/audio_controls.h"

using gp::AudioKey;
using gp::AudioSettings;
using gp::PressAudioKey;

TEST_CASE("the default is full volume, not muted, no Audio Delay") {
    const AudioSettings settings;
    CHECK(settings.volumePercent == 100);
    CHECK_FALSE(settings.muted);
    CHECK(settings.audioDelayMs == 0);
    CHECK(gp::GainOf(settings) == doctest::Approx(1.0));
}

TEST_CASE("Up and Down step the volume by 5 points and say the new level") {
    AudioSettings settings;
    gp::AudioKeyResult result = PressAudioKey(settings, AudioKey::VolumeDown);
    CHECK(result.settings.volumePercent == 95);
    CHECK(result.feedback == "Volume 95%");
    result = PressAudioKey(result.settings, AudioKey::VolumeDown);
    CHECK(result.settings.volumePercent == 90);
    result = PressAudioKey(result.settings, AudioKey::VolumeUp);
    CHECK(result.settings.volumePercent == 95);
    CHECK(gp::GainOf(result.settings) == doctest::Approx(0.95));
}

TEST_CASE("the volume stays between 0% and 100%") {
    AudioSettings settings;
    CHECK(PressAudioKey(settings, AudioKey::VolumeUp).settings.volumePercent == 100);
    settings.volumePercent = 5;
    settings = PressAudioKey(settings, AudioKey::VolumeDown).settings;
    CHECK(settings.volumePercent == 0);
    CHECK(gp::GainOf(settings) == doctest::Approx(0.0));
    const gp::AudioKeyResult result = PressAudioKey(settings, AudioKey::VolumeDown);
    CHECK(result.settings.volumePercent == 0);
    CHECK(result.feedback == "Volume 0%");
    CHECK(gp::ClampVolume(130) == 100);
    CHECK(gp::ClampVolume(-5) == 0);
    CHECK(gp::ClampVolume(42) == 42);
}

TEST_CASE("M mutes and unmutes, and unmuting returns to the volume before") {
    AudioSettings settings;
    settings.volumePercent = 80;
    gp::AudioKeyResult result = PressAudioKey(settings, AudioKey::ToggleMute);
    CHECK(result.settings.muted);
    CHECK(result.settings.volumePercent == 80);
    CHECK(result.feedback == "Muted");
    CHECK(gp::GainOf(result.settings) == doctest::Approx(0.0));
    result = PressAudioKey(result.settings, AudioKey::ToggleMute);
    CHECK_FALSE(result.settings.muted);
    CHECK(result.feedback == "Unmuted, volume 80%");
    CHECK(gp::GainOf(result.settings) == doctest::Approx(0.8));
}

TEST_CASE("K plays the sound 50 ms later each press, J 50 ms earlier") {
    AudioSettings settings;
    gp::AudioKeyResult result = PressAudioKey(settings, AudioKey::DelayLater);
    CHECK(result.settings.audioDelayMs == 50);
    CHECK(result.feedback == "Audio delay +50 ms");
    result = PressAudioKey(result.settings, AudioKey::DelayLater);
    CHECK(result.settings.audioDelayMs == 100);
    CHECK(result.feedback == "Audio delay +100 ms");
    result = PressAudioKey(result.settings, AudioKey::DelayEarlier);
    CHECK(result.settings.audioDelayMs == 50);
    CHECK(result.feedback == "Audio delay +50 ms");
}

TEST_CASE("the Audio Delay stays between 0 and 500 ms and the line says when it is at a limit") {
    AudioSettings settings;
    gp::AudioKeyResult result = PressAudioKey(settings, AudioKey::DelayEarlier);
    CHECK(result.settings.audioDelayMs == 0);
    CHECK(result.feedback == "Audio delay 0 ms (minimum)");
    settings.audioDelayMs = 450;
    result = PressAudioKey(settings, AudioKey::DelayLater);
    CHECK(result.settings.audioDelayMs == 500);
    CHECK(result.feedback == "Audio delay +500 ms (maximum)");
    result = PressAudioKey(result.settings, AudioKey::DelayLater);
    CHECK(result.settings.audioDelayMs == 500);
    CHECK(gp::ClampAudioDelay(-50) == 0);
    CHECK(gp::ClampAudioDelay(900) == 500);
    CHECK(gp::ClampAudioDelay(150) == 150);
}

TEST_CASE("changing the volume while muted unmutes, from the kept level") {
    AudioSettings settings;
    settings.volumePercent = 60;
    settings.muted = true;
    const gp::AudioKeyResult result = PressAudioKey(settings, AudioKey::VolumeUp);
    CHECK_FALSE(result.settings.muted);
    CHECK(result.settings.volumePercent == 65);
    CHECK(result.feedback == "Volume 65%");
}
