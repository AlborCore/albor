#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "prototypes/d/property_id.h"

namespace albor::prototype_d
{

    // decide which bindings run after a property changed and in what order (§13.3)
    // compare the compact dependency records, the dirty bitset with topological ordering and the shared dependency nodes (§30)
    class InvalidationStrategy
    {
    public:
        InvalidationStrategy() = default;
        virtual ~InvalidationStrategy() = default;

        InvalidationStrategy(const InvalidationStrategy&) = delete;
        InvalidationStrategy& operator=(const InvalidationStrategy&) = delete;
        InvalidationStrategy(InvalidationStrategy&&) = delete;
        InvalidationStrategy& operator=(InvalidationStrategy&&) = delete;

        [[nodiscard]] virtual std::string_view name() const = 0;

        // register a binding with its declared dependencies and its written property
        virtual void declare(BindingId binding, std::span<const PropertyId> dependencies, PropertyId target) = 0;

        // build the index once every binding and property is known
        virtual void finish(std::size_t property_count) = 0;

        // report a changed property from an imperative write, a drained worker write or a binding write
        virtual void invalidate(PropertyId changed) = 0;

        // append the bindings to evaluate now, deduplicated and in evaluation order
        virtual void plan(std::vector<BindingId>& out) = 0;

        // count the index bytes without the property slots and the binding records
        [[nodiscard]] virtual std::size_t index_bytes() const = 0;

        // count the dirty bindings of the last plan to measure the fan-out of one invalidation
        [[nodiscard]] virtual std::size_t last_candidate_count() const = 0;

        // count the dependencies the index has to remember
        [[nodiscard]] virtual std::size_t remembered_dependency_count() const = 0;
    };

} // namespace albor::prototype_d
