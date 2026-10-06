#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "prototypes/d/property_id.h"

namespace albor::prototype_d
{

    // deduplicate bindings per pass with an epoch stamp instead of a set object (§13.3)
    class StampedBindingSet
    {
    public:
        void reset(std::size_t binding_count) {
            m_stamps.assign(binding_count, 0);
            // start the epoch at one so that a zero stamp never matches the pass
            m_epoch = 1;
        }

        // advance the pass and reuse the stamp array
        void begin_pass() { m_epoch++; }

        // return true the first time the binding is seen in this pass
        bool mark(BindingId binding) {
            if (m_stamps.at(binding) == m_epoch) {
                return false;
            }

            m_stamps.at(binding) = m_epoch;
            return true;
        }

        [[nodiscard]] std::size_t bytes() const { return m_stamps.size() * sizeof(std::uint64_t); }

    private:
        std::vector<std::uint64_t> m_stamps;
        std::uint64_t m_epoch = 1;
    };

    // index the bindings that read a property in one flat array
    class DependentsIndex
    {
    public:
        void declare(std::span<const PropertyId> dependencies, BindingId binding);

        // bucket the declared pairs once the property count is known
        void finish(std::size_t property_count);

        [[nodiscard]] std::span<const BindingId> dependents(PropertyId property) const;
        [[nodiscard]] std::size_t bytes() const;
        [[nodiscard]] std::size_t dependency_count() const { return m_entries.size(); }

    private:
        std::vector<std::pair<PropertyId, BindingId>> m_entries;
        std::vector<std::uint32_t> m_offsets;
        std::vector<BindingId> m_bindings;
    };

} // namespace albor::prototype_d
