#include "core/luma_summary.h"

namespace gp {

LumaSummary SummarizeLuma(const Frame& frame) {
    LumaSummary summary;
    if (frame.width == 0 || frame.height == 0 ||
        frame.pixels.size() < FrameByteSize(frame.format, frame.width, frame.height)) {
        return summary;
    }
    // 32 x 18 points, at the centres of a grid's cells.
    const uint32_t columns = 32, rows = 18;
    const size_t bytesPerPixel = frame.format == PixelFormat::YUY2 ? 2 : 1;  // Y is the first byte of each pixel
    const size_t stride = frame.width * bytesPerPixel;
    int low = 255, high = 0;
    long long sum = 0;
    for (uint32_t r = 0; r < rows; r++) {
        const size_t y = (static_cast<size_t>(r) * 2 + 1) * frame.height / (rows * 2);
        for (uint32_t c = 0; c < columns; c++) {
            const size_t x = (static_cast<size_t>(c) * 2 + 1) * frame.width / (columns * 2);
            const int luma = frame.pixels[y * stride + x * bytesPerPixel];
            if (luma < low) low = luma;
            if (luma > high) high = luma;
            sum += luma;
        }
    }
    summary.min = low;
    summary.max = high;
    summary.mean = static_cast<int>(sum / (columns * rows));
    return summary;
}

}  // namespace gp
