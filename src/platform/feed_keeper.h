// Keeps the Feed going (ticket 06): runs Source -> Processor -> Presenter and
// the sound, follows the Video Area state machine (core/video_area_state.h),
// restarts picture and sound together when something goes wrong, and shows
// the state message in the Video Area. Window's thread only.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
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

    // Auto / 30 / 60 (ticket 08 will put it in the menu). Takes effect at once.
    void SetGameRateOverride(GameRateOverride setting);
    // Signal Rate and Game Rate now (for the Help or debug display).
    FrameRates Rates();
    // The F key: the FPS counter on or off. Off at every launch. It is only
    // drawn into frames presented anyway, so it shows with the next one.
    void ToggleFpsCounter();

private:
    void Tick();
    void Apply(RecoveryActions actions);
    void RestartFeed();
    void StartSound();
    void ShowState();
    void LogSignalProbe();
    void LogFrames();
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
        std::atomic<uint32_t> uniqueCount{0}, repeatCount{0};
    } timing_;
    std::unique_ptr<Source> source_;
    Sound& sound_;
    bool soundRunning_ = false;
    VideoAreaStateMachine machine_;
    VideoAreaState shown_ = VideoAreaState::Loading;
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
