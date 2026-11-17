// Keeps the Feed going (ticket 06): runs Source -> Processor -> Presenter and
// the sound, follows the Video Area state machine (core/video_area_state.h),
// restarts picture and sound together when something goes wrong, and shows
// the state message in the Video Area. Window's thread only.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/processor.h"
#include "core/source.h"
#include "core/unique_frames.h"
#include "core/video_area_state.h"
#include "platform/presenter.h"
#include "platform/sound.h"

namespace gp {

class FeedKeeper {
public:
    // Starts and stops `sound` with the picture; its audio settings are the caller's.
    // tolerance: the near-duplicate tolerance for Repeated Frames (core/unique_frames.h).
    FeedKeeper(HWND window, Presenter& presenter, Sound& sound, double tolerance = kNearDuplicateTolerance);
    ~FeedKeeper();
    FeedKeeper(const FeedKeeper&) = delete;
    FeedKeeper& operator=(const FeedKeeper&) = delete;

    // Shows Loading and starts the Feed. Retries on its own from then on.
    void Start();
    // Stops sound and capture; nothing is restarted after this.
    void Stop();

    // Call first from the window procedure: its timer, its own messages,
    // WM_DEVICECHANGE and WM_POWERBROADCAST. True: handled, return *result.
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result);

    // Frames that reached the Source since the previous call (for the stats line).
    uint32_t TakeFramesArrived() { return framesArrived_.exchange(0); }
    // Once a second, for the keep-awake hold: a Feed is Playing and frames
    // arrived since the previous call.
    bool TakePlayingSecond();

    // Auto / 30 / 60 (the menu's Frame rate). Takes effect at once.
    void SetGameRateOverride(GameRateOverride setting);

    // The menu's Capture Card (identifier, else name) and Resolution (empty:
    // automatic). Restarts picture and sound at once through Loading, like a
    // launch.
    void SetChoice(const std::wstring& device, const std::wstring& deviceId, std::optional<Resolution> resolution);
    // The Capture Card already playing (the automatic choice), now chosen:
    // later restarts keep to it. Nothing restarts now.
    void KeepDeviceInUse(const std::wstring& device, const std::wstring& deviceId);
    // A chosen resolution that fails to start, or that the card does not
    // offer (offered false; the default mode is then already open), is
    // dropped for the default mode; this is told which one (to clear it from
    // the settings).
    void OnResolutionFallback(std::function<void(Resolution, bool offered)> told) { fellBack_ = std::move(told); }
    // What is in use, for the menu and About.
    std::wstring DeviceInUse() const { return source_->DeviceName(); }
    std::wstring DeviceIdInUse() const { return source_->DeviceId(); }
    std::vector<CaptureMode> Modes() const { return source_->Modes(); }
    std::optional<CaptureMode> CurrentMode() const { return source_->CurrentMode(); }
    // Signal Rate and Game Rate now (for the Help or debug display).
    FrameRates Rates();
    // The FPS counter on or off (the F key, View > FPS counter; remembered).
    // It is only drawn into frames presented anyway, so it shows with the
    // next one and never causes a present of its own.
    void SetFpsCounter(bool on);

private:
    void Tick();
    void Apply(RecoveryActions actions);
    bool FallBackFrom(const SourceEvent& event);
    void DropUnofferedResolution();
    void RestartFeed();
    void StartSound();
    void ShowState();
    void LogSignalProbe();
    void LogFrames();
    void LogTimedFrames();
    void UpdateFpsCounter();
    int64_t Now() const;

    HWND window_;
    Presenter& presenter_;
    std::unique_ptr<UniqueFrameProcessor> processor_;
    bool fpsCounter_ = false;
    UniqueFrameTotals loggedTotals_;   // at the previous Frames: line
    // Time spent in Process, the Source's thread; read and reset once a second.
    struct ProcessTiming {
        std::atomic<int64_t> uniqueTicks{0}, uniqueWorst{0}, repeatTicks{0}, repeatWorst{0};
        std::atomic<int64_t> afterWorst{0};  // AfterSubmit (closeness, after the frame is handed on)
        std::atomic<uint32_t> uniqueCount{0}, repeatCount{0};
    } timing_;
    std::unique_ptr<Source> source_;
    Sound& sound_;
    bool soundRunning_ = false;
    Source::Choice choice_;
    bool choiceProven_ = true;  // a frame arrived since the choice was made
    std::function<void(Resolution, bool)> fellBack_;
    VideoAreaStateMachine machine_;
    VideoAreaState shown_ = VideoAreaState::Loading;
    SourceEvent shownFailure_;  // the failure in the Error or Card busy text shown; default otherwise
    bool shownSoundNote_ = false;
    bool running_ = false;
    HDEVNOTIFY deviceNotification_ = nullptr;

    uint32_t generation_ = 0;                // which Source start a posted event belongs to
    std::atomic<uint32_t> framesArrived_{0};
    std::atomic<bool> frameSinceTick_{false};
    std::atomic<bool> frameSinceAwakeCheck_{false};
    std::atomic<bool> firstFrameWanted_{false};
    int ticks_ = 0;
    int64_t lastRestartMs_ = 0;              // how long the latest restart blocked the window's thread
    std::vector<TimedFrame> toShow_;         // the Source's thread only
};

}  // namespace gp
