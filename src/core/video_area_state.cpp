#include "core/video_area_state.h"

#include <cwchar>
#include <string>

namespace gp {

namespace {

// HRESULTs meaning another program holds the Capture Card.
bool IsCardBusy(uint32_t code) {
    switch (code) {
    case 0xC00D3704u:  // MF_E_HW_MFT_FAILED_START_STREAMING: seen on the GC553 Pro, first read, card held elsewhere
    case 0xC00D3EA3u:  // MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED
    case 0x80070020u:  // HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)
    case 0x800700AAu:  // HRESULT_FROM_WIN32(ERROR_BUSY)
        return true;
    default:
        return false;
    }
}

}  // namespace

const char* NameOf(SourceStage stage) {
    switch (stage) {
    case SourceStage::Enumerate: return "listing devices";
    case SourceStage::Open: return "opening the device";
    case SourceStage::Reader: return "creating the reader";
    case SourceStage::Mode: return "setting the mode";
    case SourceStage::FirstRead: return "first read";
    case SourceStage::Read: return "reading";
    }
    return "?";
}

const char* NameOf(VideoAreaState state) {
    switch (state) {
    case VideoAreaState::Loading: return "Loading";
    case VideoAreaState::NoCaptureCard: return "No Capture Card";
    case VideoAreaState::CardBusy: return "Card busy";
    case VideoAreaState::NoSignal: return "No signal";
    case VideoAreaState::Playing: return "Playing";
    case VideoAreaState::Error: return "Error";
    }
    return "?";
}

VideoAreaText MessageFor(VideoAreaState state, const SourceEvent& failure) {
    switch (state) {
    case VideoAreaState::Loading:
        return {L"Loading…", L"Waiting for the picture from the capture card"};
    case VideoAreaState::NoCaptureCard:
        return {L"No capture card found", L"Plug in the capture card's USB cable"};
    case VideoAreaState::CardBusy:
        return {L"The capture card is in use by another app",
                L"For example OBS or AVerMedia software. Close it and the game will appear."};
    case VideoAreaState::NoSignal:
        return {L"No signal", L"Check the Console is switched on and its HDMI cable is plugged in"};
    case VideoAreaState::Playing:
        return {};
    case VideoAreaState::Error: {
        wchar_t detail[160];
        const std::string where = NameOf(failure.stage);
        swprintf(detail, sizeof detail / sizeof detail[0], L"Code 0x%08X while %hs. GamePlayer keeps trying.",
                 failure.code, where.c_str());
        return {L"Something went wrong with the capture card", detail};
    }
    }
    return {};
}

VideoAreaText SoundMissingNote() { return {L"", L"No sound from the capture card"}; }

VideoAreaStateMachine::VideoAreaStateMachine(int64_t nowMs) { RetryAt(nowMs); }

void VideoAreaStateMachine::FeedRestarted(int64_t) {
    // The picture and the sound were stopped, so this is no longer Playing;
    // the new Source's first frame brings Playing back and starts the sound.
    if (state_ == VideoAreaState::Playing) state_ = VideoAreaState::Loading;
    retryAtMs_.reset();
    openedAtMs_.reset();
    soundMissing_ = false;  // the sound was stopped; it starts with the first frame
    soundRetryAtMs_.reset();
}

RecoveryActions VideoAreaStateMachine::SourceReported(const SourceEvent& event, int64_t nowMs) {
    switch (event.kind) {
    case SourceEvent::Kind::Opened:
        // The state shown stays as it was until a frame arrives, so a retry
        // that opens and then fails (card busy) does not flicker.
        openedAtMs_ = nowMs;
        break;
    case SourceEvent::Kind::NoDevice:
        openedAtMs_.reset();
        lastUnexpected_.reset();
        state_ = VideoAreaState::NoCaptureCard;
        RetryAt(nowMs + kRetryMs);
        break;
    case SourceEvent::Kind::Failed:
        openedAtMs_.reset();
        if (IsCardBusy(event.code)) {
            lastUnexpected_.reset();
            state_ = VideoAreaState::CardBusy;
            failure_ = event;
            RetryAt(nowMs + kRetryMs);
        } else if (event.stage == SourceStage::Read) {
            // The stream was lost (card unplugged, PC resumed): reopen soon
            // and let that attempt say what is wrong.
            lastUnexpected_.reset();
            state_ = VideoAreaState::Loading;
            RetryAt(nowMs + kDeviceSettleMs);
        } else {
            // A one-off (say, a device half-gone while it is removed) is just
            // retried; the same failure twice in a row is shown.
            if (lastUnexpected_ == event) {
                state_ = VideoAreaState::Error;
                failure_ = event;
            }
            lastUnexpected_ = event;
            RetryAt(nowMs + kRetryMs);
        }
        break;
    }
    return {};
}

RecoveryActions VideoAreaStateMachine::FramesArrived(int64_t nowMs) {
    RecoveryActions actions;
    lastFrameMs_ = nowMs;
    openedAtMs_.reset();
    lastUnexpected_.reset();
    // A retry planned while there was no picture (No signal, a device
    // notification) must not restart a Feed that has come up by itself.
    retryAtMs_.reset();
    if (state_ != VideoAreaState::Playing) {
        state_ = VideoAreaState::Playing;
        actions.startSound = true;
    }
    return actions;
}

void VideoAreaStateMachine::SoundStarted(bool ok, int64_t nowMs) {
    soundMissing_ = !ok;
    if (ok) {
        soundRetryAtMs_.reset();
    } else {
        soundRetryAtMs_ = nowMs + kSoundRetryMs;
    }
}

void VideoAreaStateMachine::SoundLost(int64_t nowMs) {
    if (state_ != VideoAreaState::Playing) return;
    SoundStarted(false, nowMs);
}

void VideoAreaStateMachine::DevicesChanged(int64_t nowMs) {
    // A playing Feed is never interrupted (its own loss shows as a failed
    // read or a stall), nor is an open device still waiting for its first frame.
    if (state_ == VideoAreaState::Playing || openedAtMs_) return;
    RetryAt(nowMs + kDeviceSettleMs);
}

void VideoAreaStateMachine::Resumed(int64_t nowMs) {
    // A device opened before the sleep may be dead without saying so, so an
    // open one still waiting for its first frame is reopened too. A playing
    // Feed is left to the stall rule: it carries on if frames still arrive.
    if (state_ == VideoAreaState::Playing) return;
    RetryAt(nowMs + kDeviceSettleMs);
}

RecoveryActions VideoAreaStateMachine::ChoiceChanged(int64_t) {
    state_ = VideoAreaState::Loading;
    openedAtMs_.reset();
    lastUnexpected_.reset();
    retryAtMs_.reset();
    RecoveryActions actions;
    actions.restartFeed = true;
    return actions;
}

bool ChosenModeFailed(const SourceEvent& event) {
    if (event.kind != SourceEvent::Kind::Failed || IsCardBusy(event.code)) return false;
    return event.stage == SourceStage::Mode || event.stage == SourceStage::FirstRead;
}

RecoveryActions VideoAreaStateMachine::Tick(int64_t nowMs) {
    RecoveryActions actions;
    if (openedAtMs_ && nowMs - *openedAtMs_ >= kNoFramesMs) {
        // Open and asking, but the card sends nothing. A paused game still
        // sends frames, so this is never a still picture.
        openedAtMs_.reset();
        state_ = VideoAreaState::NoSignal;
        RetryAt(nowMs + kNoSignalRetryMs);
    }
    if (state_ == VideoAreaState::Playing && nowMs - lastFrameMs_ >= kStallMs) {
        state_ = VideoAreaState::Loading;
        RetryAt(nowMs);
    }
    if (retryAtMs_ && nowMs >= *retryAtMs_) {
        actions.restartFeed = true;
        retryAtMs_.reset();
    } else if (state_ == VideoAreaState::Playing && soundRetryAtMs_ && nowMs >= *soundRetryAtMs_) {
        actions.startSound = true;
        soundRetryAtMs_.reset();
    }
    return actions;
}

// Keeps the earliest pending retry.
void VideoAreaStateMachine::RetryAt(int64_t atMs) {
    if (!retryAtMs_ || atMs < *retryAtMs_) retryAtMs_ = atMs;
}

}  // namespace gp
