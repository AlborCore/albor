#include <cstdint>

#include <gtest/gtest.h>

#include "core/content_hash.h"
#include "resources/embedded_font.h"

TEST(ContentHashChecks, HashIsOverTheBytesAndIsStable) {
    // arrange
    const std::uint8_t abc[] = {'a', 'b', 'c'};
    // act
    const std::uint64_t empty = albor::hash_bytes({});
    const std::uint64_t hashed = albor::hash_bytes(abc);
    // assert
    EXPECT_EQ(empty, 14695981039346656037ULL);
    EXPECT_EQ(hashed, 0xE71FA2190541574BULL);
}

TEST(ContentHashChecks, TheFontWrapperSharesTheContentHash) {
    // arrange
    const std::uint8_t bytes[] = {1, 2, 3, 4};
    // act
    const std::uint64_t wrapped = albor::resources::hash_font_bytes(bytes, sizeof bytes);
    const std::uint64_t shared = albor::hash_bytes(bytes);
    // assert
    EXPECT_EQ(wrapped, shared);
    EXPECT_EQ(albor::resources::hash_font_bytes(nullptr, 0), 0u);
}
