#pragma once

#include <cstdint>

namespace albor::render
{
    // how many frames this process handed to a window's swapchain, which a test reads to prove a present ran (§6.3)
    [[nodiscard]] std::uint64_t d3d_presented_frames();
} // namespace albor::render
