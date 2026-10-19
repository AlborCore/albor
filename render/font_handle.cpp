#include "render/font_handle.h"

#include <atomic>

namespace albor
{
    namespace
    {
        // the one counter every handle reads its era from, which outlives every engine and every surface (§6.4, AD-06)
        std::atomic<std::uint64_t>& era_counter() {
            // the counter is a function local, so no static initialisation order decides what a cache keys on (§6.4)
            static std::atomic<std::uint64_t> counter{0u};
            // the shared counter is what makes two eras comparable across threads (§6.4, §10.5)
            return counter;
        }
    } // namespace

    FontHandle FontHandle::from_object(const void* object) {
        // wrap the typeface in a handle, which is the only form a shaped run names it by (§9.1)
        FontHandle handle;
        handle.m_object = object;
        return handle;
    }

    std::uint64_t FontHandle::era() {
        // the era is read relaxed, because it orders cache keys rather than synchronises threads (§6.4, §10.5)
        return era_counter().load(std::memory_order_relaxed);
    }

    void FontHandle::drop_era() {
        // the era is dropped forward, which makes every key of the era before miss from here on (§6.4, AD-06)
        era_counter().fetch_add(1u, std::memory_order_relaxed);
    }

    bool FontHandle::valid() const {
        // a handle with no typeface names nothing, so a renderer skips the run rather than guessing a font (§9.1)
        return m_object != nullptr;
    }

    const void* FontHandle::object() const {
        // the typeface leaves the handle as an opaque pointer, which no shaping type may name (§3.5, §9.1)
        return m_object;
    }
} // namespace albor
