#pragma once

#include <cstdint>
#include <vector>

namespace albor::harfbuzz
{
    // the sfnt bytes a platform typeface fronts, which is how a platform font enters the registry (§15, AD-34)
    [[nodiscard]] std::vector<std::uint8_t> platform_font_bytes(const void* typeface);
} // namespace albor::harfbuzz
