// Presenter: the last stage of the pipeline (Source -> Processor -> Presenter).
#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>

#include "core/frame_schedule.h"
#include "core/presentation_info.h"
#include "core/processor.h"
#include "core/video_area_text.h"

namespace gp {

class FrameGenerator;

// Owns the Direct3D 11 device and the flip-model swap chain, and shows frames
// in the window's Video Area: converted from YUV to RGB by its own shader,
// fitted in the capture mode's shape (16:9 for 1080p) with black around it.
//
// It presents on its own thread. Submit only leaves the frame in a
// single-frame slot and wakes that thread, so capture never waits for the GPU
// and nothing queues: a newer frame replaces one not shown yet.
//
// Every frame is shown as soon as it is taken from the slot (a
// TimedFrame::showAtQpc in the future is not waited for). With a frame
// generator set (ADR 0003) each Unique Frame becomes a timed sequence
// instead: its Generated Frames first, then the frame itself, evenly spaced
// by core/frame_schedule.h. Without one, nothing of that runs.
class Presenter {
public:
    struct Stats {
        uint32_t presented = 0;          // frames shown (Present succeeded)
        uint32_t replaced = 0;           // frames overtaken by a newer one before being shown
        double averageLatencyMs = 0.0;   // arrival at the Source -> Present returned
        double worstLatencyMs = 0.0;
    };

    // The timed sequences since the previous TakeTimedStats (all zero without
    // a frame generator).
    struct TimedStats {
        TimedFrameStats frames;
        double latenessAverageMs = 0.0;   // a present against its due time
        double latenessWorstMs = 0.0;
        double presentCallWorstMs = 0.0;  // the longest the swap chain's Present took to return
    };

    Presenter();
    ~Presenter();
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    // Creates the device and swap chain for this window and starts the
    // presenting thread. Call on the window's thread. If Direct3D cannot be
    // set up now (reason in the debug log) the thread keeps trying once a
    // second, as after a device loss. Returns false only if the thread cannot
    // be started; the other calls are then harmless no-ops.
    bool Start(HWND window);

    // Stops the thread and releases the device. Call on the window's thread,
    // while the window still exists. Safe to call when not started. Answers
    // the thread's window messages while it waits.
    void Stop();

    // "Any thread" below: any thread, but not while Start or Stop runs (stop
    // whatever feeds the Presenter first).

    // Hands over a frame to show. Any thread; never blocks on the GPU.
    void Submit(TimedFrame frame, uint32_t feedGeneration);

    // Tells the Presenter the window's client size changed. Any thread; returns at once.
    void VideoAreaResized();

    // Shows `text` at `place` in the Video Area, replacing what was there; an
    // empty text removes it. durationMs > 0: removed by itself after that long
    // (brief feedback). Drawn even when no frames arrive. The same text again
    // presents nothing (a brief one only gets its time restarted). Any thread.
    void ShowText(TextPlace place, VideoAreaText text, int durationMs = 0);

    // Like ShowText, but never causes a present: the text is drawn into the
    // next frame presented for another reason (a Unique Frame, a resize). For
    // the FPS counter, since Lossless Scaling counts every present as a game
    // frame. On a still picture it waits. Any thread.
    void ShowTextWithNextFrame(TextPlace place, VideoAreaText text);

    // false: the Video Area is black behind the texts (a state message is
    // showing); frames are still taken. Any thread.
    void ShowPicture(bool visible);

    // V-sync (the View menu). Off, the default: each frame is presented the
    // moment it is ready, tearing allowed, and the FreeSync monitor follows.
    // On: presents wait for the monitor's refresh (sync interval 1), no
    // tearing. From the next present. Any thread. Ignored while a frame
    // generator is set: waiting for the monitor would break the timing.
    void SetVsync(bool on);

    // Use EASU and RCAS when the picture is enlarged. Any thread.
    void SetSharpUpscale(bool on);

    // Sets the generator asked for Generated Frames with each Unique Frame,
    // from the next one; empty (the default): none, every frame is shown
    // alone, at once. With one set, a Unique Frame is shown after its
    // Generated Frames, one step apart, and a text or a resize is drawn into
    // the next of those frames instead of being presented by itself. The
    // generator is used on the presenting thread only. Any thread.
    void SetFrameGenerator(std::shared_ptr<FrameGenerator> generator);

    // Counters since the previous call. With a frame generator, `presented`
    // counts Unique Frames (not their Generated Frames) and the latency runs
    // to the present of the Unique Frame itself.
    Stats TakeStats();
    PresentationInfo Presentation();
    TimedStats TakeTimedStats();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gp
