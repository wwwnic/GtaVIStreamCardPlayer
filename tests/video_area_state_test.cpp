#include "doctest.h"

#include <string>

#include "core/video_area_state.h"

using gp::SourceEvent;
using gp::SourceStage;
using gp::VideoAreaState;
using gp::VideoAreaStateMachine;
using Kind = gp::SourceEvent::Kind;
using M = gp::VideoAreaStateMachine;

namespace {

const SourceEvent kOpened{Kind::Opened, SourceStage::FirstRead, 0};
const SourceEvent kNoDevice{Kind::NoDevice, SourceStage::Enumerate, 0};
// Seen on the GC553 Pro when another process holds it: the device opens and
// the mode is set, then the first read fails.
const SourceEvent kBusy{Kind::Failed, SourceStage::FirstRead, 0xC00D3704u};
const SourceEvent kOdd{Kind::Failed, SourceStage::Reader, 0x80004005u};
const SourceEvent kStreamLost{Kind::Failed, SourceStage::Read, 0xC00D3EA2u};

// Runs the restart the machine asked for, then reports `outcome`.
void Restart(M& machine, int64_t now, const SourceEvent& outcome) {
    machine.FeedRestarted(now);
    machine.SourceReported(outcome, now);
}

// A machine showing the Feed at time `now`.
M PlayingAt(int64_t now) {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOpened);
    machine.FramesArrived(now);
    machine.SoundStarted(true, now);
    return machine;
}

}  // namespace

TEST_CASE("on launch the Video Area is Loading and the first tick starts the Feed") {
    M machine(0);
    CHECK(machine.State() == VideoAreaState::Loading);
    CHECK(machine.Tick(0).restartFeed);
}

TEST_CASE("the first frame brings Playing and starts the sound with it") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOpened);
    CHECK(machine.State() == VideoAreaState::Loading);  // open, no picture yet
    CHECK_FALSE(machine.Tick(100).restartFeed);
    const gp::RecoveryActions actions = machine.FramesArrived(200);
    CHECK(machine.State() == VideoAreaState::Playing);
    CHECK(actions.startSound);
    CHECK_FALSE(actions.restartFeed);
    // Later frames ask for nothing more.
    CHECK_FALSE(machine.FramesArrived(300).startSound);
}

TEST_CASE("no capture card: says so and retries about once a second, without spinning") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kNoDevice);
    CHECK(machine.State() == VideoAreaState::NoCaptureCard);
    CHECK_FALSE(machine.Tick(1).restartFeed);
    CHECK_FALSE(machine.Tick(M::kRetryMs - 1).restartFeed);
    CHECK(machine.Tick(M::kRetryMs).restartFeed);
    // Asked once; nothing more until the restart has been done.
    CHECK_FALSE(machine.Tick(M::kRetryMs + 10).restartFeed);
}

TEST_CASE("another program holding the card shows Card busy, retries, and plays once it lets go") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOpened);
    machine.SourceReported(kBusy, 50);  // the first read fails just after opening
    CHECK(machine.State() == VideoAreaState::CardBusy);
    CHECK(machine.Failure() == kBusy);
    CHECK_FALSE(machine.Tick(50 + M::kRetryMs - 1).restartFeed);
    CHECK(machine.Tick(50 + M::kRetryMs).restartFeed);
    Restart(machine, 1050, kOpened);
    machine.SourceReported(kBusy, 1100);
    CHECK(machine.State() == VideoAreaState::CardBusy);
    machine.Tick(2100);
    Restart(machine, 2100, kOpened);  // OBS closed
    machine.FramesArrived(2150);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("a device another program took over while playing also counts as busy") {
    M machine = PlayingAt(100);
    machine.SourceReported({Kind::Failed, SourceStage::Read, 0xC00D3EA3u}, 200);  // MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED
    CHECK(machine.State() == VideoAreaState::CardBusy);
}

TEST_CASE("an unexpected failure shows Error with its code once it happens twice in a row") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOdd);
    CHECK(machine.State() == VideoAreaState::Loading);  // one-off: just try again
    CHECK(machine.Tick(M::kRetryMs).restartFeed);
    Restart(machine, 1000, kOdd);
    CHECK(machine.State() == VideoAreaState::Error);
    CHECK(machine.Failure() == kOdd);
    CHECK(machine.Tick(1000 + M::kRetryMs).restartFeed);  // and keeps retrying
    Restart(machine, 2000, kOpened);
    machine.FramesArrived(2050);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("the Error rule forgets a failure once something else happened") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOdd);
    machine.Tick(1000);
    Restart(machine, 1000, kNoDevice);
    machine.Tick(2000);
    Restart(machine, 2000, kOdd);
    CHECK(machine.State() == VideoAreaState::NoCaptureCard);
}

