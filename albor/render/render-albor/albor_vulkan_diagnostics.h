#pragma once

#include <cstdint>

namespace albor::render
{
    // how many frames this process handed to a window's swapchain, which a test reads to prove a present ran (§6.3)
    [[nodiscard]] std::uint64_t vulkan_presented_frames();

    // how many device memory allocations this process made, which a steady frame must not raise (§6.3, AD-06)
    [[nodiscard]] std::uint64_t vulkan_device_allocations();
} // namespace albor::render
