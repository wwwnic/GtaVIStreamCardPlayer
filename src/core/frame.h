// A captured frame of the Feed picture, as it travels Source -> Processor -> Presenter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gp {

enum class PixelFormat { YUY2, NV12 };

// Colour description read from the capture mode. "Unspecified" means the
// Capture Card did not say; see MakeYuvToRgb for the defaults applied.
enum class ColourRange { Unspecified, Limited, Full };
enum class ColourMatrix { Unspecified, BT601, BT709 };

struct Colour {
    ColourRange range = ColourRange::Unspecified;
    ColourMatrix matrix = ColourMatrix::Unspecified;
    bool operator==(const Colour&) const = default;
};

struct Frame {
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::YUY2;
    Colour colour;
    int64_t arrivalQpc = 0;  // QueryPerformanceCounter when the Source received it
    uint64_t sequence = 0;   // counts frames from the Source, starting at 1
    // Tightly packed, top-down, no row padding.
    // YUY2: height rows of width*2 bytes. NV12: Y plane (width*height), then
    // interleaved UV plane (width * height/2).
    std::vector<uint8_t> pixels;
};

// Frames are immutable once handed on, so stages may keep earlier ones
// (a Processor needs the previous Unique Frames) without copying.
using FramePtr = std::shared_ptr<const Frame>;

// Bytes in Frame::pixels for a picture of this format and size.
size_t FrameByteSize(PixelFormat format, uint32_t width, uint32_t height);

// Hands out frames and takes their pixel buffers back when the last holder
// lets go, so steady capture does not allocate. Thread-safe. Frames may
// outlive the pool.
class FramePool {
public:
    FramePool();
    // A frame whose pixels buffer is exactly byteSize long; other fields are reset.
    std::shared_ptr<Frame> Acquire(size_t byteSize);

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};

}  // namespace gp