TEST_CASE("a stream lost while playing reconnects quickly, showing Loading meanwhile") {
    M machine = PlayingAt(100);
    machine.SourceReported(kStreamLost, 500);
    CHECK(machine.State() == VideoAreaState::Loading);
    CHECK_FALSE(machine.Tick(500 + M::kDeviceSettleMs - 1).restartFeed);
    CHECK(machine.Tick(500 + M::kDeviceSettleMs).restartFeed);
}

TEST_CASE("unplugging the card while playing ends in No Capture Card, and replugging in Playing") {
    M machine = PlayingAt(100);
    machine.SourceReported(kStreamLost, 500);
    machine.Tick(1000);
    Restart(machine, 1000, kNoDevice);
    CHECK(machine.State() == VideoAreaState::NoCaptureCard);
    machine.Tick(2000);
    Restart(machine, 2000, kOpened);
    CHECK(machine.FramesArrived(2100).startSound);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("the card open but sending no frames means No signal, and it keeps reopening slowly") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOpened);
    CHECK(machine.State() == VideoAreaState::Loading);
    CHECK_FALSE(machine.Tick(M::kNoFramesMs - 1).restartFeed);
    CHECK(machine.State() == VideoAreaState::Loading);
    CHECK_FALSE(machine.Tick(M::kNoFramesMs).restartFeed);
    CHECK(machine.State() == VideoAreaState::NoSignal);
    const int64_t noSignalAt = M::kNoFramesMs;
    CHECK_FALSE(machine.Tick(noSignalAt + M::kNoSignalRetryMs - 1).restartFeed);
    CHECK(machine.Tick(noSignalAt + M::kNoSignalRetryMs).restartFeed);
    Restart(machine, 8000, kOpened);
    CHECK(machine.State() == VideoAreaState::NoSignal);  // still no frames: no flicker
}

TEST_CASE("frames arriving again end No signal at once, without waiting for a reopen") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOpened);
    machine.Tick(M::kNoFramesMs);
    REQUIRE(machine.State() == VideoAreaState::NoSignal);
    CHECK(machine.FramesArrived(4000).startSound);  // Console switched on
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("a static picture or a Game Rate near zero is never No signal") {
    // The Source keeps delivering frames of a paused game (they are Repeated
    // Frames, so nothing new is presented). Only frames reaching the Source count.
    M machine = PlayingAt(100);
    for (int64_t t = 200; t <= 600000; t += 250) {
        machine.FramesArrived(t);
        const gp::RecoveryActions actions = machine.Tick(t);
        CHECK_FALSE(actions.restartFeed);
    }
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("frames stopping while playing is a stalled device: reconnect, showing Loading") {
    M machine = PlayingAt(100);
    CHECK_FALSE(machine.Tick(100 + M::kStallMs - 1).restartFeed);
    CHECK(machine.State() == VideoAreaState::Playing);
    CHECK(machine.Tick(100 + M::kStallMs).restartFeed);
    CHECK(machine.State() == VideoAreaState::Loading);
}

TEST_CASE("a device notification retries sooner than the next regular retry") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kNoDevice);
    machine.DevicesChanged(100);
    CHECK_FALSE(machine.Tick(100 + M::kDeviceSettleMs - 1).restartFeed);
    CHECK(machine.Tick(100 + M::kDeviceSettleMs).restartFeed);
}

TEST_CASE("a device notification never interrupts a playing Feed") {
    M machine = PlayingAt(100);
    machine.DevicesChanged(200);  // a mouse plugged in
    for (int64_t t = 250; t < 5000; t += 250) {
        machine.FramesArrived(t);
        CHECK_FALSE(machine.Tick(t).restartFeed);
    }
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("after PC sleep and resume the Feed returns by itself") {
    M machine = PlayingAt(100);
    const int64_t resumed = 100 + 3600 * 1000;  // no frames, no ticks while asleep
    machine.DevicesChanged(resumed);
    CHECK(machine.Tick(resumed).restartFeed);
    CHECK(machine.State() == VideoAreaState::Loading);
    Restart(machine, resumed, kOpened);
    CHECK(machine.FramesArrived(resumed + 300).startSound);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("sound that cannot start keeps the picture, shows a note and is retried") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOpened);
    machine.FramesArrived(100);
    machine.SoundStarted(false, 100);
    CHECK(machine.State() == VideoAreaState::Playing);
    CHECK(machine.SoundMissing());
    machine.FramesArrived(100 + M::kSoundRetryMs - 1);
    CHECK_FALSE(machine.Tick(100 + M::kSoundRetryMs - 1).startSound);
    machine.FramesArrived(100 + M::kSoundRetryMs);
    const gp::RecoveryActions actions = machine.Tick(100 + M::kSoundRetryMs);
    CHECK(actions.startSound);
    CHECK_FALSE(actions.restartFeed);
    machine.SoundStarted(true, 5200);
    CHECK_FALSE(machine.SoundMissing());
}

