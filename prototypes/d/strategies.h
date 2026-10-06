#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "prototypes/d/property_id.h"
#include "prototypes/d/strategy.h"
#include "prototypes/d/strategy_common.h"

namespace albor::prototype_d
{

    constexpr std::uint32_t INVALID_NODE = static_cast<std::uint32_t>(-1);

    // keep one flat dependent list per property as the smallest index (§13.3)
    class CompactRecordsStrategy final : public InvalidationStrategy
    {
    public:
        [[nodiscard]] std::string_view name() const override { return "compact_records"; }

        void declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) override;
        void finish(std::size_t property_count) override;
        void invalidate(PropertyId changed) override;
        void plan(std::vector<BindingId>& out) override;

        [[nodiscard]] std::size_t index_bytes() const override;
        [[nodiscard]] std::size_t last_candidate_count() const override { return m_last_candidate_count; }
        [[nodiscard]] std::size_t remembered_dependency_count() const override { return m_index.dependency_count(); }

    private:
        DependentsIndex m_index;
        StampedBindingSet m_stamps;
        StampedBindingSet m_pending_stamps;
        std::vector<PropertyId> m_pending;
        std::size_t m_binding_count = 0;
        std::size_t m_last_candidate_count = 0;
    };

    // order a dirty property bitset with kahn topological ordering (§13.3)

    // share one dependency node between bindings that read the same properties (§13.3)
    class SharedNodesStrategy final : public InvalidationStrategy
    {
    public:
        [[nodiscard]] std::string_view name() const override { return "shared_dep_nodes"; }

        void declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) override;
        void finish(std::size_t property_count) override;
        void invalidate(PropertyId changed) override;
        void plan(std::vector<BindingId>& out) override;

        [[nodiscard]] std::size_t index_bytes() const override;
        [[nodiscard]] std::size_t last_candidate_count() const override { return m_last_candidate_count; }
        [[nodiscard]] std::size_t remembered_dependency_count() const override { return m_shared_dependencies + m_node_entries; }

        // count the distinct nodes the declarations collapsed into
        [[nodiscard]] std::size_t node_count() const { return m_nodes.size(); }

    private:
        std::vector<std::vector<BindingId>> m_nodes;
        std::vector<std::vector<PropertyId>> m_node_dependencies;
        std::vector<std::vector<std::uint32_t>> m_property_nodes;
        std::vector<std::vector<PropertyId>> m_declared;
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_interned;
        StampedBindingSet m_stamps;
        StampedBindingSet m_pending_stamps;
        std::vector<PropertyId> m_pending;
        std::size_t m_shared_dependencies = 0;
        std::size_t m_node_entries = 0;
        std::size_t m_last_candidate_count = 0;
    };

} // namespace albor::prototype_d
