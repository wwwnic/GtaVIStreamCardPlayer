// When the monitor's vsyncs are, for Target (ADR 0004), from the swap chain's
// frame statistics (IDXGISwapChain::GetFrameStatistics): the QPC time of a
// recent vblank, its refresh count and the count of the present shown then.
// Pure logic: the Presenter passes the samples and the clock in.
#pragma once

#include <array>
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
    // More vblanks than this between two samples (about a minute at 144 Hz,
    // while presenting every vsync) is not believable: start over.
    static constexpr uint64_t kMostVblanksBetween = 10000;

    // A new sample. Returns the vsyncs since the previous one that showed no
    // new present (missed: a present came too late, the previous frame stayed).
    // The same present again, or counts going backwards (statistics restarted),
    // count nothing.
    uint32_t Sample(const VsyncSample& sample);

    // A present was made, to be displayed at displayTicks (NextVsync's
    // answer, plus the vsyncs a sync interval over 1 holds it back).
    // presentCount: IDXGISwapChain::GetLastPresentCount after it.
    void Presented(uint64_t presentCount, int64_t displayTicks);

    // Missed vsyncs counted by the present the sample says was shown last:
    // how many vblanks after the one it was made for it was first shown
    // (judged once per present; 0 for one it was not told of). Unlike
    // Sample's count, vblanks with no present at all are not counted: with
    // real frames only the Presenter presents only for new frames. Judged by
    // time (the sample's vblank, less the vblanks since the present was
    // shown), so refresh counts that lag (seen on a second monitor) never
    // make a present late.
    uint32_t Late(const VsyncSample& sample, int64_t nominalPeriodTicks);

    // Forget every sample (another monitor, another swap chain).
    void Reset();

    // The display time of a present made at nowTicks: the first vblank after
    // it, from the latest sample and the period. With no sample yet: nowTicks
    // (the Presenter was just woken by the swap chain, right after a vblank).
    // Never less than half a period after the time it gave before, so two
    // presents never claim the same vblank; but never more than one and a
    // half periods ahead of nowTicks either.
    int64_t NextVsync(int64_t nowTicks, int64_t nominalPeriodTicks);

    // The measured period when there is one (the cadence presents actually
    // follow, even where it is not the monitor's own: seen on a 59 Hz second
    // monitor, still paced at the primary's 144), else the nominal one.
    int64_t Period(int64_t nominalPeriodTicks) const;

    bool HaveSample() const { return latest_.has_value(); }

private:
    std::optional<VsyncSample> latest_;
    std::optional<VsyncSample> reference_;  // the second since Reset, for measuring the period
    int64_t measuredPeriod_ = 0;
    uint32_t validSamples_ = 0;  // since Reset or a restart
    std::optional<int64_t> lastGiven_;
    struct Asked {
        uint64_t presentCount = 0;
        int64_t displayTicks = 0;
    };
    std::array<Asked, 8> asked_{};  // the latest presents, by presentCount % 8
    uint64_t judged_ = 0;           // the latest present Late judged
};

}  // namespace gp
