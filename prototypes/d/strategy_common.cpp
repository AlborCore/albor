#include "prototypes/d/strategy_common.h"

#include <algorithm>

namespace albor::prototype_d
{

    void DependentsIndex::declare(std::span<const PropertyId> dependencies, BindingId binding) {
        // record one entry per declared dependency
        for (const PropertyId dependency : dependencies) {
            m_entries.emplace_back(dependency, binding);
        }
    }

    void DependentsIndex::finish(std::size_t property_count) {
        // sort by property so that the bindings reading one property end up contiguous
        std::ranges::sort(m_entries, {}, &std::pair<PropertyId, BindingId>::first);

        // allocate one offset slot per property plus the end sentinel
        m_offsets.assign(property_count + 1, 0);

        // count the bindings that read each property into the shifted slot
        for (const auto& entry : m_entries) {
            m_offsets.at(entry.first + 1)++;
        }

        // turn the counts into start offsets
        for (std::size_t index = 0; index < property_count; index++) {
            m_offsets.at(index + 1) += m_offsets.at(index);
        }

        // scatter the bindings into their property bucket using a private cursor
        std::vector<std::uint32_t> cursor(m_offsets.begin(), m_offsets.end() - 1);
        m_bindings.assign(m_entries.size(), INVALID_BINDING);

        // write each binding into the bucket of the property it reads
        for (const auto& entry : m_entries) {
            m_bindings.at(cursor.at(entry.first)++) = entry.second;
        }
    }

    std::span<const BindingId> DependentsIndex::dependents(PropertyId property) const {
        // resolve the range of bindings that read the property
        const std::uint32_t begin = m_offsets.at(property);
        const std::uint32_t end = m_offsets.at(property + 1);

        // hand back the range without copying it (§13.3)
        return std::span<const BindingId>(m_bindings.data() + begin, end - begin);
    }

    std::size_t DependentsIndex::bytes() const { return m_offsets.size() * sizeof(std::uint32_t) + m_bindings.size() * sizeof(BindingId); }

} // namespace albor::prototype_d
