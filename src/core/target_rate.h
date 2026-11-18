// The Target Rate the user picks (ticket 03, ADR 0004) and what Target does
// with it on a fixed-refresh monitor: the rate in use, how presents are
// spread over the vsyncs when it is below the refresh, when only real
// frames are shown, and when the uneven-motion notice shows.
//
// Pure logic.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gp {

// The saved Target Rate: Monitor (follows the refresh of the monitor the
// window is on), else frames per second.
inline constexpr int kTargetRateMonitor = 0;
inline constexpr int kTargetRateLowest = 1;  // a saved number is accepted from here (Q10)
inline constexpr int kTargetRateHighest = 1000;

// The rate in use: the smaller of the Target Rate and the refresh (Q16). The
// user's number is kept as chosen.
int RateInUse(int targetRate, int refresh);

// Q17: whether a Unique Frame `intervalTicks` after the one before gets real
// frames only, because the rate in use is not above the Game Rate. A tenth
// of margin absorbs arrival jitter, so 60 at a 60 fps Game Rate never
// generates (and never drops a real frame for a Generated one).
inline constexpr int kRealOnlyMarginPercent = 10;
bool RealFramesOnly(int64_t intervalTicks, int rateInUse, int64_t ticksPerSecond);

// Q18: the on-screen notice that a rate cannot be shown evenly (one that
// does not divide the refresh: some frames stay one vsync longer). Once,
// when an uneven rate starts applying: chosen, Target turned on, or the
// window moved to a monitor with another refresh. Not again while the same
// rate applies on the same refresh.
class UnevenNotice {
public:
    // The rate in use and the refresh, each time they are read. The notice
    // text when it is to show now.
    std::optional<std::string> RateApplies(int rateInUse, int refresh);
    // Target ended: the next RateApplies is a start again.
    void Reset();

private:
    int rate_ = 0, refresh_ = 0;  // as last told; 0: nothing applies
};

// One present of Target: how many vsyncs it stays on screen (its sync
// interval, 1 to 4, what DXGI accepts) and whether it is one of the `rate`
// per second, so it may show a new frame. Not fresh: the rest of a span
// longer than 4 vsyncs, the same picture again.
inline constexpr int kMostSyncInterval = 4;
struct PacedPresent {
    int syncInterval = 1;
    bool fresh = true;
};

// Spreads `rate` presents per second over `refresh` vsyncs per second as
// evenly as possible (Bresenham): each spans the floor or the ceiling of
// refresh / rate vsyncs, the longer ones evenly among the shorter. At
// rate == refresh every present spans one vsync, as before.
class PresentPacer {
public:
    PresentPacer(int rate, int refresh);
    PacedPresent Next();

private:
    int rate_ = 1;
    int refresh_ = 1;
    int carry_ = 0;    // the Bresenham remainder, 0..rate-1
    int pending_ = 0;  // vsyncs of the current span not covered by a present yet
};

}  // namespace gp
