#pragma once

#include <cstddef>

namespace albor::probe
{
    // report whether the probe counts, which is off by default so an ordinary test pays one atomic load (§2.2)
    void set_counting(bool counting);

    // report whether the probe counts (§2.2)
    [[nodiscard]] bool counting();

    // forget every allocation counted so far, which a test calls before the work it measures (§2.2)
    void reset();

    // how many allocations happened while counting was on, which is what a steady state frame test asserts on (§2.2)
    [[nodiscard]] std::size_t count();

    // how many of those allocations are still live, which is what a leak check reads (§2.2)
    [[nodiscard]] std::size_t live();
} // namespace albor::probe
