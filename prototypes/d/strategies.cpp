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

    void DirtyBitsetStrategy::declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) {
        // remember the declared dependencies to count the unresolved producers of a binding
        m_binding_dep_offsets.push_back(static_cast<std::uint32_t>(m_binding_deps.size()));

        // append the dependencies of this binding to the flat array
        m_binding_deps.insert(m_binding_deps.end(), dependencies.begin(), dependencies.end());

        // grow the target table up to this binding
        m_binding_targets.resize(static_cast<std::size_t>(binding) + 1, INVALID_PROPERTY);

        // remember the property the binding writes, which becomes its producer identity
        m_binding_targets.at(binding) = target;

        // register every dependency in the dependents index
        m_index.declare(dependencies, binding);
    }

    void DirtyBitsetStrategy::finish(std::size_t property_count) {
        // bucket the declared dependencies by property
        m_index.finish(property_count);

        // read the binding count from the target table
        const std::size_t binding_count = m_binding_targets.size();

        // close the dependency slice of the last binding
        m_binding_dep_offsets.push_back(static_cast<std::uint32_t>(m_binding_deps.size()));

        // derive how many words the dirty property bitset needs
        m_word_count = (property_count + 63) / 64;

        // allocate the pass state
        m_property_dirty.assign(m_word_count, 0);
        m_pending_deps.assign(binding_count, 0);
        m_binding_dirty.assign(binding_count, 0);

        // reserve the two scratch vectors used by the ordering pass
        m_ready.reserve(binding_count);
        m_candidates.reserve(binding_count);

        // collect the edges that order the pass
        std::vector<std::pair<BindingId, BindingId>> edges;

        // reserve one edge slot per declared dependency
        edges.reserve(m_binding_deps.size());

        // read the target of every binding
        for (const BindingId producer : std::views::iota(BindingId{0}, static_cast<BindingId>(binding_count))) {
            const PropertyId target = m_binding_targets.at(producer);

            // skip a binding that writes no property
            if (target == INVALID_PROPERTY) {
                continue;
            }

            // connect the producer to every binding that reads its target
            for (const BindingId consumer : m_index.dependents(target)) {
                // ignore a binding that would depend on itself
                if (consumer != producer) {
                    edges.emplace_back(producer, consumer);
                }
            }
        }

        // group the edges by producer so the scan stays contiguous
        std::ranges::sort(edges);

        // allocate the producer offsets of the edge table
        m_binding_edge_offsets.assign(binding_count + 1, 0);

        // count the outgoing edges of every producer into the shifted slot
        for (const auto& [producer, consumer] : edges) {
            // the consumer is not needed for counting
            static_cast<void>(consumer);

            // increase the count of this producer
            m_binding_edge_offsets.at(producer + 1)++;
        }

        // turn the counts into start offsets
        for (std::size_t index = 0; index < binding_count; index++) {
            m_binding_edge_offsets.at(index + 1) += m_binding_edge_offsets.at(index);
        }

        // allocate the consumer table of the edge index
        m_binding_dependents.assign(edges.size(), INVALID_BINDING);

        // copy the start offsets into a private scatter cursor
        std::vector<std::uint32_t> cursor(m_binding_edge_offsets.begin(), m_binding_edge_offsets.end() - 1);

        // scatter the consumers into their producer bucket
        for (const auto& [producer, consumer] : edges) {
            m_binding_dependents.at(cursor.at(producer)++) = consumer;
        }
    }

    void DirtyBitsetStrategy::invalidate(PropertyId changed) {
        // set the dirty bit as the only state kept per change
        m_property_dirty.at(changed / 64) |= std::uint64_t{1} << (changed % 64);
    }

    void DirtyBitsetStrategy::collect_candidates() {
        // start a fresh candidate set
        m_candidates.clear();

        // forget the dependent count of the previous pass
        m_last_candidate_count = 0;

        // scan the dirty bitset word by word
        for (std::size_t word = 0; word < m_word_count; word++) {
            // copy the dirty bits of this word so that the scan can consume them
            std::uint64_t bits = m_property_dirty.at(word);

            // visit only the properties that are dirty
            while (bits != 0) {
                // take the lowest dirty bit of the word
                const std::size_t bit = static_cast<std::size_t>(std::countr_zero(bits));

                // resolve the property id of that bit
                const PropertyId property = static_cast<PropertyId>(word * 64 + bit);

                // clear the visited bit
                bits &= bits - 1;

                // visit every binding that reads the dirty property
                for (const BindingId binding : m_index.dependents(property)) {
                    m_last_candidate_count++;

                    // skip a binding that another dirty property already claimed
                    if (m_binding_dirty.at(binding) != 0) {
                        continue;
                    }

                    // mark the binding as a candidate of this pass
                    m_binding_dirty.at(binding) = 1;

                    // reset the blocking count before counting the producer edges
                    m_pending_deps.at(binding) = 0;

                    // add the binding to the candidate set
                    m_candidates.push_back(binding);
                }
            }
        }

        // count the edges inside the dirty subgraph as the blocking count
        for (const BindingId producer : m_candidates) {
            // resolve the edge range of this producer
            const std::uint32_t begin = m_binding_edge_offsets.at(producer);
            const std::uint32_t end = m_binding_edge_offsets.at(producer + 1);

            // block every consumer that depends on this producer
            for (std::uint32_t index = begin; index < end; index++) {
                m_pending_deps.at(m_binding_dependents.at(index))++;
            }
        }
    }

    void DirtyBitsetStrategy::release_consumers(BindingId binding, std::vector<BindingId>& ready) {
        // resolve the edge range of this binding
        const std::uint32_t begin = m_binding_edge_offsets.at(binding);
        const std::uint32_t end = m_binding_edge_offsets.at(binding + 1);

        // walk every consumer that reads what this binding writes
        for (std::uint32_t index = begin; index < end; index++) {
            // resolve the waiting consumer
            const BindingId consumer = m_binding_dependents.at(index);

            // release only candidates because the rest does not run in this pass
            if (m_binding_dirty.at(consumer) != 0 && --m_pending_deps.at(consumer) == 0) {
                ready.push_back(consumer);
            }
        }
    }

    void DirtyBitsetStrategy::clear_dirty() {
        // forget the dirty properties of the pass
        std::ranges::fill(m_property_dirty, std::uint64_t{0});

        // forget the candidate marks of the pass
        std::ranges::fill(m_binding_dirty, std::uint8_t{0});
    }

    void DirtyBitsetStrategy::plan(std::vector<BindingId>& out) {
        // find every binding that reads a dirty property
        collect_candidates();

        // start the ordering with an empty ready set
        m_ready.clear();

        // forget the cycle count of the previous pass
        m_cycle_count = 0;

        // make every candidate without unresolved producers ready
        for (const BindingId binding : m_candidates) {
            // admit a candidate that nothing blocks
            if (m_pending_deps.at(binding) == 0) {
                m_ready.push_back(binding);
            }
        }

        // drain the ready set to emit the bindings in dependency order
        for (std::size_t head = 0; head < m_ready.size(); head++) {
            // take the next binding whose dependencies are resolved
            const BindingId binding = m_ready.at(head);

            // emit the binding before its consumers
            out.push_back(binding);

            // release the consumers that were blocked by this binding
            release_consumers(binding, m_ready);
        }

        // emit the blocked bindings of a cycle in declaration order
        if (m_ready.size() != m_candidates.size()) {
            // count the bindings the ordering could not resolve
            m_cycle_count = m_candidates.size() - m_ready.size();

            // emit the unresolved candidates so that the engine can still converge
            for (const BindingId binding : m_candidates) {
                // emit only the bindings that stayed blocked
                if (m_pending_deps.at(binding) != 0) {
                    out.push_back(binding);
                }
            }
        }

        // forget the dirty state before the engine runs the bindings
        clear_dirty();
    }

    std::size_t DirtyBitsetStrategy::index_bytes() const {
        // charge the declared dependency array
        const std::size_t dependency_bytes = m_binding_dep_offsets.size() * sizeof(std::uint32_t) + m_binding_deps.size() * sizeof(PropertyId);

        // charge the binding to binding edge table
        const std::size_t edge_bytes = m_binding_edge_offsets.size() * sizeof(std::uint32_t) + m_binding_dependents.size() * sizeof(BindingId);

        // charge the dirty bitset and the per binding pass state
        const std::size_t state_bytes = m_property_dirty.size() * sizeof(std::uint64_t) + m_pending_deps.size() * sizeof(std::uint32_t) + m_binding_dirty.size();

        // charge the scratch vectors used while ordering
        const std::size_t scratch_bytes = (m_candidates.capacity() + m_ready.capacity()) * sizeof(BindingId);

        // add every part the strategy keeps in memory
        return m_index.bytes() + dependency_bytes + edge_bytes + state_bytes + scratch_bytes + m_binding_targets.size() * sizeof(PropertyId);
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
