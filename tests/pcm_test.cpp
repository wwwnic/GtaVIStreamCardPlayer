#include "doctest.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/pcm.h"

using gp::PcmFormat;
using gp::SampleType;

namespace {
template <typename T>
std::vector<uint8_t> Bytes(const std::vector<T>& values) {
    std::vector<uint8_t> bytes(values.size() * sizeof(T));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}
}  // namespace

TEST_CASE("16-bit stereo sound becomes float stereo") {
    const PcmFormat format{SampleType::Int16, 2, 44100};
    const auto bytes = Bytes<int16_t>({16384, -32768, 0, 32767});
    std::vector<float> stereo(4);
    gp::ToStereo(bytes.data(), 2, format, stereo.data());
    CHECK(stereo[0] == doctest::Approx(0.5));
    CHECK(stereo[1] == doctest::Approx(-1.0));
    CHECK(stereo[2] == doctest::Approx(0.0));
    CHECK(stereo[3] == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("mono sound plays on both sides; extra channels beyond front left and right are ignored") {
    std::vector<float> stereo(2);
    const auto mono = Bytes<float>({0.25f});
    gp::ToStereo(mono.data(), 1, PcmFormat{SampleType::Float32, 1, 48000}, stereo.data());
    CHECK(stereo == std::vector<float>{0.25f, 0.25f});

    const auto surround = Bytes<float>({0.1f, 0.2f, 0.9f, 0.9f, 0.9f, 0.9f});
    gp::ToStereo(surround.data(), 1, PcmFormat{SampleType::Float32, 6, 48000}, stereo.data());
    CHECK(stereo == std::vector<float>{0.1f, 0.2f});
}

TEST_CASE("24-bit packed and 32-bit integer sound become float stereo") {
    // 0x400000 is half of full scale in 24 bits; little-endian bytes.
    const std::vector<uint8_t> packed = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0};
    std::vector<float> stereo(2);
    gp::ToStereo(packed.data(), 1, PcmFormat{SampleType::Int24, 2, 48000}, stereo.data());
    CHECK(stereo[0] == doctest::Approx(0.5));
    CHECK(stereo[1] == doctest::Approx(-0.5));

    const auto wide = Bytes<int32_t>({1073741824, -1073741824});
    gp::ToStereo(wide.data(), 1, PcmFormat{SampleType::Int32, 2, 48000}, stereo.data());
    CHECK(stereo[0] == doctest::Approx(0.5));
    CHECK(stereo[1] == doctest::Approx(-0.5));
}

TEST_CASE("float stereo becomes 16-bit sound, clipped rather than wrapped when too loud") {
    const std::vector<float> stereo = {0.5f, -0.5f, 1.5f, -1.5f};
    std::vector<uint8_t> bytes(8);
    gp::FromStereo(stereo.data(), 2, PcmFormat{SampleType::Int16, 2, 48000}, bytes.data());
    int16_t samples[4];
    std::memcpy(samples, bytes.data(), sizeof samples);
    CHECK(samples[0] == 16384);
    CHECK(samples[1] == -16384);
    CHECK(samples[2] == 32767);
    CHECK(samples[3] == -32768);
}

TEST_CASE("on a surround output the sound goes to front left and right, the rest silent") {
    const std::vector<float> stereo = {0.1f, 0.2f};
    std::vector<uint8_t> bytes(6 * 4, 0xFF);
    gp::FromStereo(stereo.data(), 1, PcmFormat{SampleType::Float32, 6, 48000}, bytes.data());
    float samples[6];
    std::memcpy(samples, bytes.data(), sizeof samples);
    CHECK(samples[0] == 0.1f);
    CHECK(samples[1] == 0.2f);
    for (int c = 2; c < 6; c++) CHECK(samples[c] == 0.0f);
}

TEST_CASE("on a mono output left and right are mixed") {
    const std::vector<float> stereo = {0.2f, 0.4f};
    std::vector<uint8_t> bytes(4);
    gp::FromStereo(stereo.data(), 1, PcmFormat{SampleType::Float32, 1, 48000}, bytes.data());
    float sample = 0;
    std::memcpy(&sample, bytes.data(), 4);
    CHECK(sample == doctest::Approx(0.3));
}

TEST_CASE("24-bit and 32-bit outputs carry the sound back out unchanged") {
    const std::vector<float> stereo = {0.5f, -0.25f};
    std::vector<float> back(2);
    for (SampleType type : {SampleType::Int24, SampleType::Int32}) {
        const PcmFormat format{type, 2, 48000};
        std::vector<uint8_t> bytes(format.BytesPerFrame());
        gp::FromStereo(stereo.data(), 1, format, bytes.data());
        gp::ToStereo(bytes.data(), 1, format, back.data());
        CHECK(back[0] == doctest::Approx(0.5));
        CHECK(back[1] == doctest::Approx(-0.25));
    }
}