TEST_CASE("sound lost while playing is retried on its own; the picture carries on") {
    M machine = PlayingAt(100);
    CHECK_FALSE(machine.SoundMissing());
    machine.SoundLost(200);
    CHECK(machine.SoundMissing());
    machine.FramesArrived(200 + M::kSoundRetryMs);
    CHECK(machine.Tick(200 + M::kSoundRetryMs).startSound);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("the sound note belongs to Playing only; a reconnect starts picture and sound afresh together") {
    M machine = PlayingAt(100);
    machine.SoundLost(200);
    machine.SourceReported(kStreamLost, 300);
    CHECK_FALSE(machine.SoundMissing());  // not Playing
    CHECK_FALSE(machine.Tick(300 + M::kSoundRetryMs).startSound);
    machine.FeedRestarted(1000);
    machine.SourceReported(kOpened, 1000);
    CHECK(machine.FramesArrived(1100).startSound);
    machine.SoundStarted(true, 1100);
    CHECK_FALSE(machine.SoundMissing());
}

TEST_CASE("every state has a name for the log") {
    CHECK(std::string(gp::NameOf(VideoAreaState::Loading)) == "Loading");
    CHECK(std::string(gp::NameOf(VideoAreaState::NoCaptureCard)) == "No Capture Card");
    CHECK(std::string(gp::NameOf(VideoAreaState::CardBusy)) == "Card busy");
    CHECK(std::string(gp::NameOf(VideoAreaState::NoSignal)) == "No signal");
    CHECK(std::string(gp::NameOf(VideoAreaState::Playing)) == "Playing");
    CHECK(std::string(gp::NameOf(VideoAreaState::Error)) == "Error");
}

TEST_CASE("plugging the card in brings the Feed up without a restart of the app") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kNoDevice);
    machine.Tick(1000);
    Restart(machine, 1000, kNoDevice);
    machine.Tick(2000);
    Restart(machine, 2000, kOpened);
    CHECK(machine.State() == VideoAreaState::NoCaptureCard);  // no flicker while it opens
    machine.FramesArrived(2100);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("choosing another device or resolution while playing restarts at once through Loading") {
    M machine = PlayingAt(100);
    const gp::RecoveryActions actions = machine.ChoiceChanged(500);
    CHECK(actions.restartFeed);
    CHECK(machine.State() == VideoAreaState::Loading);
    Restart(machine, 500, kOpened);
    CHECK(machine.FramesArrived(600).startSound);
    CHECK(machine.State() == VideoAreaState::Playing);
}

TEST_CASE("a choice made while an error shows restarts at once and leaves the error behind") {
    M machine(0);
    machine.Tick(0);
    Restart(machine, 0, kOdd);
    machine.Tick(1000);
    Restart(machine, 1000, kOdd);
    REQUIRE(machine.State() == VideoAreaState::Error);
    CHECK(machine.ChoiceChanged(1200).restartFeed);
    CHECK(machine.State() == VideoAreaState::Loading);
    machine.FeedRestarted(1200);
    CHECK_FALSE(machine.Tick(1300).restartFeed);  // no stale retry behind it
}

TEST_CASE("a chosen mode that fails to set or to start reading counts as the mode's failure") {
    CHECK(gp::ChosenModeFailed({Kind::Failed, SourceStage::Mode, 0xC00D36B4u}));
    CHECK(gp::ChosenModeFailed({Kind::Failed, SourceStage::FirstRead, 0x80004005u}));
}

TEST_CASE("a busy card, a missing card or a lost stream is not the chosen mode's fault") {
    CHECK_FALSE(gp::ChosenModeFailed(kBusy));
    CHECK_FALSE(gp::ChosenModeFailed(kNoDevice));
    CHECK_FALSE(gp::ChosenModeFailed(kStreamLost));
    CHECK_FALSE(gp::ChosenModeFailed(kOdd));  // before the mode is set
    CHECK_FALSE(gp::ChosenModeFailed(kOpened));
}
