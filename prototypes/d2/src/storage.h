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

    // the first chunk and the cap of the geometric growth policy D3 measures (plan.md R3): a small first chunk
    // keeps one-instance stores from paying a whole 64 KB, doubling until the cap restores the amortisation
    constexpr std::size_t SLAB_FIRST_CHUNK_BYTES = 1024;

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

        // choose what an equal write means, which defaults to the comparison D2 recorded (plan.md R6)
        void set_equality_policy(EqualityPolicy policy) { m_equality = policy; }

        // write and report the change that justifies invalidating a dependent (§13.3)
        bool write(PropertyId id, Value value, WriteSource source);

        [[nodiscard]] Value read(PropertyId id) const;

        // read a scalar property without building the 40 B variant a Value carries (plan.md R10)
        //
        // generated bindings know the declared type of every property they read (§14.1), so the fast path is the
        // one the compiler would emit; BindingContext::read_int keeps D2's recorded path, which builds the Value
        std::int64_t read_int(PropertyId id) const { return m_slots.at(id).value.as_int(); }
        double read_double(PropertyId id) const { return m_slots.at(id).value.as_double(); }
        bool read_bool(PropertyId id) const { return m_slots.at(id).value.as_bool(); }

        // read a payload owning property through the same path a Value read would take (plan.md R10)
        [[nodiscard]] std::shared_ptr<const std::string> read_string(PropertyId id) const { return m_slots.at(id).value.as_string(); }

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

        // report the bytes the slot array's capacity holds, so D3 can separate live slots from reallocation slack
        [[nodiscard]] std::size_t slot_capacity_bytes() const { return m_slots.capacity() * sizeof(ErasedSlot); }
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

        // the write-compare rule, which defaults to D2's recorded comparison (plan.md R6)
        EqualityPolicy m_equality = EqualityPolicy::Exact;
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
        // choose how the slab grows (plan.md R3): the fixed 64 KB chunk is D2's recorded behaviour and the
        // default, so every figure D2 published stays reproducible; the geometric policy starts small and
        // doubles up to the fixed cap
        enum class SlabGrowth : std::uint8_t
        {
            Fixed64K,
            Geometric
        };

        explicit TypedStore(RegionTable& regions) :
            m_regions(regions) {}

        TypedStore(const TypedStore&) = delete;
        TypedStore& operator=(const TypedStore&) = delete;
        TypedStore(TypedStore&&) = delete;
        TypedStore& operator=(TypedStore&&) = delete;

        // set the growth policy once, before the first region is placed (§28.2)
        void set_slab_growth(SlabGrowth growth) { m_growth = growth; }

        // choose what an equal write means, which defaults to the comparison D2 recorded (plan.md R6)
        void set_equality_policy(EqualityPolicy policy) { m_equality = policy; }

        InstanceId create(SchemaId schema);
        void destroy(InstanceId instance);

        bool write(PropertyId id, Value value, WriteSource source);

        [[nodiscard]] Value read(PropertyId id) const;

        // read a scalar property without building the 40 B variant a Value carries (plan.md R10)
        //
        // the declared type comes from the schema, so the fast path reads the alternative the offset already selects;
        // BindingContext::read_int keeps D2's recorded path, which goes through read() and builds the Value
        std::int64_t read_int(PropertyId id) const { return *static_cast<const std::int64_t*>(address_of(id)); }
        double read_double(PropertyId id) const { return *static_cast<const double*>(address_of(id)); }
        bool read_bool(PropertyId id) const { return *static_cast<const bool*>(address_of(id)); }

        // read a payload owning property through the same path a Value read would take (plan.md R10)
        [[nodiscard]] std::shared_ptr<const std::string> read_string(PropertyId id) const { return *static_cast<const std::shared_ptr<const std::string>*>(address_of(id)); }

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

        // report the bytes the slab chunks hold, so D3 can separate region bytes from the slack chunking leaves
        [[nodiscard]] std::size_t slab_allocated_bytes() const;

        // report how many slab chunks were allocated, so one chunk per many instances is visible (§1 T1.4)
        [[nodiscard]] std::size_t allocation_count() const { return m_chunks.size(); }

        // report the bytes one region costs the region table, which is per instance rather than per property
        [[nodiscard]] std::size_t region_table_bytes() const { return m_regions.region_table_bytes(); }

        // report the storage address of one property's value, so the addressability and stability measurements
        // can observe it rather than infer it (plan.md R3)
        [[nodiscard]] void* storage_address_of(PropertyId id) const { return address_of(id); }

        // report the per-instance base-offset entries the typed store keeps alongside the addressability table
        [[nodiscard]] std::size_t region_base_bytes() const { return m_region_base.capacity() * sizeof(std::uint32_t); }
        [[nodiscard]] std::size_t address_bytes() const;
        [[nodiscard]] std::size_t payload_bytes() const;

        [[nodiscard]] std::size_t property_count() const { return m_regions.property_id_count(); }

    private:
        void reserve_chunk();
        void place(PropertyId id, const Value& value, WriteSource source);
        [[nodiscard]] void* address_of(PropertyId id) const;
        void destroy_value(PropertyId id);

        RegionTable& m_regions;

        // hold the slab as fixed size chunks so a region address never moves (§13.3)
        std::vector<std::unique_ptr<std::byte[]>> m_chunks;

        // carry each chunk's size, so a variable growth policy can still account its bytes and split offsets
        std::vector<std::size_t> m_chunk_sizes;

        // the growth policy, which defaults to D2's recorded fixed 64 KB chunk (§28.4)
        SlabGrowth m_growth = SlabGrowth::Fixed64K;

        // the write-compare rule, which defaults to D2's recorded comparison (plan.md R6)
        EqualityPolicy m_equality = EqualityPolicy::Exact;

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
