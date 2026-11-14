// The Video Area states and automatic recovery (ticket 06): events from the
// Source and the clock in, the state to show and what to restart out.
// Pure logic: the caller passes the time (milliseconds, any steady clock).
#pragma once

#include <cstdint>
#include <optional>

#include "core/source.h"
#include "core/video_area_text.h"

namespace gp {

enum class VideoAreaState {
    Loading,        // started or reconnecting; no picture yet
    NoCaptureCard,  // no video capture device present
    CardBusy,       // another program holds the card
    NoSignal,       // the card is open but sends no frames (Console off or unplugged)
    Playing,        // the Feed is showing
    Error,          // anything unexpected; see FailureCode()
};

const char* NameOf(VideoAreaState state);

// The message the Video Area shows in this state (empty while Playing).
// `failure` is VideoAreaStateMachine::Failure().
VideoAreaText MessageFor(VideoAreaState state, const SourceEvent& failure);
// The small note shown while Playing when the sound could not start.
VideoAreaText SoundMissingNote();

// What the caller must do after an event or a tick.
struct RecoveryActions {
    // Stop the sound and the Source, then start the Source again from scratch
    // (device choice included) and call FeedRestarted.
    bool restartFeed = false;
    // Start the sound (the Feed has just come up, or the sound is retried);
    // report the outcome with SoundStarted.
    bool startSound = false;
};

class VideoAreaStateMachine {
public:
    static constexpr int64_t kRetryMs = 1000;          // non-playing states retry this often
    static constexpr int64_t kDeviceSettleMs = 500;    // after a device notification or a lost stream
    static constexpr int64_t kNoFramesMs = 3000;       // open, no frame for this long: no signal
    static constexpr int64_t kNoSignalRetryMs = 5000;  // then reopen this often
    static constexpr int64_t kStallMs = 2000;          // playing, no frame arrived for this long: reconnect
    static constexpr int64_t kSoundRetryMs = 5000;     // sound missing while playing: retry this often

    // Starts in Loading; the first Tick asks for the Feed to start.
    explicit VideoAreaStateMachine(int64_t nowMs);

    VideoAreaState State() const { return state_; }
    // Error: the failure shown. Also kept for CardBusy.
    SourceEvent Failure() const { return failure_; }
    // Playing, but the sound could not start: show a small note.
    bool SoundMissing() const { return state_ == VideoAreaState::Playing && soundMissing_; }

    // Events. Each may ask for actions.
    void FeedRestarted(int64_t nowMs);                       // the caller did restartFeed
    RecoveryActions SourceReported(const SourceEvent& event, int64_t nowMs);
    RecoveryActions FramesArrived(int64_t nowMs);            // at least one frame reached the Source
    void SoundStarted(bool ok, int64_t nowMs);
    void SoundLost(int64_t nowMs);                           // sound stopped by itself while playing
    void DevicesChanged(int64_t nowMs);                      // device arrival/removal, resume from sleep

    // Call regularly (a few times a second).
    RecoveryActions Tick(int64_t nowMs);

private:
    void RetryAt(int64_t atMs);

    VideoAreaState state_ = VideoAreaState::Loading;
    SourceEvent failure_;
    std::optional<SourceEvent> lastUnexpected_;  // shown as Error only when it repeats
    std::optional<int64_t> retryAtMs_;
    std::optional<int64_t> openedAtMs_;   // the Source is open and no frame has come yet
    int64_t lastFrameMs_ = 0;
    bool soundMissing_ = false;
    std::optional<int64_t> soundRetryAtMs_;
};

}  // namespace gp
