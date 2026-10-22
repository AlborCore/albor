#include "core/content_hash.h"

namespace albor
{
    std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) noexcept {
        // the hash is fnv one a, which is stable across builds and platforms because it is pure arithmetic (§15, AD-34)
        constexpr std::uint64_t OFFSET_BASIS = 14695981039346656037ULL;
        // the prime is the fnv one a multiplier, which is a constant rather than a tuning knob (§15, AD-34)
        constexpr std::uint64_t PRIME = 1099511628211ULL;
        // the basis starts the running value, so two different resources cannot start alike (§15)
        std::uint64_t hash = OFFSET_BASIS;
        // every byte is folded in, because a hash that skips bytes would collide on a partial resource (§15)
        for (const std::uint8_t byte : bytes) {
            // the byte is spread across the word, because one byte into a sixty four bit word shifts one step (§15)
            hash ^= static_cast<std::uint64_t>(byte);
            // the running value is multiplied rather than rotated, which is what makes the hash avalanche (§15)
            hash *= PRIME;
        }
        // the finished hash identifies the bytes on every platform, which is the whole point (§15, AD-34)
        return hash;
    }
} // namespace albor
