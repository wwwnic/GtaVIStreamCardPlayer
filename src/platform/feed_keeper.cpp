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

int64_t QpcNow() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

int64_t QpcFrequency() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

double QpcToMs(int64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(QpcFrequency()); }

void RaiseTo(std::atomic<int64_t>& worst, int64_t value) {
    int64_t seen = worst.load();
    while (value > seen && !worst.compare_exchange_weak(seen, value)) {}
}

}  // namespace

FeedKeeper::FeedKeeper(HWND window, Presenter& presenter, Sound& sound, double tolerance)
    : window_(window),
      presenter_(presenter),
      processor_(std::make_unique<UniqueFrameProcessor>(QpcFrequency(), tolerance)),
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
    // The first tick opens the Capture Card, which blocks this thread. It is
    // posted, not run here, so the caller shows the window first and Loading
    // is on screen while the card opens.
    PostMessageW(window_, WM_TIMER, kTickTimer, 0);
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
            choiceProven_ = true;
            Apply(machine_.FramesArrived(Now()));
        } else {
            SourceEvent event;
            event.kind = static_cast<SourceEvent::Kind>(kind);
            event.stage = static_cast<SourceStage>(wParam & 15);
            event.code = static_cast<uint32_t>(lParam);
            // A failure ends this Source's frames. One that arrived just
            // before it must not count as a frame after it at the next tick.
            if (event.kind == SourceEvent::Kind::Failed) frameSinceTick_ = false;
            if (!FallBackFrom(event)) Apply(machine_.SourceReported(event, Now()));
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
            machine_.Resumed(Now());
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
    if (++ticks_ % (1000 / kTickMs) == 0) {
        LogSignalProbe();
        LogFrames();
        UpdateFpsCounter();
    }
}

void FeedKeeper::SetGameRateOverride(GameRateOverride setting) {
    processor_->SetOverride(setting);
    Log("Frames: override %s", NameOf(setting));
}

void FeedKeeper::SetChoice(const std::wstring& device, const std::wstring& deviceId,
                           std::optional<Resolution> resolution) {
    choice_ = {device, deviceId, resolution};
    source_->Choose(choice_);
    choiceProven_ = false;
    if (resolution) {
        Log("Feed: choice: device %s, resolution %ux%u", device.empty() ? "automatic" : ToUtf8(device).c_str(),
            resolution->width, resolution->height);
    } else {
        Log("Feed: choice: device %s, resolution default", device.empty() ? "automatic" : ToUtf8(device).c_str());
    }
    if (!running_) return;
    const RecoveryActions actions = machine_.ChoiceChanged(Now());
    ShowState();  // Loading, before the restart blocks this thread
    Apply(actions);
    ShowState();
}

// A chosen resolution that does not start: back to the default mode, at once.
bool FeedKeeper::FallBackFrom(const SourceEvent& event) {
    if (choiceProven_ || !choice_.resolution || !ChosenModeFailed(event)) return false;
    const Resolution failed = *choice_.resolution;
    Log("Feed: resolution %ux%u did not start (hr=0x%08X, %s); back to the default mode", failed.width, failed.height,
        event.code, NameOf(event.stage));
    if (fellBack_) fellBack_(failed, true);  // may call SetChoice itself
    if (choice_.resolution) SetChoice(choice_.device, choice_.deviceId, std::nullopt);
    return true;
}

// A chosen resolution the card does not list (a remembered one, another
// card): the Source opened the default mode instead. Said, and dropped.
void FeedKeeper::DropUnofferedResolution() {
    const std::optional<CaptureMode> mode = source_->CurrentMode();
    if (!choice_.resolution || !mode) return;
    const Resolution dropped = *choice_.resolution;
    if (mode->width == dropped.width && mode->height == dropped.height) return;
    choice_.resolution.reset();
    source_->Choose(choice_);
    Log("Feed: resolution %ux%u is not offered by \"%s\"; the default mode", dropped.width, dropped.height,
        ToUtf8(source_->DeviceName()).c_str());
    if (fellBack_) fellBack_(dropped, false);
}

void FeedKeeper::KeepDeviceInUse(const std::wstring& device, const std::wstring& deviceId) {
    choice_.device = device;
    choice_.deviceId = deviceId;
    source_->Choose(choice_);
    Log("Feed: choice: device %s, the one playing (no restart)", ToUtf8(device).c_str());
}

FrameRates FeedKeeper::Rates() { return processor_->RatesAt(QpcNow()); }

void FeedKeeper::SetFpsCounter(bool on) {
    fpsCounter_ = on;
    Log("Frames: FPS counter %s", fpsCounter_ ? "on" : "off");
    UpdateFpsCounter();
}

// Never presents by itself: Lossless Scaling would count it as a game frame.
// Only while Playing: over a state message (Loading, No signal) it would be a
// number from a Feed that is not there.
void FeedKeeper::UpdateFpsCounter() {
    const bool show = fpsCounter_ && machine_.State() == VideoAreaState::Playing;
    presenter_.ShowTextWithNextFrame(TextPlace::TopLeft,
                                     show ? VideoAreaText{FpsCounterText(Rates()), L""} : VideoAreaText{});
}

