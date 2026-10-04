#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "prototypes/d2/src/schema.h"
#include "prototypes/d2/src/value.h"

namespace albor::prototype_d2
{

    // choose the write source that decides precedence between initialization, binding, animation and assignment (§13.4, §13.5)
    enum class WriteSource : std::uint8_t
    {
        GeneratedInit,
        Binding,
        Animation,
        Imperative
    };

    // give every slab this many bytes so that region addresses survive further allocation (§13.3)
    constexpr std::size_t SLAB_CHUNK_BYTES = 64 * 1024;

    // hold the value, its write source and the change counter of one type-erased slot (AD-04b Candidate B)
    struct ErasedSlot
    {
        Value value;
        WriteSource source = WriteSource::GeneratedInit;
        std::uint64_t generation = 0;

        // remember whether a binding is still attached, separately from the value's current owner (§13.5)
        std::uint8_t attached = 0;
    };

    // store one uniform variant per property, which is the representation Prototype D measured (§13.3)
    class ErasedStore
    {
    public:
        explicit ErasedStore(RegionTable& regions) :
            m_regions(regions) {}

        ErasedStore(const ErasedStore&) = delete;
        ErasedStore& operator=(const ErasedStore&) = delete;
        ErasedStore(ErasedStore&&) = delete;
        ErasedStore& operator=(ErasedStore&&) = delete;

        // allocate the slots of one component instance and seed them with generated initialization (§13.4)
        InstanceId create(SchemaId schema);
        void destroy(InstanceId instance);

        // write and report the change that justifies invalidating a dependent (§13.3)
        bool write(PropertyId id, Value value, WriteSource source);

        [[nodiscard]] Value read(PropertyId id) const;
        [[nodiscard]] ValueType type_of(PropertyId id) const;
        [[nodiscard]] WriteSource source_of(PropertyId id) const;
        [[nodiscard]] std::uint64_t generation_of(PropertyId id) const;

        void attach_binding(PropertyId id);

        // report whether a binding may write right now, which an imperative write and an animation both prevent (§13.5)
        [[nodiscard]] bool has_binding(PropertyId id) const;

        // report whether a binding is still attached, which an animation does not break but an imperative write does (§13.5)
        [[nodiscard]] bool binding_attached(PropertyId id) const;

        void rebind(PropertyId id);

        // restore a write source without changing the value, so a replacement can keep an override (§13.4)
        void set_source(PropertyId id, WriteSource source);

        // report the slot array and its vector header (§28.2)
        [[nodiscard]] std::size_t memory_bytes() const { return m_slots.size() * sizeof(ErasedSlot); }
        [[nodiscard]] std::size_t payload_bytes() const;
        [[nodiscard]] std::size_t allocation_count() const { return m_reallocations + 1; }
        [[nodiscard]] std::size_t property_count() const { return m_slots.size(); }

        // count the bytes one property occupies, for the bytes-per-property comparison (AD-04b)
        [[nodiscard]] static constexpr std::size_t bytes_per_property() { return sizeof(ErasedSlot); }

    private:
        void grow_to(std::size_t property_count);

        RegionTable& m_regions;
        std::vector<ErasedSlot> m_slots;
        std::size_t m_reallocations = 0;
    };

    // store property values inline at the offsets the component schema computed (AD-04b Candidate A)
    //
    // two costs are measured rather than hidden. The slab is chunked so that a region address stays valid when a
    // later instance is allocated, which costs slack; and a dense property id still has to be turned into a byte
    // address, which costs a per-property location entry plus a type tag (§5 T5.6). Prototype D's slot also carried an
    // 8-byte generation counter, which this store does not need: a write compares the value it is replacing instead.
    class TypedStore
    {
    public:
        explicit TypedStore(RegionTable& regions) :
            m_regions(regions) {}

        TypedStore(const TypedStore&) = delete;
        TypedStore& operator=(const TypedStore&) = delete;
        TypedStore(TypedStore&&) = delete;
        TypedStore& operator=(TypedStore&&) = delete;

        InstanceId create(SchemaId schema);
        void destroy(InstanceId instance);

        bool write(PropertyId id, Value value, WriteSource source);

        [[nodiscard]] Value read(PropertyId id) const;
        [[nodiscard]] ValueType type_of(PropertyId id) const;
        [[nodiscard]] WriteSource source_of(PropertyId id) const;

        // report no generation counter because a typed write compares the value it replaces (§13.3)
        [[nodiscard]] std::uint64_t generation_of(PropertyId) const { return 0; }

        void attach_binding(PropertyId id);

        // report whether a binding may write right now, which an imperative write and an animation both prevent (§13.5)
        [[nodiscard]] bool has_binding(PropertyId id) const;

        // report whether a binding is still attached, which an animation does not break but an imperative write does (§13.5)
        [[nodiscard]] bool binding_attached(PropertyId id) const;

        void rebind(PropertyId id);

        // restore a write source without changing the value, so a replacement can keep an override (§13.4)
        void set_source(PropertyId id, WriteSource source);

        // report the region bytes, the addressability table and the write source bytes (§28.2)
        [[nodiscard]] std::size_t memory_bytes() const;
        [[nodiscard]] std::size_t value_bytes() const;
        [[nodiscard]] std::size_t address_bytes() const;
        [[nodiscard]] std::size_t payload_bytes() const;

        // report how many slab chunks were allocated, so one chunk per many instances is visible (§1 T1.4)
        [[nodiscard]] std::size_t allocation_count() const { return m_chunks.size(); }
        [[nodiscard]] std::size_t property_count() const { return m_regions.property_id_count(); }

    private:
        void reserve_chunk();
        void place(PropertyId id, const Value& value, WriteSource source);
        [[nodiscard]] void* address_of(PropertyId id) const;
        void destroy_value(PropertyId id);

        RegionTable& m_regions;

        // hold the slab as fixed size chunks so a region address never moves (§13.3)
        std::vector<std::unique_ptr<std::byte[]>> m_chunks;

        // map a dense property id to the byte offset of its value inside the slab (§5 T5.6)
        std::vector<std::uint32_t> m_location;

        // remember the declared type of every property so a write can place the right alternative (§13.3)
        std::vector<std::uint8_t> m_type;

        // remember the write source of every property for precedence (§13.4)
        std::vector<std::uint8_t> m_source;

        // remember whether a binding is still attached, separately from the value's current owner (§13.5)
        std::vector<std::uint8_t> m_attached;

        // remember the first byte of every region so a region can be destroyed (§5)
        std::vector<std::uint32_t> m_region_base;

        // track the next free byte of the last chunk (§13.3)
        std::size_t m_bump = 0;
    };

} // namespace albor::prototype_d2