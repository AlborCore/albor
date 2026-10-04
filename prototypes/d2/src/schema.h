#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "prototypes/d2/src/value.h"

namespace albor::prototype_d2
{

    // identify a property inside the whole scene by a dense index into the property space (§13.3)
    using PropertyId = std::uint32_t;

    // identify a property inside one component instance by a component-local offset (AD-04a, Prototype B)
    using LocalPropertyId = std::uint16_t;

    // identify a component type by a dense index into the schema table
    using SchemaId = std::uint32_t;

    // identify one live instance of a component type by a dense index into the region table
    using InstanceId = std::uint32_t;

    constexpr PropertyId INVALID_PROPERTY = static_cast<PropertyId>(-1);
    constexpr InstanceId INVALID_INSTANCE = static_cast<InstanceId>(-1);

    // distinguish the dynamic structures the design requires a mutable graph for (§18, §20)
    enum class InstanceKind : std::uint8_t
    {
        Component,
        Delegate,
        Popup
    };

    // declare the property types of one component type and precompute its inline layout (§13.3)
    struct ComponentSchema
    {
        std::string_view name;
        InstanceKind kind = InstanceKind::Component;
        std::vector<ValueType> types;
        std::vector<std::size_t> offsets;
        std::size_t region_bytes = 0;

        [[nodiscard]] std::size_t property_count() const { return types.size(); }

        // report the byte range of one component-local property inside its region (§13.3)
        [[nodiscard]] std::size_t offset_of(LocalPropertyId local) const { return offsets.at(local); }

        [[nodiscard]] ValueType type_of(LocalPropertyId local) const { return types.at(local); }
    };

    // hold one instance's slice of the dense property id space and the stamp that detects staleness (AD-04a)
    struct Region
    {
        SchemaId schema = 0;
        PropertyId first_id = INVALID_PROPERTY;
        std::uint32_t property_count = 0;
        std::uint64_t stamp = 0;
        bool alive = false;
    };

    // report whether a region was reused for a component type with a different property count (§5 T5.5)
    struct ReuseReport
    {
        std::size_t reused_same_size = 0;
        std::size_t rejected_mismatched_size = 0;
        std::size_t appended_fresh = 0;

        // report the property id space the table had to grow to accommodate the reuse policy
        [[nodiscard]] std::size_t id_high_water() const { return m_id_high_water; }

        std::size_t m_id_high_water = 0;
    };

    // map component-local property ids through a per-instance base offset (§13.3, AD-04a)
    //
    // the dense property id space is what makes a bound property a plain index, and it is also what makes region
    // reuse dangerous: freeing a region of n properties and allocating a region of m > n shifts every id above it,
    // so this table only ever reuses a region whose property count matches, and records the rest (§5 T5.5)
    class RegionTable
    {
    public:
        // register a component type and precompute the inline layout of its properties (§13.3)
        SchemaId add_schema(std::string_view name, InstanceKind kind, std::span<const ValueType> types);

        [[nodiscard]] const ComponentSchema& schema(SchemaId id) const { return m_schemas.at(id); }
        [[nodiscard]] std::size_t schema_count() const { return m_schemas.size(); }

        // allocate an instance region, reusing a free region of the same property count (§5)
        InstanceId create(SchemaId schema);

        // free a region and return its property count so the store can release the bytes (§5)
        void destroy(InstanceId instance);

        [[nodiscard]] const Region& region(InstanceId instance) const { return m_regions.at(instance); }
        [[nodiscard]] bool alive(InstanceId instance) const { return m_regions.at(instance).alive; }

        // resolve a component-local id through the base offset of the instance (AD-04a)
        [[nodiscard]] PropertyId global(InstanceId instance, LocalPropertyId local) const {
            const Region& slice = m_regions.at(instance);
            return slice.first_id + static_cast<PropertyId>(local);
        }

        // report the stamp an instance carries so a stale id can be detected (§5 T5.4)
        [[nodiscard]] std::uint64_t stamp(InstanceId instance) const { return m_regions.at(instance).stamp; }

        // report a matching region so a caller holding a stamp can tell it went away (§5 T5.4)
        [[nodiscard]] InstanceId instance_for_stamp(InstanceId instance, std::uint64_t stamp) const {
            const Region& slice = m_regions.at(instance);
            return slice.alive && slice.stamp == stamp ? instance : INVALID_INSTANCE;
        }

        [[nodiscard]] std::size_t live_instance_count() const;
        [[nodiscard]] std::size_t region_count() const { return m_regions.size(); }
        [[nodiscard]] std::size_t property_id_count() const { return m_next_id; }
        [[nodiscard]] const ReuseReport& reuse() const { return m_reuse; }

    private:
        std::vector<ComponentSchema> m_schemas;
        std::vector<Region> m_regions;
        std::vector<InstanceId> m_free_regions;
        PropertyId m_next_id = 0;
        std::uint64_t m_next_stamp = 1;
        ReuseReport m_reuse;
    };

} // namespace albor::prototype_d2
