#include "prototypes/d2/src/index.h"

#include <algorithm>
#include <utility>

namespace albor::prototype_d2
{

    namespace
    {

        // give every segment this many entries on first use, so a graph of singletons stays dense (§4)
        constexpr std::uint32_t INITIAL_SEGMENT_CAPACITY = 2;

        // allocate the arena in blocks so a growing segment never moves another segment (§4)
        constexpr std::size_t SEGMENT_BLOCK_ENTRIES = 4096;

    } // namespace

    void SealedCsrIndex::declare(BindingId binding, std::span<const PropertyId> dependencies) {
        // remember one pair per declared dependency so a rebuild can bucket them again (§4)
        for (const PropertyId dependency : dependencies) {
            m_entries.emplace_back(dependency, binding);
        }
    }

    void SealedCsrIndex::finish(std::size_t property_count) {
        // remember the property count so a rebuild knows how many buckets to build (§4)
        m_property_count = property_count;

        // build the buckets now that every property id is known (§13.3)
        rebuild();
    }

    MutationCost SealedCsrIndex::rebuild() {
        // report the cost of this rebuild (§4 T4.4)
        MutationCost cost;
        cost.rebuild_entries = m_entries.size();

        // allocate one offset per property plus the terminating entry (§13.3)
        m_offsets.assign(m_property_count + 1, 0);

        // count the entries that read each property (§13.3)
        for (const auto& [property, binding] : m_entries) {
            static_cast<void>(binding);
            m_offsets.at(property + 1)++;
        }

        // turn the counts into a prefix sum, which is the CSR offset array (§13.3)
        for (std::size_t property = 0; property + 1 < m_offsets.size(); property++) {
            m_offsets.at(property + 1) += m_offsets.at(property);
        }

        // fill the flat binding array in property order (§13.3)
        m_bindings.assign(m_entries.size(), INVALID_BINDING);

        // place every entry at its running offset (§13.3)
        for (const auto& [property, binding] : m_entries) {
            // resolve the slot this property owns (§13.3)
            const std::uint32_t slot = m_offsets.at(property);

            // write the binding and open the next slot (§13.3)
            m_bindings.at(slot) = binding;

            // advance past the entry that was just written
            m_offsets.at(property) = slot + 1;
        }

        // the offsets were advanced while filling, so rebuild them as a prefix sum again (§13.3)
        std::fill(m_offsets.begin(), m_offsets.end(), 0);

        for (const auto& [property, binding] : m_entries) {
            static_cast<void>(binding);
            m_offsets.at(property + 1)++;
        }

        for (std::size_t property = 0; property + 1 < m_offsets.size(); property++) {
            m_offsets.at(property + 1) += m_offsets.at(property);
        }

        // the buckets are current again (§4)
        m_stale = false;

        // hand back the measured rebuild cost
        return cost;
    }

    MutationCost SealedCsrIndex::add_entry(BindingId binding, PropertyId property) {
        // remember the new pair so a rebuild can bucket it (§4)
        m_entries.emplace_back(property, binding);

        // mark the built buckets stale, because they no longer match the declared pairs (§4)
        m_stale = true;

        // rebuild every bucket, which is the cost this design pays for a local edit (§4 T4.4)
        return rebuild();
    }

    MutationCost SealedCsrIndex::remove_entry(BindingId binding, PropertyId property) {
        // find the declared pair this edit removes (§4)
        const auto found = std::ranges::find_if(m_entries, [binding, property](const auto& entry) { return entry.first == property && entry.second == binding; });

        // report nothing when the pair was never declared (§4)
        if (found == m_entries.end()) {
            return MutationCost{};
        }

        // drop the declared pair (§4)
        m_entries.erase(found);

        // mark the built buckets stale, because they no longer match the declared pairs (§4)
        m_stale = true;

        // rebuild every bucket, which is the cost this design pays for a local edit (§4 T4.4)
        return rebuild();
    }

    std::span<const BindingId> SealedCsrIndex::dependents(PropertyId property) const {
        // read the CSR range that belongs to the property (§13.3)
        const std::uint32_t begin = m_offsets.at(property);
        const std::uint32_t end = m_offsets.at(property + 1);

        // return the flat slice of binding ids (§13.3)
        return std::span<const BindingId>(m_bindings.data() + begin, end - begin);
    }

    std::size_t SealedCsrIndex::index_bytes() const {
        // charge the offsets and the flat binding array (§28.2)
        return m_offsets.size() * sizeof(std::uint32_t) + m_bindings.size() * sizeof(BindingId);
    }

    void SegmentedIndex::declare(BindingId binding, std::span<const PropertyId> dependencies) {
        // a declaration appends into the segment of each dependency (§13.3)
        for (const PropertyId dependency : dependencies) {
            add_entry(binding, dependency);
        }
    }

    void SegmentedIndex::finish(std::size_t property_count) {
        // a segment table has to cover every property before a query can be answered (§4)
        if (m_segments.size() < property_count) {
            m_segments.resize(property_count);
        }

        // nothing else is built, because segments were filled while the bindings were declared (§4)
    }

