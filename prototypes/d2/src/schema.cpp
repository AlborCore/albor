#include "prototypes/d2/src/schema.h"

#include <algorithm>

namespace albor::prototype_d2
{

    namespace
    {

        // round a byte count up to the alignment its type requires (§13.3)
        std::size_t align_up(std::size_t offset, std::size_t alignment) {
            // an alignment of one never moves the offset
            if (alignment <= 1) {
                return offset;
            }

            // round the offset up to the next multiple of the alignment
            return ((offset + alignment - 1) / alignment) * alignment;
        }

    } // namespace

    SchemaId RegionTable::add_schema(std::string_view name, InstanceKind kind, std::span<const ValueType> types) {
        // prepare the schema and precompute where each property lands in the region (§13.3)
        ComponentSchema entry;
        entry.name = name;
        entry.kind = kind;
        entry.types.assign(types.begin(), types.end());
        entry.offsets.reserve(types.size());

        // lay the properties out in declaration order, honouring each type's alignment
        std::size_t cursor = 0;

        // remember the strictest alignment the declared types need, so the region is padded to that and no further
        std::size_t region_alignment = 1;

        for (const ValueType type : types) {
            // move to the alignment this value needs
            cursor = align_up(cursor, inline_align_of(type));

            // record where this property starts inside the region
            entry.offsets.push_back(cursor);

            // reserve the inline bytes of this property
            cursor += inline_size_of(type);

            // track the strictest alignment any declared type needs (§13.3)
            region_alignment = std::max(region_alignment, inline_align_of(type));
        }

        // pad the region to its own alignment so the next region can start aligned. Padding to max_align_t instead
        // would charge a three bool component 16 bytes rather than 3, which is the layout question itself (§13.3)
        entry.region_bytes = align_up(cursor, region_alignment);

        // append the schema and hand back its dense id
        m_schemas.push_back(std::move(entry));
        return static_cast<SchemaId>(m_schemas.size() - 1);
    }

    InstanceId RegionTable::create(SchemaId schema) {
        // resolve the declared property count of the component type
        const std::uint32_t property_count = static_cast<std::uint32_t>(m_schemas.at(schema).property_count());

        // look for a free region whose size matches, because reusing a different size would shift every id above it (§5 T5.5)
        std::size_t free_slot = m_free_regions.size();

        for (std::size_t index = 0; index < m_free_regions.size(); index++) {
            const Region& candidate = m_regions.at(m_free_regions.at(index));

            // accept the first region that declares the same number of properties
            if (candidate.property_count == property_count) {
                free_slot = index;
                break;
            }
        }

        // claim the region when one matched the declared size
        if (free_slot < m_free_regions.size()) {
            // take the matching region out of the free list
            const InstanceId instance = m_free_regions.at(free_slot);
            m_free_regions.at(free_slot) = m_free_regions.back();
            m_free_regions.pop_back();

            // count the reuse that kept the id space stable
            m_reuse.reused_same_size++;

            // bring the region back with a fresh stamp so stale ids are detectable (§5 T5.4)
            Region& revived = m_regions.at(instance);
            revived.schema = schema;
            revived.stamp = m_next_stamp++;
            revived.alive = true;

            // hand back the revived instance
            return instance;
        }

        // record that no region of this size was available to reuse
        if (!m_free_regions.empty()) {
            m_reuse.rejected_mismatched_size += m_free_regions.size();
        }

        // allocate a fresh region because the id space cannot absorb a different size (§5 T5.5)
        Region fresh;
        fresh.schema = schema;
        fresh.first_id = m_next_id;
        fresh.property_count = property_count;
        fresh.stamp = m_next_stamp++;
        fresh.alive = true;

        // append the region and hand back its dense id
        m_regions.push_back(fresh);
        m_reuse.appended_fresh++;

        // grow the dense property id space by the new region (§13.3)
        m_next_id += property_count;
        m_reuse.m_id_high_water = m_next_id;

        // report the new instance
        return static_cast<InstanceId>(m_regions.size() - 1);
    }

    void RegionTable::destroy(InstanceId instance) {
        // resolve the region that goes away
        Region& slice = m_regions.at(instance);

        // ignore a region that is already free so a double destroy cannot corrupt the list
        if (!slice.alive) {
            return;
        }

        // mark the region free and retire its stamp so stale ids fail the check (§5 T5.4)
        slice.alive = false;

        // keep the id range so a same-size region can be revived without shifting ids
        m_free_regions.push_back(instance);
    }

    std::size_t RegionTable::live_instance_count() const {
        // count the regions that still hold a live instance
        std::size_t live = 0;

        for (const Region& slice : m_regions) {
            if (slice.alive) {
                live++;
            }
        }

        // report the live instance count
        return live;
    }

} // namespace albor::prototype_d2
