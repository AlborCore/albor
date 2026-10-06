#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "prototypes/d/property_id.h"
#include "prototypes/d/strategy.h"
#include "prototypes/d/strategy_common.h"

namespace albor::prototype_d
{

    // order a dirty property bitset with kahn topological ordering (§13.3)
    // this unit is linked only in the dev and test configurations, because release does not ship the ordering (AD-04)
    class DirtyBitsetStrategy final : public InvalidationStrategy
    {
    public:
        [[nodiscard]] std::string_view name() const override { return "dirty_bitset_topo"; }

        void declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) override;
        void finish(std::size_t property_count) override;
        void invalidate(PropertyId changed) override;
        void plan(std::vector<BindingId>& out) override;

        [[nodiscard]] std::size_t index_bytes() const override;
        [[nodiscard]] std::size_t last_candidate_count() const override { return m_last_candidate_count; }
        [[nodiscard]] std::size_t remembered_dependency_count() const override { return m_index.dependency_count() + m_binding_deps.size() + m_binding_dependents.size(); }

        // count the candidates the last plan could not order to expose cycles (§13.3)
        [[nodiscard]] std::size_t last_cycle_count() const { return m_cycle_count; }

    private:
        // collect the bindings that read a dirty property and their blocking edges
        void collect_candidates();

        // release the consumers of a binding that is now ordered
        void release_consumers(BindingId binding, std::vector<BindingId>& ready);

        void clear_dirty();

        DependentsIndex m_index;
        std::vector<PropertyId> m_binding_targets;
        std::vector<std::uint32_t> m_binding_dep_offsets;
        std::vector<PropertyId> m_binding_deps;
        std::vector<std::uint32_t> m_binding_edge_offsets;
        std::vector<BindingId> m_binding_dependents;
        std::vector<std::uint64_t> m_property_dirty;
        std::vector<std::uint32_t> m_pending_deps;
        std::vector<std::uint8_t> m_binding_dirty;
        std::vector<BindingId> m_candidates;
        std::vector<BindingId> m_ready;
        std::size_t m_word_count = 0;
        std::size_t m_last_candidate_count = 0;
        std::size_t m_cycle_count = 0;
    };

} // namespace albor::prototype_d
