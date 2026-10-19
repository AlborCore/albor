#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "render/aged_cache.h"

using namespace albor;

TEST(AgedCacheChecks, AnswersAValueItWasGiven) {
    // arrange an empty cache, which holds at most eight values for ten frames (§6.4, AD-06)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    // act nothing was ever stored under the key (§6.4)
    EXPECT_EQ(cache.find(3u), nullptr);
    // act one value is stored under it (§6.4)
    cache.store(3u, 42);
    // assert the value answers its own key, which is what a cache is for (§6.4)
    const int* found = cache.find(3u);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 42);
    EXPECT_EQ(cache.count(), 1u);
}

TEST(AgedCacheChecks, AnswersNothingForAnotherKey) {
    // arrange one stored value beside the key it must not answer (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    cache.store(3u, 42);
    // act another key is asked for, which the identity does not answer (§6.4)
    EXPECT_EQ(cache.find(4u), nullptr);
    // assert the cache still holds only the value it was given (§6.4)
    EXPECT_EQ(cache.count(), 1u);
}

TEST(AgedCacheChecks, ReplacesAValueUnderTheSameKey) {
    // arrange one stored value, which a later store under the same key overrides (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    cache.store(3u, 42);
    // act the same key is stored again with another value (§6.4)
    cache.store(3u, 43);
    // assert the cache holds one entry carrying the later value, because a key is one entry (§6.4)
    ASSERT_NE(cache.find(3u), nullptr);
    EXPECT_EQ(*cache.find(3u), 43);
    EXPECT_EQ(cache.count(), 1u);
}

TEST(AgedCacheChecks, DropsAValueNoReaderReached) {
    // arrange one stored value at a frame of its own (§6.4, AD-06)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    cache.begin_frame(5u);
    cache.store(3u, 42);
    // act the cache is aged to the limit of that store, which is the last frame the store alone survives (§6.4, AD-06)
    cache.begin_frame(15u);
    // assert the value is still answered, because a reader inside the limit must find it (§6.4)
    EXPECT_NE(cache.find(3u), nullptr);
    // act the cache is aged past the limit of that read, which is a frame nothing reached the value in (§6.4)
    cache.begin_frame(26u);
    // assert the value is gone, because a value no reader reached is memory a frame cannot use (§6.4, AD-06)
    EXPECT_EQ(cache.find(3u), nullptr);
    EXPECT_EQ(cache.count(), 0u);
}

TEST(AgedCacheChecks, KeepsAValueAReaderTouched) {
    // arrange one stored value, which a later frame touches without reading it (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    cache.begin_frame(5u);
    cache.store(3u, 42);
    // act the value is touched at the end of its idle limit, which is what a draw that binds it does (§6.4, AD-06)
    cache.begin_frame(15u);
    cache.touch(3u);
    // act the cache is aged another idle limit past that touch (§6.4)
    cache.begin_frame(25u);
    // assert the value survives, because the touch stamped it as reached again (§6.4, AD-06)
    EXPECT_NE(cache.find(3u), nullptr);
}

TEST(AgedCacheChecks, TouchesNothingItNeverHeld) {
    // arrange an empty cache, which a touch of an unknown key must not populate (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    // act a key nothing was stored under is touched (§6.4)
    cache.touch(3u);
    // assert the cache stays empty, because a touch ages an entry rather than making one (§6.4)
    EXPECT_EQ(cache.count(), 0u);
    EXPECT_EQ(cache.find(3u), nullptr);
}

TEST(AgedCacheChecks, ReadsWithoutAgeingTheValue) {
    // arrange one stored value, which a peek reads without stamping it (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 8u);
    cache.begin_frame(5u);
    cache.store(3u, 42);
    // act the value is peeked at the end of its idle limit, which is a read that is not a use (§6.4)
    cache.begin_frame(15u);
    const int* peeked = cache.peek(3u);
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(*peeked, 42);
    // act the cache is aged one frame past the limit of the store alone (§6.4, AD-06)
    cache.begin_frame(16u);
    // assert the value is gone, because a peek does not keep it alive and only a use does (§6.4, AD-06)
    EXPECT_EQ(cache.peek(3u), nullptr);
    EXPECT_EQ(cache.count(), 0u);
}

