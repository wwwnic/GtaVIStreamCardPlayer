// When the monitor's vsyncs are, for Target (ADR 0004), from the swap chain's
// frame statistics (IDXGISwapChain::GetFrameStatistics): the QPC time of a
// recent vblank, its refresh count and the count of the present shown then.
// Pure logic: the Presenter passes the samples and the clock in.
#pragma once

#include <cstdint>
#include <optional>

namespace gp {

struct VsyncSample {
    uint64_t presentCount = 0;   // DXGI_FRAME_STATISTICS::PresentCount: the latest present shown
    uint64_t refreshCount = 0;   // PresentRefreshCount: the vblank it was first shown at
    int64_t vblankTicks = 0;     // SyncQPCTime: the time of the vblank SyncRefreshCount
    uint64_t syncRefresh = 0;    // SyncRefreshCount
};

class VsyncClock {
public:
    // A refresh period measured over at least this many vblanks replaces the nominal one.
    static constexpr uint64_t kMeasureOverRefreshes = 64;

    // A new sample. Returns the vsyncs since the previous one that showed no
    // new present (missed: a present came too late, the previous frame stayed).
    // The same present again, or counts going backwards (statistics restarted),
    // count nothing.
    uint32_t Sample(const VsyncSample& sample);

    // Forget every sample (another monitor, another swap chain).
    void Reset();

    // The display time of a present made at nowTicks: the first vblank after
    // it, from the latest sample and the period. With no sample yet: nowTicks
    // (the Presenter was just woken by the swap chain, right after a vblank).
    // Never less than half a period after the time it gave before, so two
    // presents never claim the same vblank.
    int64_t NextVsync(int64_t nowTicks, int64_t nominalPeriodTicks);

    // The measured period when there is one within a quarter of the nominal
    // one, else the nominal one.
    int64_t Period(int64_t nominalPeriodTicks) const;

    bool HaveSample() const { return latest_.has_value(); }

private:
    std::optional<VsyncSample> latest_;
    std::optional<VsyncSample> reference_;  // the first since Reset, for measuring the period
    int64_t measuredPeriod_ = 0;
    std::optional<int64_t> lastGiven_;
};

}  // namespace gp
