#include "platform/feed_keeper.h"

#include <dbt.h>

#include "platform/log.h"
#include "platform/mf_source.h"

namespace gp {

namespace {

const UINT_PTR kTickTimer = 0x6F06;  // away from main's small timer ids
const UINT kTickMs = 250;
const uint32_t kFirstFrame = 3;      // posted "kind" beside SourceEvent::Kind's values

UINT FeedEventMessage() {
    static const UINT message = RegisterWindowMessageW(L"GamePlayer.FeedEvent");
    return message;
}

// wParam: generation << 8 | kind << 4 | stage. lParam: the code.
WPARAM Pack(uint32_t generation, uint32_t kind, SourceStage stage) {
    return (static_cast<WPARAM>(generation) << 8) | (kind << 4) | static_cast<uint32_t>(stage);
}

int64_t TickNow() { return static_cast<int64_t>(GetTickCount64()); }

}  // namespace

FeedKeeper::FeedKeeper(HWND window, Presenter& presenter, Sound& sound)
    : window_(window),
      presenter_(presenter),
      processor_(MakePassThroughProcessor()),
      source_(MakeMediaFoundationSource()),
      sound_(sound),
      machine_(TickNow()) {}

FeedKeeper::~FeedKeeper() { Stop(); }

int64_t FeedKeeper::Now() const { return TickNow(); }

void FeedKeeper::Start() {
    if (running_) return;
    running_ = true;
    // Every device interface arriving or leaving, so a replugged card is
    // noticed at once instead of at the next retry.
    DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
    filter.dbcc_size = sizeof filter;
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    deviceNotification_ = RegisterDeviceNotificationW(window_, &filter,
                                                      DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    if (!deviceNotification_) Log("Feed: no device notifications (error %lu); retries only", GetLastError());
    Log("Video Area: %s", NameOf(machine_.State()));
    presenter_.ShowPicture(false);
    presenter_.ShowText(TextPlace::Centre, MessageFor(machine_.State(), {}));
    SetTimer(window_, kTickTimer, kTickMs, nullptr);
    Tick();
}

void FeedKeeper::Stop() {
    if (!running_) return;
    running_ = false;
    KillTimer(window_, kTickTimer);
    if (deviceNotification_) UnregisterDeviceNotification(deviceNotification_);
    deviceNotification_ = nullptr;
    sound_.Stop();
    soundRunning_ = false;
    // The Source stops calling into the Processor and Presenter before either goes away.
    source_->Stop();
    generation_++;
}

bool FeedKeeper::TakePlayingSecond() {
    const bool frames = frameSinceAwakeCheck_.exchange(false);
    return frames && machine_.State() == VideoAreaState::Playing;
}

bool FeedKeeper::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result) {
    *result = 0;
    if (!running_) return false;
    if (message == WM_TIMER && wParam == kTickTimer) {
        Tick();
        return true;
    }
    if (message == FeedEventMessage()) {
        if ((wParam >> 8) != generation_) return true;  // from a Source since stopped
        const uint32_t kind = (wParam >> 4) & 15;
        if (kind == kFirstFrame) {
            frameSinceTick_ = false;
            Apply(machine_.FramesArrived(Now()));
        } else {
            SourceEvent event;
            event.kind = static_cast<SourceEvent::Kind>(kind);
            event.stage = static_cast<SourceStage>(wParam & 15);
            event.code = static_cast<uint32_t>(lParam);
            Apply(machine_.SourceReported(event, Now()));
        }
        ShowState();
        return true;
    }
    if (message == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE || wParam == DBT_DEVNODES_CHANGED) {
            machine_.DevicesChanged(Now());
        }
        return false;  // let DefWindowProc answer too
    }
    if (message == WM_POWERBROADCAST) {
        if (wParam == PBT_APMSUSPEND) Log("Feed: PC going to sleep");
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            Log("Feed: PC resumed from sleep");
            machine_.DevicesChanged(Now());
        }
        *result = TRUE;
        return true;
    }
    return false;
}

void FeedKeeper::Tick() {
    const int64_t now = Now();
    if (frameSinceTick_.exchange(false)) Apply(machine_.FramesArrived(now));
    if (soundRunning_ && !sound_.Capturing()) {
        soundRunning_ = false;
        machine_.SoundLost(now);
    }
    Apply(machine_.Tick(now));
    ShowState();
    if (++ticks_ % (1000 / kTickMs) == 0) LogSignalProbe();
}

void FeedKeeper::Apply(RecoveryActions actions) {
    if (actions.restartFeed) RestartFeed();
    if (actions.startSound) StartSound();
}

void FeedKeeper::RestartFeed() {
    const int64_t began = Now();
    sound_.Stop();
    soundRunning_ = false;
    source_->Stop();
    const uint32_t generation = ++generation_;
    machine_.FeedRestarted(began);
    firstFrameWanted_ = true;
    HWND window = window_;
    source_->Start(
        [this, window, generation](FramePtr captured) {
            framesArrived_++;
            frameSinceTick_ = true;
            frameSinceAwakeCheck_ = true;
            if (firstFrameWanted_.exchange(false)) {
                PostMessageW(window, FeedEventMessage(), Pack(generation, kFirstFrame, SourceStage::Read), 0);
            }
            toShow_.clear();
            processor_->Process(std::move(captured), toShow_);
            for (TimedFrame& frame : toShow_) presenter_.Submit(std::move(frame));
        },
        [window, generation](const SourceEvent& event) {
            PostMessageW(window, FeedEventMessage(), Pack(generation, static_cast<uint32_t>(event.kind), event.stage),
                         static_cast<LPARAM>(event.code));
        });
    lastRestartMs_ = Now() - began;
}

void FeedKeeper::StartSound() {
    sound_.Stop();
    soundRunning_ = sound_.Start(source_->DeviceName());
    if (!soundRunning_) Log("Feed: no sound; the picture carries on");
    machine_.SoundStarted(soundRunning_, Now());
}

void FeedKeeper::ShowState() {
    const VideoAreaState state = machine_.State();
    if (state != shown_) {
        const SourceEvent failure = machine_.Failure();
        if (state == VideoAreaState::Error || state == VideoAreaState::CardBusy) {
            Log("Video Area: %s -> %s (hr=0x%08X, %s)", NameOf(shown_), NameOf(state), failure.code,
                NameOf(failure.stage));
        } else {
            Log("Video Area: %s -> %s", NameOf(shown_), NameOf(state));
        }
        shown_ = state;
        presenter_.ShowText(TextPlace::Centre, MessageFor(state, failure));
        presenter_.ShowPicture(state == VideoAreaState::Playing);
    }
    const bool note = machine_.SoundMissing();
    if (note != shownSoundNote_) {
        shownSoundNote_ = note;
        presenter_.ShowText(TextPlace::Bottom, note ? SoundMissingNote() : VideoAreaText{});
    }
}

// Every value no-signal detection could use, once a second, so one run with
// the HDMI cable pulled tells what the card really does.
void FeedKeeper::LogSignalProbe() {
    const Source::Probe video = source_->TakeProbe();
    const Sound::Probe sound = sound_.TakeProbe();
    Log("Signal: state %s, video frames %u, stream ticks %u, type changes %u, discontinuities %u, luma min %d mean %d "
        "max %d; sound frames %u, silent %u, peak %.4f; last restart %lld ms",
        NameOf(machine_.State()), video.frames, video.streamTicks, video.typeChanges, video.discontinuities,
        video.lumaMin, video.lumaMean, video.lumaMax, sound.frames, sound.silentFrames, sound.peak,
        static_cast<long long>(lastRestartMs_));
}

}  // namespace gp
