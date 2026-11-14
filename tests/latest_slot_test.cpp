#include "doctest.h"

#include "core/latest_slot.h"

using gp::LatestSlot;

TEST_CASE("an empty slot gives nothing") {
    LatestSlot<int> slot;
    CHECK(slot.Take() == std::nullopt);
}

TEST_CASE("a value put in the slot is taken exactly once") {
    LatestSlot<int> slot;
    CHECK(slot.Put(7) == false);
    CHECK(slot.Take() == 7);
    CHECK(slot.Take() == std::nullopt);
}

TEST_CASE("a newer value replaces one that was not taken, and says so") {
    LatestSlot<int> slot;
    slot.Put(1);
    CHECK(slot.Put(2) == true);
    CHECK(slot.Take() == 2);
    CHECK(slot.Take() == std::nullopt);
}

TEST_CASE("a value put after a take does not count as a replacement") {
    LatestSlot<int> slot;
    slot.Put(1);
    slot.Take();
    CHECK(slot.Put(2) == false);
}