    BindingId* SegmentedIndex::entry_at(std::uint32_t offset) {
        // the arena is a list of fixed size blocks, so a global offset addresses a block and an index within it (§4)
        return m_chunks.at(offset / SEGMENT_BLOCK_ENTRIES).get() + (offset % SEGMENT_BLOCK_ENTRIES);
    }

    const BindingId* SegmentedIndex::entry_at(std::uint32_t offset) const {
        // the const form of the same addressing (§4)
        return m_chunks.at(offset / SEGMENT_BLOCK_ENTRIES).get() + (offset % SEGMENT_BLOCK_ENTRIES);
    }

    void SegmentedIndex::reserve_arena() {
        // allocate another block of segment entries so growth does not move other segments (§4)
        m_chunks.push_back(std::make_unique<BindingId[]>(SEGMENT_BLOCK_ENTRIES));

        // count the allocation so the memory measurement can charge it (§28.2)
        m_chunk_allocations++;

        // continue from the start of the new block
        m_bump = 0;
    }

    void SegmentedIndex::grow_segment(PropertyId property, std::uint32_t needed) {
        // resolve the segment that has to hold the entries (§4)
        Segment& segment = m_segments.at(property);

        // allocate a new block when the current one cannot hold the doubled capacity (§4)
        if (m_chunks.empty() || (m_bump + needed) > SEGMENT_BLOCK_ENTRIES) {
            reserve_arena();
        }

        // double the capacity so repeated growth stays amortised (§4)
        const std::uint32_t capacity = std::max(needed, segment.capacity == 0 ? INITIAL_SEGMENT_CAPACITY : segment.capacity * 2);

        // remember where the new run starts inside the arena (§4)
        const std::uint32_t start = static_cast<std::uint32_t>(m_bump);
        m_bump += capacity;

        // copy the live entries into the larger run (§4)
        for (std::uint32_t index = 0; index < segment.count; index++) {
            *entry_at(start + index) = *entry_at(segment.start + index);
        }

        // count the relocation so growth is separable from the entry cost (§28.2)
        m_relocations++;

        // publish the new run and leave the old slack behind (§4)
        segment.start = start;
        segment.capacity = capacity;
    }

    MutationCost SegmentedIndex::add_entry(BindingId binding, PropertyId property) {
        // make sure the segment table covers the property (§4)
        if (property >= m_segments.size()) {
            m_segments.resize(static_cast<std::size_t>(property) + 1);
        }

        // resolve the segment that receives the entry (§4)
        Segment& segment = m_segments.at(property);

        // grow the segment when it is full (§4 T4.1)
        if (segment.count == segment.capacity) {
            grow_segment(property, segment.capacity == 0 ? INITIAL_SEGMENT_CAPACITY : segment.capacity * 2);
        }

        // append the binding at the end of the segment (§13.3)
        *entry_at(segment.start + segment.count) = binding;

        // count the entry that is now live (§4)
        segment.count++;
        m_live++;

        // report that this entry cost no rebuild and no relocation (§4 T4.1)
        return MutationCost{};
    }

    MutationCost SegmentedIndex::remove_entry(BindingId binding, PropertyId property) {
        // report the cost of this removal (§4 T4.2)
        MutationCost cost;

        // a property with no segment never held the entry
        if (property >= m_segments.size()) {
            return cost;
        }

        // resolve the segment that may hold the entry (§4)
        Segment& segment = m_segments.at(property);

        // find the entry inside the segment (§4)
        BindingId* const base = entry_at(segment.start);

        for (std::uint32_t index = 0; index < segment.count; index++) {
            // skip the entries that name a different binding (§4)
            if (base[index] != binding) {
                continue;
            }

            // shift the tail of the segment down over the removed entry (§4)
            for (std::uint32_t tail = index + 1; tail < segment.count; tail++) {
                base[tail - 1] = base[tail];
            }

            // shrink the segment by one entry (§4)
            segment.count--;
            m_live--;

            // count the shifted entries so the removal cost is measurable (§28.2)
            cost.shifted_entries += segment.count - index;

            // hand back the measured cost of this removal
            return cost;
        }

        // report that the entry was not present (§4)
        return cost;
    }

    std::span<const BindingId> SegmentedIndex::dependents(PropertyId property) const {
        // a property outside the segment table has no dependents (§4)
        if (property >= m_segments.size()) {
            return {};
        }

        // resolve the segment that holds the dependents (§4)
        const Segment& segment = m_segments.at(property);

        // an unused segment has no storage to read (§4)
        if (segment.count == 0) {
            return {};
        }

        // return the contiguous run of binding ids (§13.3)
        return std::span<const BindingId>(entry_at(segment.start), segment.count);
    }

    std::size_t SegmentedIndex::slack_entries() const {
        // accumulate the capacity that holds no live entry (§4)
        std::size_t slack = 0;

        // walk every segment (§4)
        for (const Segment& segment : m_segments) {
            // count the unused capacity of this segment (§4)
            slack += segment.capacity - segment.count;
        }

        // report the growth slack the index never gives back (§4)
        return slack;
    }

    std::size_t SegmentedIndex::index_bytes() const {
        // charge the segment table and the arena blocks the segments occupy (§28.2)
        return m_segments.size() * sizeof(Segment) + m_chunks.size() * SEGMENT_BLOCK_ENTRIES * sizeof(BindingId);
    }

} // namespace albor::prototype_d2
