// Sound sample formats, and conversion to and from the stereo float frames
// the Viewer works in. The capture and output endpoints may use different
// formats; the Viewer converts itself rather than relying on exclusive mode.
#pragma once

#include <cstddef>
#include <cstdint>

namespace gp {

enum class SampleType {
    Float32,
    Int16,
    Int24,  // packed, 3 bytes per sample
    Int32,  // also 24-bit samples carried in a 32-bit container (left-aligned)
};

struct PcmFormat {
    SampleType type = SampleType::Float32;
    uint32_t channels = 2;
    uint32_t rate = 48000;

    size_t BytesPerSample() const;
    size_t BytesPerFrame() const { return BytesPerSample() * channels; }
};

// Converts `frames` frames to interleaved stereo float. Mono is played on
// both sides; channels after front left and right are ignored.
void ToStereo(const uint8_t* data, size_t frames, const PcmFormat& format, float* stereo);

// Converts interleaved stereo float to `format`. Too-loud samples are
// clipped. A mono output gets the mix of both; extra channels get silence.
void FromStereo(const float* stereo, size_t frames, const PcmFormat& format, uint8_t* data);

}  // namespace gp
