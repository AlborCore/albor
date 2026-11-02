#pragma once

#include <cstdint>

namespace albor
{
    // the component scoped identity of a reactive property, which is a base offset plus a slot (AD-04b, §7.2)
    struct PropertyId
    {
        // the runtime base offset of the component instance that owns the property
        std::uint32_t base = 0;

        // the slot of the property inside its owning component
        std::uint32_t slot = 0;

        // compare two identifiers field by field, which the dependency graph relies on
        friend bool operator==(const PropertyId& lhs, const PropertyId& rhs) = default;
    };

    // the identifier a property carries before a component assigns it one, which never names a real property
    [[nodiscard]] constexpr PropertyId invalid_property_id() {
        // the all ones identifier cannot collide with a real base and slot pair
        return {.base = 0xFFFFFFFFu, .slot = 0xFFFFFFFFu};
    }

    // report whether an identifier names a real property
    [[nodiscard]] constexpr bool is_valid(PropertyId id) {
        // the sentinel base is what an unassigned property carries
        return id.base != 0xFFFFFFFFu;
    }

    // the allocator that hands out component scoped property identifiers (§7.2, AD-04b)
    class PropertyIdAllocator
    {
    public:
        // build an allocator whose first component receives the given base offset
        explicit PropertyIdAllocator(std::uint32_t first_base = 1);

        PropertyIdAllocator(const PropertyIdAllocator&) = delete;
        PropertyIdAllocator& operator=(const PropertyIdAllocator&) = delete;
        PropertyIdAllocator(PropertyIdAllocator&&) = delete;
        PropertyIdAllocator& operator=(PropertyIdAllocator&&) = delete;
        ~PropertyIdAllocator() = default;

        // begin a new component and hand out the base offset every property of it shares (§7.2)
        [[nodiscard]] std::uint32_t begin_component();

        // hand out the next property slot of the component that is currently open
        [[nodiscard]] PropertyId next();

        // hand out the number of components that have begun
        [[nodiscard]] std::uint32_t component_count() const;

        // hand out the number of properties the current component has taken
        [[nodiscard]] std::uint32_t slot_count() const;

    private:
        // the base offset the current component hands to its properties
        std::uint32_t m_base;

        // the next base offset begin_component will hand out
        std::uint32_t m_next_base;

        // the next slot the current component hands out
        std::uint32_t m_next_slot;

        // how many components have begun, which starts at one for the open component
        std::uint32_t m_components;
    };
} // namespace albor
