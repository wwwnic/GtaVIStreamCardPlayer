// Presenter: the last stage of the pipeline (Source -> Processor -> Presenter).
#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>

#include "core/processor.h"
#include "core/video_area_text.h"

namespace gp {

// Owns the Direct3D 11 device and the flip-model swap chain, and shows frames
// in the window's Video Area: converted from YUV to RGB by its own shader,
// fitted 16:9 with black around it.
//
// It presents on its own thread. Submit only leaves the frame in a
// single-frame slot and wakes that thread, so capture never waits for the GPU
// and nothing queues: a newer frame replaces one not shown yet.
//
// Ticket 02 limit: every frame is shown as soon as it is taken from the slot;
// a TimedFrame::showAtQpc in the future is not waited for yet (no Processor
// asks for that until in-app Frame Generation exists).
class Presenter {
public:
    struct Stats {
        uint32_t presented = 0;          // frames shown
        uint32_t replaced = 0;           // frames overtaken by a newer one before being shown
        double averageLatencyMs = 0.0;   // arrival at the Source -> Present returned
        double worstLatencyMs = 0.0;
    };

    Presenter();
    ~Presenter();
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    // Creates the device and swap chain for this window and starts the
    // presenting thread. Call on the window's thread. Returns false (reason in
    // the debug log) if Direct3D cannot be set up; the other calls are then
    // harmless no-ops.
    bool Start(HWND window);

    // Stops the thread and releases the device. Call on the window's thread,
    // while the window still exists. Safe to call when not started.
    void Stop();

    // Hands over a frame to show. Any thread; never blocks on the GPU.
    void Submit(TimedFrame frame);

    // Tells the Presenter the window's client size changed. Any thread; returns at once.
    void VideoAreaResized();

    // Shows `text` at `place` in the Video Area, replacing what was there; an
    // empty text removes it. durationMs > 0: removed by itself after that long
    // (brief feedback). Drawn even when no frames arrive. Any thread.
    void ShowText(TextPlace place, VideoAreaText text, int durationMs = 0);

    // false: the Video Area is black behind the texts (a state message is
    // showing); frames are still taken. Any thread.
    void ShowPicture(bool visible);

    // Counters since the previous call.
    Stats TakeStats();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gp
