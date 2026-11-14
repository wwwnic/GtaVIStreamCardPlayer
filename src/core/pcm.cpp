#include "core/pcm.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gp {

namespace {

float ReadSample(const uint8_t* p, SampleType type) {
    switch (type) {
    case SampleType::Float32: {
        float value;
        std::memcpy(&value, p, 4);
        return value;
    }
    case SampleType::Int16: {
        int16_t value;
        std::memcpy(&value, p, 2);
        return value / 32768.0f;
    }
    case SampleType::Int24: {
        const int32_t value = static_cast<int32_t>(static_cast<uint32_t>(p[0]) << 8 | static_cast<uint32_t>(p[1]) << 16 |
                                                   static_cast<uint32_t>(p[2]) << 24);
        return static_cast<float>(value / 2147483648.0);
    }
    case SampleType::Int32: {
        int32_t value;
        std::memcpy(&value, p, 4);
        return static_cast<float>(value / 2147483648.0);
    }
    }
    return 0.0f;
}

// Full scale as a 32-bit integer, clipped.
int32_t ToInt32(float sample) {
    const double scaled = std::round(std::clamp(static_cast<double>(sample), -1.0, 1.0) * 2147483648.0);
    return static_cast<int32_t>(std::clamp(scaled, -2147483648.0, 2147483647.0));
}

void WriteSample(uint8_t* p, SampleType type, float sample) {
    switch (type) {
    case SampleType::Float32: std::memcpy(p, &sample, 4); return;
    case SampleType::Int16: {
        const float scaled = std::round(std::clamp(sample, -1.0f, 1.0f) * 32768.0f);
        const int16_t value = static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
        std::memcpy(p, &value, 2);
        return;
    }
    case SampleType::Int24: {
        const uint32_t value = static_cast<uint32_t>(ToInt32(sample));
        p[0] = static_cast<uint8_t>(value >> 8);
        p[1] = static_cast<uint8_t>(value >> 16);
        p[2] = static_cast<uint8_t>(value >> 24);
        return;
    }
    case SampleType::Int32: {
        const int32_t value = ToInt32(sample);
        std::memcpy(p, &value, 4);
        return;
    }
    }
}

}  // namespace

size_t PcmFormat::BytesPerSample() const {
    switch (type) {
    case SampleType::Int16: return 2;
    case SampleType::Int24: return 3;
    default: return 4;
    }
}

void ToStereo(const uint8_t* data, size_t frames, const PcmFormat& format, float* stereo) {
    const size_t sampleBytes = format.BytesPerSample();
    const size_t frameBytes = format.BytesPerFrame();
    for (size_t f = 0; f < frames; f++) {
        const uint8_t* frame = data + f * frameBytes;
        if (format.channels == 0) {
            stereo[2 * f] = stereo[2 * f + 1] = 0.0f;
            continue;
        }
        const float left = ReadSample(frame, format.type);
        const float right = format.channels >= 2 ? ReadSample(frame + sampleBytes, format.type) : left;
        stereo[2 * f] = left;
        stereo[2 * f + 1] = right;
    }
}

void FromStereo(const float* stereo, size_t frames, const PcmFormat& format, uint8_t* data) {
    const size_t sampleBytes = format.BytesPerSample();
    const size_t frameBytes = format.BytesPerFrame();
    for (size_t f = 0; f < frames; f++) {
        uint8_t* frame = data + f * frameBytes;
        const float left = stereo[2 * f];
        const float right = stereo[2 * f + 1];
        if (format.channels == 1) {
            WriteSample(frame, format.type, 0.5f * (left + right));
            continue;
        }
        for (uint32_t c = 0; c < format.channels; c++) {
            WriteSample(frame + c * sampleBytes, format.type, c == 0 ? left : c == 1 ? right : 0.0f);
        }
    }
}

}  // namespace gp
