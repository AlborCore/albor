#include <vector>

#include <gtest/gtest.h>

#include "tools/alloc-probe/alloc_probe.h"

using namespace albor;

TEST(AllocationProbeReleaseBlindChecks, TheProbeCountsWhatIsAllocated) {
    // arrange
    probe::set_counting(true);
    probe::reset();
    // act a block the framework hands out has to reach the counter, or the probe measures nothing (§2.2)
    const std::vector<int> block(64, 7);
    // assert
    EXPECT_GT(probe::count(), 0u);
    // assert
    EXPECT_GT(block.size(), 0u);
    probe::set_counting(false);
}