TEST(AgedCacheChecks, DropsTheColdestSliceWhenItFills) {
    // arrange a cache filled to its capacity of sixteen, with one entry read again at a later frame (§6.4, AD-06)
    AgedCache<std::uint32_t, int> cache(10u, 16u);
    cache.begin_frame(0u);
    for (std::uint32_t index = 0u; index < 16u; index++)
        cache.store(index, static_cast<int>(index));
    // act one entry is read again at a later frame, which is what makes it the warmest of them (§6.4)
    cache.begin_frame(5u);
    EXPECT_NE(cache.find(0u), nullptr);
    // act one more value arrives, which the full cache answers by dropping its coldest slice (§6.4)
    cache.store(999u, 999);
    // assert the slice is two of sixteen rather than all of them, because an evicted value is rebuilt later (§6.4, AD-06)
    EXPECT_EQ(cache.count(), 15u);
    // assert the warm entry survived, because the slice takes the coldest entries first (§6.4)
    EXPECT_NE(cache.find(0u), nullptr);
    // assert the new value is held, because the frame that stored it is the warmest there is (§6.4)
    EXPECT_NE(cache.find(999u), nullptr);
}

TEST(AgedCacheChecks, KeepsEveryValueOneFrameStoresPastItsCapacity) {
    // arrange a cache of capacity four, which a frame of six values exceeds (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 4u);
    cache.begin_frame(3u);
    // act one frame stores six values, which is more than the cache is meant to hold (§6.4, AD-06)
    for (std::uint32_t index = 0u; index < 6u; index++)
        cache.store(index, static_cast<int>(index));
    // assert every value of the frame is still held, because none may be taken from the frame drawing it (§6.4, AD-06)
    EXPECT_EQ(cache.count(), 6u);
    for (std::uint32_t index = 0u; index < 6u; index++)
        EXPECT_NE(cache.peek(index), nullptr);
    // act the next frame stores one more value, which is where the overfill is answered (§6.4, AD-06)
    cache.begin_frame(4u);
    cache.store(9u, 9);
    // assert one value of the earlier frame went, because a victim only comes from a frame that ended (§6.4, AD-06)
    EXPECT_EQ(cache.count(), 6u);
    EXPECT_NE(cache.peek(9u), nullptr);
}

TEST(AgedCacheChecks, DropsOnlyWhatItCanWhenFewerValuesAreCold) {
    // arrange a full cache whose next frame reads fifteen of its sixteen values (§6.4, AD-06)
    AgedCache<std::uint32_t, int> cache(10u, 16u);
    cache.begin_frame(0u);
    for (std::uint32_t index = 0u; index < 16u; index++)
        cache.store(index, static_cast<int>(index));
    cache.begin_frame(1u);
    for (std::uint32_t index = 0u; index < 15u; index++)
        EXPECT_NE(cache.find(index), nullptr);
    // act one more value arrives, which the full cache answers with the one value it may take (§6.4, AD-06)
    cache.store(99u, 99);
    // assert the single cold value went rather than a slice the cache does not have (§6.4)
    EXPECT_EQ(cache.peek(15u), nullptr);
    // assert the fifteen values this frame read are untouched, because the frame is still drawing them (§6.4, AD-06)
    for (std::uint32_t index = 0u; index < 15u; index++)
        EXPECT_NE(cache.peek(index), nullptr);
    EXPECT_NE(cache.peek(99u), nullptr);
    EXPECT_EQ(cache.count(), 16u);
}

TEST(AgedCacheChecks, KeepsOneValueWhenItIsGivenNoCapacity) {
    // arrange a cache of no capacity, which would otherwise drop every value it is given (§6.4)
    AgedCache<std::uint32_t, int> cache(10u, 0u);
    cache.begin_frame(1u);
    // act one value is stored, which is the most a cache of no capacity can hold (§6.4)
    cache.store(3u, 42);
    // assert the value is answered, because the capacity is raised to one rather than dropped to none (§6.4)
    ASSERT_NE(cache.find(3u), nullptr);
    EXPECT_EQ(*cache.find(3u), 42);
    EXPECT_EQ(cache.count(), 1u);
    // act a second value arrives at the next frame, which the full cache answers by replacing what it holds (§6.4)
    cache.begin_frame(2u);
    cache.store(4u, 43);
    // assert only the later value is held, because the capacity is one entry (§6.4, AD-06)
    EXPECT_EQ(cache.count(), 1u);
    EXPECT_EQ(cache.peek(3u), nullptr);
    EXPECT_NE(cache.peek(4u), nullptr);
}

TEST(AgedCacheChecks, AnswersAPointerToTheValueItHolds) {
    // arrange one stored shared value, which a caller reads through the pointer it is handed (§6.4)
    AgedCache<std::uint32_t, std::shared_ptr<const int>> cache(10u, 8u);
    const std::shared_ptr<const int> value = std::make_shared<const int>(42);
    cache.store(3u, value);
    // act the entry is read back through the pointer the cache hands out (§6.4)
    const std::shared_ptr<const int>* found = cache.find(3u);
    ASSERT_NE(found, nullptr);
    // assert the pointer refers to the stored value, which is what a caller without a copy reads (§6.4)
    EXPECT_EQ(*found, value);
}