// Once a second: enough to tell from the log alone whether a game's Repeated
// Frames arrive bit-identical (near repeats, close Unique Frames) and what the
// comparison costs (a still picture is a full scan per frame).
void FeedKeeper::LogFrames() {
    const UniqueFrameTotals now = processor_->Totals();
    const UniqueFrameTotals& was = loggedTotals_;
    const UniqueCloseness close = processor_->TakeCloseness();
    const FrameRates rates = Rates();
    const uint32_t uniqueCount = timing_.uniqueCount.exchange(0), repeatCount = timing_.repeatCount.exchange(0);
    const int64_t uniqueTicks = timing_.uniqueTicks.exchange(0), repeatTicks = timing_.repeatTicks.exchange(0);
    const int64_t uniqueWorst = timing_.uniqueWorst.exchange(0), repeatWorst = timing_.repeatWorst.exchange(0);
    const int64_t afterWorst = timing_.afterWorst.exchange(0);
    Log("Frames: arrived %llu, unique %llu, repeated exact %llu near %llu, presented %llu, dropped %llu; "
        "game rate %d, presented rate %d, signal rate %d; override %s, tolerance %.4f; near max %.5f, close unique %u min %.5f; "
        "before present ms unique avg %.3f worst %.3f, repeated avg %.3f worst %.3f; after present worst %.3f; "
        "FPS counter %s",
        static_cast<unsigned long long>(now.arrived - was.arrived),
        static_cast<unsigned long long>(now.unique - was.unique),
        static_cast<unsigned long long>(now.repeatedExact - was.repeatedExact),
        static_cast<unsigned long long>(now.repeatedNear - was.repeatedNear),
        static_cast<unsigned long long>(now.presented - was.presented),
        static_cast<unsigned long long>(now.dropped - was.dropped), rates.gameRate, rates.presentedRate,
        rates.signalRate, NameOf(processor_->Override()), processor_->Tolerance(), close.nearShareMax, close.closeUnique,
        close.closeUnique ? close.closeShareMin : 0.0, uniqueCount ? QpcToMs(uniqueTicks) / uniqueCount : 0.0,
        QpcToMs(uniqueWorst), repeatCount ? QpcToMs(repeatTicks) / repeatCount : 0.0, QpcToMs(repeatWorst), QpcToMs(afterWorst),
        !fpsCounter_ ? "off" : !rates.warm ? "warming up" : ("\"" + ToUtf8(FpsCounterText(rates)) + "\"").c_str());
    loggedTotals_ = now;
}

void FeedKeeper::Apply(RecoveryActions actions) {
    if (actions.restartFeed) RestartFeed();
    if (actions.startSound) {
        ShowState();  // the picture first: opening the sound blocks this thread
        StartSound();
    }
}

void FeedKeeper::RestartFeed() {
    const int64_t began = Now();
    sound_.Stop();
    soundRunning_ = false;
    source_->Stop();
    frameSinceTick_ = false;  // a frame from the stopped Source must not count for the new one
    processor_->Reset();  // the new Source's first frame is always shown
    const uint32_t generation = ++generation_;
    machine_.FeedRestarted(began);
    UpdateFpsCounter();  // blank before the new Source's first frame, not the old Source's number
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
            const uint64_t uniqueBefore = processor_->Totals().unique;
            const int64_t began = QpcNow();
            processor_->Process(std::move(captured), toShow_);
            const int64_t spent = QpcNow() - began;
            for (TimedFrame& frame : toShow_) presenter_.Submit(std::move(frame));
            const int64_t submitted = QpcNow();
            processor_->AfterSubmit();
            RaiseTo(timing_.afterWorst, QpcNow() - submitted);
            if (processor_->Totals().unique != uniqueBefore) {
                timing_.uniqueTicks += spent;
                timing_.uniqueCount++;
                RaiseTo(timing_.uniqueWorst, spent);
            } else {
                timing_.repeatTicks += spent;
                timing_.repeatCount++;
                RaiseTo(timing_.repeatWorst, spent);
            }
        },
        [window, generation](const SourceEvent& event) {
            PostMessageW(window, FeedEventMessage(), Pack(generation, static_cast<uint32_t>(event.kind), event.stage),
                         static_cast<LPARAM>(event.code));
        });
    lastRestartMs_ = Now() - began;
    DropUnofferedResolution();
}

void FeedKeeper::StartSound() {
    sound_.Stop();
    soundRunning_ = sound_.Start(source_->DeviceName());
    if (!soundRunning_) Log("Feed: no sound; the picture carries on");
    machine_.SoundStarted(soundRunning_, Now());
}

void FeedKeeper::ShowState() {
    const VideoAreaState state = machine_.State();
    // Error and Card busy carry a failure; it can change while the state stays.
    const bool carriesFailure = state == VideoAreaState::Error || state == VideoAreaState::CardBusy;
    const SourceEvent failure = carriesFailure ? machine_.Failure() : SourceEvent{};
    if (state != shown_ || failure != shownFailure_) {
        if (carriesFailure) {
            Log("Video Area: %s -> %s (hr=0x%08X, %s)", NameOf(shown_), NameOf(state), failure.code,
                NameOf(failure.stage));
        } else {
            Log("Video Area: %s -> %s", NameOf(shown_), NameOf(state));
        }
        shown_ = state;
        shownFailure_ = failure;
        UpdateFpsCounter();  // first: the present for the state change draws it (or no longer does)
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
