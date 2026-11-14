#include "doctest.h"

#include <vector>

#include "core/sound_ring.h"

using gp::SoundRing;

namespace {
// Stereo frames whose left sample is the frame number and right is its negative.
std::vector<float> Frames(int first, int count) {
    std::vector<float> samples;
    for (int i = first; i < first + count; i++) {
        samples.push_back(static_cast<float>(i));
        samples.push_back(static_cast<float>(-i));
    }
    return samples;
}
}  // namespace

TEST_CASE("frames come out of the sound ring in the order they went in, across the wrap-around") {
    SoundRing ring(4);
    std::vector<float> out(2 * 4);
    ring.Write(Frames(1, 3).data(), 3);
    CHECK(ring.Read(out.data(), 2) == 2);
    CHECK(ring.Write(Frames(4, 3).data(), 3) == 0);  // wraps past the end
    CHECK(ring.Fill() == 4);
    CHECK(ring.Read(out.data(), 4) == 4);
    CHECK(out == Frames(3, 4));
    CHECK(ring.Fill() == 0);
}

TEST_CASE("reading more than the sound ring holds (underrun) gives what there is and leaves it empty") {
    SoundRing ring(8);
    ring.Write(Frames(1, 3).data(), 3);
    std::vector<float> out(2 * 5, 99.0f);
    CHECK(ring.Read(out.data(), 5) == 3);
    CHECK(std::vector<float>(out.begin(), out.begin() + 6) == Frames(1, 3));
    CHECK(ring.Fill() == 0);
    CHECK(ring.Read(out.data(), 5) == 0);
}

TEST_CASE("writing into a full sound ring (overrun) drops the oldest frames and keeps the newest") {
    SoundRing ring(4);
    ring.Write(Frames(1, 3).data(), 3);
    CHECK(ring.Write(Frames(4, 3).data(), 3) == 2);
    CHECK(ring.Fill() == 4);
    std::vector<float> out(2 * 4);
    ring.Read(out.data(), 4);
    CHECK(out == Frames(3, 4));
}

TEST_CASE("a write larger than the whole sound ring keeps only its newest frames") {
    SoundRing ring(4);
    ring.Write(Frames(1, 2).data(), 2);
    CHECK(ring.Write(Frames(3, 6).data(), 6) == 4);
    std::vector<float> out(2 * 4);
    CHECK(ring.Read(out.data(), 4) == 4);
    CHECK(out == Frames(5, 4));
}

TEST_CASE("discarding drops the oldest frames") {
    SoundRing ring(4);
    ring.Write(Frames(1, 4).data(), 4);
    ring.Discard(3);
    CHECK(ring.Fill() == 1);
    std::vector<float> out(2);
    ring.Read(out.data(), 1);
    CHECK(out == Frames(4, 1));
}
