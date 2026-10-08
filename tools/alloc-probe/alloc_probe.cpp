#include "tools/alloc-probe/alloc_probe.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace albor::probe
{
    namespace
    {
        // whether the probe counts, which stays off so an ordinary test pays one relaxed load (§2.2)
        std::atomic<bool> g_counting{false};

        // how many allocations happened while counting, which a test reads through count() (§2.2)
        std::atomic<std::size_t> g_count{0};

        // how many of those are still live, which is what a leak check reads (§2.2)
        std::atomic<std::size_t> g_live{0};
    } // namespace

    void set_counting(bool counting) {
        // the flag is stored relaxed, because it only decides whether the counters move (§2.2)
        g_counting.store(counting, std::memory_order_relaxed);
    }

    bool counting() {
        // the flag is read relaxed, because a test reads it after it wrote it (§2.2)
        return g_counting.load(std::memory_order_relaxed);
    }

    void reset() {
        // both counters start again, so a test measures only the work it wrapped (§2.2)
        g_count.store(0, std::memory_order_relaxed);
        // the live count starts again too, because a leak check reads it from the same reset (§2.2)
        g_live.store(0, std::memory_order_relaxed);
    }

    std::size_t count() {
        // the count is read relaxed, because it only moves while counting (§2.2)
        return g_count.load(std::memory_order_relaxed);
    }

    std::size_t live() {
        // the live count is read relaxed, for the same reason (§2.2)
        return g_live.load(std::memory_order_relaxed);
    }

    namespace
    {
        // note one allocation, which is the whole cost of counting when the probe is off (§2.2)
        void note_allocation() {
            // the flag is checked first, so an ordinary test never touches the counters (§2.2)
            if (!g_counting.load(std::memory_order_relaxed))
                return;
            // the allocation is counted, which is what a steady state frame test reads (§2.2)
            g_count.fetch_add(1, std::memory_order_relaxed);
            // a fresh block is live until it is deleted (§2.2)
            g_live.fetch_add(1, std::memory_order_relaxed);
        }

        // note one deallocation, which is what a leak check subtracts from the live count (§2.2)
        void note_deallocation() {
            // the flag is checked first, so an ordinary test never touches the counters (§2.2)
            if (!g_counting.load(std::memory_order_relaxed))
                return;
            // a freed block stops being live, because the leak check compares the two counters (§2.2)
            g_live.fetch_sub(1, std::memory_order_relaxed);
        }
    } // namespace
} // namespace albor::probe

// the operators below are linked only by the test binary, so a shipping build keeps the compiler ones (AD-04, §2.3)
void* operator new(std::size_t size) {
    // the block is asked for first, because the probe only counts what the allocator actually handed out (§2.2)
    void* block = std::malloc(size == 0 ? 1 : size);
    // an allocation that failed throws, which is what the language requires of the throwing form (§2.2)
    if (block == nullptr)
        throw std::bad_alloc();
    // the allocation is counted on the way out, so a test sees the cost of the work it wrapped (§2.2)
    albor::probe::note_allocation();
    return block;
}

void* operator new[](std::size_t size) {
    // the array form hands out one block, because the array form has no other job (§2.2)
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    // the nothrow form reports a failure rather than throwing (§2.2)
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    // the array form matches the scalar one, because arrays need no separate counting (§2.2)
    return ::operator new(size, std::nothrow);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    // the aligned form asks for an aligned block, because an over aligned type needs one (§2.2)
    void* block = std::aligned_alloc(static_cast<std::size_t>(alignment), ((size + static_cast<std::size_t>(alignment) - 1) / static_cast<std::size_t>(alignment)) * static_cast<std::size_t>(alignment));
    // an allocation that failed throws, as the throwing aligned form requires (§2.2)
    if (block == nullptr)
        throw std::bad_alloc();
    // the allocation is counted on the way out, like every other form (§2.2)
    albor::probe::note_allocation();
    return block;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    // the array form hands out one aligned block, because that is the whole difference (§2.2)
    return ::operator new(size, alignment);
}

void operator delete(void* block) noexcept {
    // a delete of nothing is a no operation, so there is nothing to count (§2.2)
    if (block == nullptr)
        return;
    // the block stops being live before it goes back, so a leak check reads a settled number (§2.2)
    albor::probe::note_deallocation();
    // the block returns to the allocator it came from, because the probe did not replace it (§2.2)
    std::free(block);
}

void operator delete[](void* block) noexcept {
    // the array form frees one block, because the array form allocated one (§2.2)
    ::operator delete(block);
}

void operator delete(void* block, std::size_t) noexcept {
    // the sized form frees exactly what the unsized form would (§2.2)
    ::operator delete(block);
}

void operator delete[](void* block, std::size_t) noexcept {
    // the sized array form frees one block, like the unsized array form (§2.2)
    ::operator delete(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept {
    // the nothrow form frees exactly what the plain form would (§2.2)
    ::operator delete(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept {
    // the nothrow array form frees one block, like the plain array form (§2.2)
    ::operator delete(block);
}

void operator delete(void* block, std::align_val_t) noexcept {
    // the aligned form returns a block that aligned_alloc handed out, so plain free is correct (§2.2)
    if (block == nullptr)
        return;
    // the block stops being live before it goes back, because the probe counted it (§2.2)
    albor::probe::note_deallocation();
    // the block returns to the allocator it came from, which is the same one either way (§2.2)
    std::free(block);
}

void operator delete[](void* block, std::align_val_t) noexcept {
    // the aligned array form frees one aligned block (§2.2)
    ::operator delete(block, std::align_val_t{1});
}
