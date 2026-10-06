#include "prototypes/d/strategies.h"

#include <algorithm>
#include <ranges>

namespace albor::prototype_d
{

    void CompactRecordsStrategy::declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) {
        // the flat index is the only structure this strategy needs
        static_cast<void>(target);

        // register every dependency of the binding
        m_index.declare(dependencies, binding);

        // remember how many bindings exist so the stamp set can be sized
        m_binding_count = std::max(m_binding_count, static_cast<std::size_t>(binding) + 1);
    }

    void CompactRecordsStrategy::finish(std::size_t property_count) {
        // bucket the declared dependencies by property
        m_index.finish(property_count);

        // allocate the deduplication stamps
        m_stamps.reset(m_binding_count);
        m_pending_stamps.reset(property_count);

        // reserve room for the changed properties of one pass
        m_pending.reserve(64);
    }

    void CompactRecordsStrategy::invalidate(PropertyId changed) {
        // remember each changed property once per pass to skip a repeated scan
        if (m_pending_stamps.mark(changed)) {
            m_pending.push_back(changed);
        }
    }

    void CompactRecordsStrategy::plan(std::vector<BindingId>& out) {
        // start a new deduplication epoch
        m_stamps.begin_pass();
        m_pending_stamps.begin_pass();

        // count every dependent visited, including the duplicates
        m_last_candidate_count = 0;

        // walk the dependents of every property that changed in this pass
        for (const PropertyId property : m_pending) {
            // visit each binding that reads the changed property
            for (const BindingId binding : m_index.dependents(property)) {
                m_last_candidate_count++;

                // deduplicate a binding reached by several changed properties
                if (m_stamps.mark(binding)) {
                    out.push_back(binding);
                }
            }
        }

        // forget the changed properties for the next pass
        m_pending.clear();
    }

    std::size_t CompactRecordsStrategy::index_bytes() const {
        // charge the index, the stamps and the pending property vector
        return m_index.bytes() + m_stamps.bytes() + m_pending_stamps.bytes() + m_pending.capacity() * sizeof(PropertyId);
    }

    void SharedNodesStrategy::declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) {
        // the node index is derived from the dependency sets alone
        static_cast<void>(target);

        // sort the declared set so that equal inputs intern to the same node
        std::vector<PropertyId> sorted(dependencies.begin(), dependencies.end());
        std::ranges::sort(sorted);

        // grow the declaration table up to this binding
        m_declared.resize(static_cast<std::size_t>(binding) + 1);

        // keep the sorted dependency set for the interning pass
        m_declared.at(binding) = std::move(sorted);
    }

    void SharedNodesStrategy::finish(std::size_t property_count) {
        // allocate the node list of every property
        m_property_nodes.assign(property_count, {});

        // allocate the deduplication stamps
        m_stamps.reset(m_declared.size());
        m_pending_stamps.reset(property_count);

        // intern the dependency set of every binding
        for (const BindingId binding : std::views::iota(BindingId{0}, static_cast<BindingId>(m_declared.size()))) {
            // read the sorted dependency set of this binding
            const std::vector<PropertyId>& dependencies = m_declared.at(binding);

            // start the hash with the fnv offset basis
            std::uint64_t hash = 1469598103934665603ULL;

            // fold every dependency into the hash
            for (const PropertyId dependency : dependencies) {
                hash ^= dependency;
                hash *= 1099511628211ULL;
            }

            // assume the dependency set is unique until a match is found
            std::uint32_t node = INVALID_NODE;

            // look up the bucket that may hold the node
            const auto bucket = m_interned.find(hash);

            // compare the candidates of the bucket against the declared set
            if (bucket != m_interned.end()) {
                for (const std::uint32_t candidate : bucket->second) {
                    // reuse the node that declares exactly the same inputs
                    if (m_node_dependencies.at(candidate) == dependencies) {
                        node = candidate;
                        break;
                    }
                }
            }

            // create a node when no declared set matched
            if (node == INVALID_NODE) {
                node = static_cast<std::uint32_t>(m_nodes.size());
                m_nodes.emplace_back();
                m_node_dependencies.push_back(dependencies);
                m_interned[hash].push_back(node);

                // charge the shared dependencies only once per node
                m_shared_dependencies += dependencies.size();
            }

            // join the binding to the node of its dependency set
            m_nodes.at(node).push_back(binding);

            // count the binding that joined a node
            m_node_entries++;

            // link the node from every property it reads
            for (const PropertyId dependency : dependencies) {
                // resolve the node list of this property
                std::vector<std::uint32_t>& nodes = m_property_nodes.at(dependency);

                // add the node only when the property does not reach it yet
                if (std::ranges::find(nodes, node) == nodes.end()) {
                    nodes.push_back(node);
                }
            }
        }
    }

    void SharedNodesStrategy::invalidate(PropertyId changed) {
        // remember each changed property once per pass to skip a repeated scan
        if (m_pending_stamps.mark(changed)) {
            m_pending.push_back(changed);
        }
    }

    void SharedNodesStrategy::plan(std::vector<BindingId>& out) {
        // start a new deduplication epoch
        m_stamps.begin_pass();
        m_pending_stamps.begin_pass();

        // count every binding visited through the shared nodes
        m_last_candidate_count = 0;

        // walk the nodes of every property that changed in this pass
        for (const PropertyId property : m_pending) {
            // visit every shared node that reads the changed property
            for (const std::uint32_t node : m_property_nodes.at(property)) {
                // visit every binding that joined the node
                for (const BindingId binding : m_nodes.at(node)) {
                    m_last_candidate_count++;

                    // emit the binding the first time it is reached
                    if (m_stamps.mark(binding)) {
                        out.push_back(binding);
                    }
                }
            }
        }

        // forget the changed properties for the next pass
        m_pending.clear();
    }

    std::size_t SharedNodesStrategy::index_bytes() const {
        // charge the stamps and the pending property vector
        std::size_t total = m_stamps.bytes() + m_pending_stamps.bytes() + m_pending.capacity() * sizeof(PropertyId);

        // charge the binding list of every shared node
        for (const std::vector<BindingId>& node : m_nodes) {
            total += node.capacity() * sizeof(BindingId);
        }

        // charge the node list of every property, including the vector headers
        for (const std::vector<std::uint32_t>& nodes : m_property_nodes) {
            total += nodes.capacity() * sizeof(std::uint32_t) + sizeof(std::vector<std::uint32_t>);
        }

        // report every byte the strategy keeps in memory
        return total;
    }

} // namespace albor::prototype_d
