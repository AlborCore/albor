#pragma once

#include <cstddef>
#include <span>

namespace albor::render
{
    // hand out the precompiled metal library the device loads once at startup, which the build embedded (AD-16)
    [[nodiscard]] std::span<const std::byte> metal_library_data();
} // namespace albor::render
