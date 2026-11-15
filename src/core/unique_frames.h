// Unique Frames (ticket 04): the Processor that drops Repeated Frames, so only
// distinct pictures are presented and Lossless Scaling counts the true Game Rate.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/frame.h"
#include "core/processor.h"

namespace gp {

// THE TOLERANCE TO TUNE. A frame that is not byte-identical to the previous
// Unique Frame still counts as a Repeated Frame when fewer than this share of
// its 8-byte words differ. 0 = exact match only, the default: in a 60 fps game
// a small real change (one character moving) must never be dropped. The
// prototype's rule was 0.5% (0.005); `--tolerance` sets it for one run.
inline constexpr double kNearDuplicateTolerance = 0.0;

// Unique Frames differing in fewer than this share of words are "close" and
// reported (UniqueCloseness), so a session with a 30 fps game shows from the
// log alone whether its repeats arrive slightly different, and by how much.
inline constexpr double kCloseShare = 0.01;

// How a frame compares with the previous Unique Frame.
enum class FrameMatch {
    Different,  // a Unique Frame
    Exact,      // byte-identical: a Repeated Frame (the fast path)
    Near,       // differs, but under the tolerance: a Repeated Frame
};

// `differingShare`, when given, receives the share of 8-byte words that
// differ: 0 for Exact; exact for Near and for a Different frame under
// kCloseShare (and under the tolerance's limit); otherwise 1. Frames of another
// size or format are Different. Counting stops as soon as the share passes
// both limits, so a moving picture costs little; an identical one is one memcmp.
FrameMatch CompareFrames(const Frame& a, const Frame& b, double tolerance, double* differingShare = nullptr);

// How close the calls were, since the previous TakeCloseness.
struct UniqueCloseness {
    uint32_t closeUnique = 0;    // Unique Frames differing in under kCloseShare
    double closeShareMin = 1.0;  // the smallest share among them (1: none)
    double nearShareMax = 0.0;   // the largest share among Near repeats (0: none)
};

// The Auto / 30 / 60 setting (ticket 08 puts it in the menu).
enum class GameRateOverride {
    Auto,   // drop Repeated Frames as detected (default)
    Fps30,  // at most one frame per two captured, preferring Unique Frames
    Fps60,  // drop nothing: every captured frame is presented
};
const char* NameOf(GameRateOverride setting);
// "auto", "30" or "60" (as given to --rate). False, `setting` untouched, otherwise.
bool ParseGameRateOverride(const std::wstring& word, GameRateOverride& setting);

// Running totals since the Processor was made (subtract two readings for a period).
struct UniqueFrameTotals {
    uint64_t arrived = 0;         // captured frames given to the Processor
    uint64_t unique = 0;          // judged Unique Frames
    uint64_t repeatedExact = 0;   // judged Repeated by the exact comparison
    uint64_t repeatedNear = 0;    // judged Repeated by the near-duplicate tolerance
    uint64_t presented = 0;       // emitted to the Presenter
    uint64_t dropped = 0;         // not emitted (arrived - presented)
};

// Frames per second over about the last second. Both are 0 when nothing
// arrives; a Game Rate of 0 with a Signal Rate of 60 is a still picture, which
// is normal (menus, pause screens).
struct FrameRates {
    int signalRate = 0;  // frames arriving from the Capture Card
    int gameRate = 0;    // Unique Frames among them
};

class UniqueFrameProcessor final : public Processor {
public:
    // ticksPerSecond: the unit of Frame::arrivalQpc (the QPC frequency).
    explicit UniqueFrameProcessor(int64_t ticksPerSecond, double tolerance = kNearDuplicateTolerance);

    // Emits the frame at once if it is to be shown; never waits for a later one.
    void Process(FramePtr captured, std::vector<TimedFrame>& out) override;

    // Forgets the previous frames (a new Source started): the next frame is shown.
    // Call only while Process is not running.
    void Reset();

    // Any thread, takes effect from the next frame.
    void SetOverride(GameRateOverride setting) { override_ = setting; }
    GameRateOverride Override() const { return override_; }
    double Tolerance() const { return tolerance_; }

    // Any thread. The rates are smoothed: a change of less than one frame per
    // second is not shown, so a steady game reads steady.
    FrameRates RatesAt(int64_t nowTicks);
    UniqueFrameTotals Totals();
    UniqueCloseness TakeCloseness();

    // The latest Unique Frames, newest first (up to kRecentUnique), for later
    // Processor steps such as Frame Generation. Source's thread only.
    static constexpr size_t kRecentUnique = 3;
    const std::vector<FramePtr>& RecentUniqueFrames() const { return recent_; }

private:
    struct Arrival {
        int64_t ticks;
        bool unique;
    };
    // The rates cover the arrivals of the last two seconds: long enough that
    // uneven pacing reads steady, short enough to follow a change in a second or two.
    static constexpr int kRateWindowSeconds = 2;
    static constexpr size_t kHistory = 512;  // arrivals kept (over 2 s at 240 Hz)

    const int64_t ticksPerSecond_;
    const double tolerance_;
    std::atomic<GameRateOverride> override_{GameRateOverride::Auto};

    // Source's thread only.
    FramePtr reference_;          // the last Unique Frame: each frame is compared with it
    bool referenceShown_ = false; // override 30: false while that frame waits its turn
    uint32_t sinceEmitted_ = 2;   // captured frames since the last emitted one
    std::vector<FramePtr> recent_;

    std::mutex mutex_;            // guards everything below
    Arrival history_[kHistory] = {};
    size_t historyNext_ = 0;
    size_t historyCount_ = 0;
    UniqueFrameTotals totals_;
    UniqueCloseness closeness_;
    int shownSignal_ = 0;
    int shownGame_ = 0;
};

// The on-screen FPS counter's text: "Game Rate / Signal Rate", e.g. "30 / 60".
std::wstring FpsCounterText(FrameRates rates);

}  // namespace gp
