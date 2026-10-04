#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "prototypes/d2/src/schema.h"

namespace albor::prototype_d2
{

    // identify a binding by a dense index into the binding table
    using BindingId = std::uint32_t;

    constexpr BindingId INVALID_BINDING = static_cast<BindingId>(-1);

    // report what a mutation did to the index, so the cost can be attributed (§28.2)
    struct MutationCost
    {
        std::size_t rebuild_entries = 0;
        std::size_t segment_relocations = 0;
        std::size_t shifted_entries = 0;
    };

    // index the bindings that read a property in one flat array, built once and never changed (AD-04a)
    //
    // this is Prototype D's representation. It is the cheapest index to query and the most expensive to change,
    // because any mutation has to rebuild every bucket from the declared pairs (§4 T4.4)
    class SealedCsrIndex
    {
    public:
        void declare(BindingId binding, std::span<const PropertyId> dependencies);
        void finish(std::size_t property_count);

        // remember that a mutation invalidated the built buckets, so a query reports the staleness (§4)
        void mark_stale() { m_stale = true; }

        // rebuild every bucket from the declared pairs and report the cost (§4 T4.4)
        MutationCost rebuild();

        // a local edit request, which this design can only answer by rebuilding every bucket (§4 T4.4)
        MutationCost add_entry(BindingId binding, PropertyId property);
        MutationCost remove_entry(BindingId binding, PropertyId property);

        [[nodiscard]] std::span<const BindingId> dependents(PropertyId property) const;
        [[nodiscard]] std::size_t index_bytes() const;

        // report no growth slack, because every bucket is rebuilt from the declared pairs (§4)
        [[nodiscard]] std::size_t slack_entries() const { return 0; }

        [[nodiscard]] bool stale() const { return m_stale; }
        [[nodiscard]] std::size_t dependency_count() const { return m_entries.size(); }

        [[nodiscard]] static std::string_view name() { return "sealed_csr"; }

    private:
        std::vector<std::pair<PropertyId, BindingId>> m_entries;
        std::vector<std::uint32_t> m_offsets;
        std::vector<BindingId> m_bindings;
        std::size_t m_property_count = 0;
        bool m_stale = false;
    };

    // index the bindings that read a property in one growable segment per property (AD-04a, §4)
    //
    // the segment table costs more per property than the CSR offsets and never releases the slack that growth
    // leaves behind, which is the price of being able to add and remove a dependency without rebuilding. Removal
    // shifts the tail of one segment instead of writing a tombstone, so a query never filters and the shift is
    // bounded by the segment length rather than by the index size.
    class SegmentedIndex
    {
    public:
        void declare(BindingId binding, std::span<const PropertyId> dependencies);

        // nothing to build, because every segment is filled as its binding is declared (§4)
        void finish(std::size_t property_count);

        // append one dependency entry to the segment of its property, growing the segment if needed (§4 T4.1)
        MutationCost add_entry(BindingId binding, PropertyId property);

        // remove one dependency entry from the segment of its property, shifting the tail (§4 T4.2)
        MutationCost remove_entry(BindingId binding, PropertyId property);

        [[nodiscard]] std::span<const BindingId> dependents(PropertyId property) const;
        [[nodiscard]] std::size_t index_bytes() const;

        // report the capacity the segments hold beyond their live entries, which is the growth slack (§4)
        [[nodiscard]] std::size_t slack_entries() const;
        [[nodiscard]] std::size_t live_entries() const { return m_live; }
        [[nodiscard]] std::size_t segment_count() const { return m_segments.size(); }

        // report the entries the index holds, which is what a shared visitor reads (§4)
        [[nodiscard]] std::size_t dependency_count() const { return m_live; }

        // report how many segment relocations happened, to separate growth cost from entry cost (§28.2)
        [[nodiscard]] std::size_t relocations() const { return m_relocations; }
        [[nodiscard]] std::size_t chunk_count() const { return m_chunks.size(); }

        [[nodiscard]] static std::string_view name() { return "segmented"; }

    private:
        struct Segment
        {
            std::uint32_t start = 0;
            std::uint32_t capacity = 0;
            std::uint32_t count = 0;
        };

        void grow_segment(PropertyId property, std::uint32_t needed);
        void reserve_arena();

        std::vector<Segment> m_segments;
        std::vector<std::unique_ptr<BindingId[]>> m_chunks;
        std::size_t m_bump = 0;
        std::size_t m_live = 0;
        std::size_t m_relocations = 0;
        std::size_t m_chunk_allocations = 0;
    };

} // namespace albor::prototype_d2