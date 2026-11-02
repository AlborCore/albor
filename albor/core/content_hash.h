#pragma once

#include <cstdint>
#include <span>

namespace albor
{
    // hash bytes by content, which is the identity every embedded resource shares (§15, AD-34)
    [[nodiscard]] std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) noexcept;
} // namespace albor
