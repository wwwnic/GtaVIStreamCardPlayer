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
#include "core/video_area_state.h"
#include "platform/presenter.h"
#include "platform/sound.h"

namespace gp {

class FeedKeeper {
public:
    // Starts and stops `sound` with the picture; its audio settings are the caller's.
    FeedKeeper(HWND window, Presenter& presenter, Sound& sound);
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

private:
    void Tick();
    void Apply(RecoveryActions actions);
    void RestartFeed();
    void StartSound();
    void ShowState();
    void LogSignalProbe();
    int64_t Now() const;

    HWND window_;
    Presenter& presenter_;
    std::unique_ptr<Processor> processor_;
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
