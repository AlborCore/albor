#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace albor
{
    // a map of values aged by frame, whose size is the capacity plus the working set of one frame (§6.4, AD-06)
    template <typename Key, typename Value, typename U = std::hash<Key>>
    class AgedCache
    {
    public:
        // build a cache that keeps a value for the idle limit and targets the capacity it is given (§6.4, AD-06)
        explicit AgedCache(std::uint64_t idle_frames, std::size_t capacity) :
            m_idle_frames(idle_frames),
            // a cache of no capacity would drop every value it stores, so it holds at least one (§6.4)
            m_capacity(std::max<std::size_t>(capacity, 1u)) {}

        // hand out the value a key was stored under, or null when nothing was, which ages a hit (§6.4)
        [[nodiscard]] Value* find(const Key& key) {
            // the entry is looked up by the exact identity the value was stored under (§6.4)
            const auto found = m_values.find(key);
            // a key nothing was stored under answers nothing, which is the miss a caller builds for (§6.4)
            if (found == m_values.end())
                return nullptr;
            // the hit ages its entry to this frame, which is what keeps it from the sweep (§6.4, AD-06)
            found->second.last_used = m_frame;
            // the value is handed back through std::addressof, since a smart pointer overloads operator& (§6.4)
            return std::addressof(found->second.value);
        }

        // hand out the value a key was stored under without aging it, or null when nothing was (§6.4)
        [[nodiscard]] const Value* peek(const Key& key) const {
            // the entry is looked up by the same identity a find reads, and a miss answers nothing (§6.4)
            const auto found = m_values.find(key);
            // the value is handed back through std::addressof, because a smart pointer overloads its own operator& (§6.4)
            return found == m_values.end() ? nullptr : std::addressof(found->second.value);
        }

        // age the entry a key is stored under without reading it, which is what a use with no lookup is (§6.4)
        void touch(const Key& key) {
            // the entry is looked up by its identity, and one nothing was stored under is passed over (§6.4)
            const auto found = m_values.find(key);
            // a hit ages to this frame, which keeps it from the sweep the next one runs (§6.4, AD-06)
            if (found != m_values.end())
                found->second.last_used = m_frame;
        }

        // keep a value under its identity, dropping the coldest slice when the cache is already full (§6.4, AD-06)
        void store(const Key& key, Value value) {
            // a full cache drops a slice before it grows, which is what keeps its size near the capacity (§6.4, AD-06)
            if (m_values.size() >= m_capacity)
                drop_the_coldest_slice();
            // the value joins under its identity, aged to the frame that stored it (§6.4)
            m_values.insert_or_assign(key, Entry{.value = std::move(value), .last_used = m_frame});
        }

        // age the cache at a frame, which a consumer calls once per frame because it drives every eviction (§6.4, AD-06)
        void begin_frame(std::uint64_t frame) {
            // the frame is what every entry ages against, which the sweep and the eviction both read (§6.4, AD-06)
            m_frame = frame;
            // the sweep walks every entry, because a value no caller reached is memory that frame cannot use (§6.4)
            for (auto entry = m_values.begin(); entry != m_values.end();)
                // a fresh entry is passed over, and a stale one is erased here rather than later (§6.4, AD-06)
                entry = frame - entry->second.last_used <= m_idle_frames ? std::next(entry) : m_values.erase(entry);
        }

        // report how many values the cache holds, which a test asserts on (§6.4)
        [[nodiscard]] std::size_t count() const {
            // the count is what a test asserts on, because a cache is otherwise invisible (§6.4)
            return m_values.size();
        }

    private:
        // drop the coldest slice of the full cache, because dropping all of it loses every value of a frame (§6.4, AD-06)
        void drop_the_coldest_slice() {
            // every entry's age is gathered with its identity, which the coldest slice is chosen from (§6.4, AD-06)
            std::vector<std::pair<std::uint64_t, Key>> ages;
            for (const auto& [key, entry] : m_values) {
                // an entry reached in this frame is never a victim, because the frame is still drawing it (§6.4, AD-06)
                if (entry.last_used < m_frame)
                    ages.emplace_back(entry.last_used, key);
            }
            // a cache whose every entry came from this frame keeps them all, so the frame is never robbed (§6.4)
            if (ages.empty())
                return;
            // the slice is a fixed fraction, and never nothing, because a cache that drops nothing cannot grow (§6.4)
            const std::size_t slice = std::min(std::max<std::size_t>(m_capacity / 8u, 1u), ages.size());
            // the coldest slice sorts to the front, because the entries nothing reached soonest are the ones to go (§6.4)
            const auto coldest = ages.begin() + static_cast<std::ptrdiff_t>(slice);
            std::partial_sort(ages.begin(), coldest, ages.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
            // the slice is erased by identity, because a map hands out no window a caller could erase (§6.4)
            for (auto age = ages.begin(); age != coldest; ++age)
                m_values.erase(age->second);
        }

        // one value with the frame it was last reached in, which the sweep and the eviction both read (§6.4, AD-06)
        struct Entry
        {
            // the value the cache holds (§6.4)
            Value value{};

            // the frame the value was last found or stored in, which ages it (§6.4)
            std::uint64_t last_used = 0;
        };

        // the values keyed by the identity they were stored under (§6.4)
        std::unordered_map<Key, Entry, U> m_values;

        // the frame the cache is being aged at, which a hit or a store stamps an entry with (§6.4)
        std::uint64_t m_frame = 0;

        // how many frames an unreached value stays, which bounds the memory a long run holds (§6.4, AD-06)
        std::uint64_t m_idle_frames = 0;

        // how many values the cache aims to hold, which the working set of one frame may exceed (§6.4, AD-06)
        std::size_t m_capacity = 1;
    };
} // namespace albor
