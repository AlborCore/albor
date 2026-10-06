#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "prototypes/d2/src/graph.h"
#include "prototypes/d2/src/index.h"

namespace albor::prototype_d2
{

    // report what a cycle check found, and what it cost (§7)
    struct CycleReport
    {
        bool has_cycle = false;
        std::vector<BindingId> members;
        std::size_t bindings_considered = 0;
        std::size_t edges_considered = 0;
    };

    // find every binding that sits on a cycle by running Kahn over the binding graph (§7 T7.1)
    //
    // a binding the walk can never reach sits on, or downstream of, a cycle. The prototype reports the unreachable
    // set rather than trying to isolate the minimal cycle, because the shipping question is whether a cycle exists,
    // not which binding introduced it.
    template <class Store>
    CycleReport detect_cycles(const Graph<Store>& graph) {
        // accumulate what the check did (§28.2)
        CycleReport report;

        // the binding count bounds every array below (§7)
        const std::size_t binding_count = graph.binding_count();
        report.bindings_considered = binding_count;

        // nothing can be cyclic without bindings (§7)
        if (binding_count == 0) {
            return report;
        }

        // count the incoming edges of every binding (§7)
        std::vector<std::size_t> indegree(binding_count, 0);

        // build the forward edges: the producer of a read property runs first (§7)
        std::vector<std::vector<BindingId>> edges(binding_count);

        // walk every live binding and resolve the producer of each declared dependency (§7)
        for (BindingId consumer = 0; consumer < binding_count; consumer++) {
            // skip a binding that was removed (§4)
            if (!graph.binding_alive(consumer)) {
                continue;
            }

            for (const PropertyId dependency : graph.dependencies_of(consumer)) {
                // resolve the binding that writes the dependency (§7)
                const BindingId producer = graph.writer_of(dependency);

                // a directly written property orders nothing (§7)
                if (producer == INVALID_BINDING || producer == consumer || !graph.binding_alive(producer)) {
                    continue;
                }

                // record the edge and count it (§7)
                edges.at(producer).push_back(consumer);
                indegree.at(consumer)++;
            }
        }

        // sum the edges so the cost of the check can be attributed (§28.2)
        for (const auto& outgoing : edges) {
            report.edges_considered += outgoing.size();
        }

        // collect the bindings with no incoming edge to start the walk (§7)
        std::vector<BindingId> ready;

        for (BindingId binding = 0; binding < binding_count; binding++) {
            if (graph.binding_alive(binding) && indegree.at(binding) == 0) {
                ready.push_back(binding);
            }
        }

        // walk the graph in Kahn order (§7)
        std::vector<std::uint8_t> reached(binding_count, 0);

        while (!ready.empty()) {
            // take the next binding with no unmet dependency (§7)
            const BindingId binding = ready.back();
            ready.pop_back();

            // mark it reachable (§7)
            reached.at(binding) = 1;

            // release the edges it satisfied (§7)
            for (const BindingId consumer : edges.at(binding)) {
                // count down the unmet dependencies (§7)
                if (--indegree.at(consumer) == 0) {
                    // the consumer may now be reached (§7)
                    ready.push_back(consumer);
                }
            }
        }

        // every live binding the walk never reached sits on or behind a cycle (§7)
        for (BindingId binding = 0; binding < binding_count; binding++) {
            if (graph.binding_alive(binding) && reached.at(binding) == 0) {
                report.has_cycle = true;
                report.members.push_back(binding);
            }
        }

        // hand back what the check found (§7)
        return report;
    }

    // report whether the topological order the graph built at seal already proves the graph is acyclic (§7 T7.1)
    template <class Store>
    bool seal_proved_acyclic(const Graph<Store>& graph) {
        // Kahn reaches every binding exactly when the graph is acyclic (§7)
        return graph.ordered_binding_count() == graph.binding_count();
    }

    // report the cycle membership as property ids, so a failure names the properties a developer would see (§7)
    template <class Store>
    std::vector<PropertyId> cycle_property_ids(const Graph<Store>& graph, const CycleReport& report) {
        // translate the binding ids of the report into the properties they write (§7)
        std::vector<PropertyId> properties;
        properties.reserve(report.members.size());

        for (const BindingId binding : report.members) {
            properties.push_back(graph.target_of(binding));
        }

        // hand back the property ids of the cycle
        return properties;
    }

} // namespace albor::prototype_d2
