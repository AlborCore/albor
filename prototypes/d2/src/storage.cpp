#include "prototypes/d2/src/storage.h"

#include <cstring>
#include <new>
#include <utility>

namespace albor::prototype_d2
{

    namespace
    {

        // read one bool out of an opaque byte address (§13.3)
        bool load_bool(const void* address) { return *static_cast<const bool*>(address); }

        // read one int64 out of an opaque byte address (§13.3)
        std::int64_t load_int(const void* address) { return *static_cast<const std::int64_t*>(address); }

        // read one double out of an opaque byte address (§13.3)
        double load_double(const void* address) { return *static_cast<const double*>(address); }

        // destroy and release the payload a stored value owns, in place (§13.1)
        void release_in_place(void* address, ValueType type) {
            // only the payload owning types have a destructor to run (§13.1)
            if (type == ValueType::String) {
                // destroy the shared string pointer without constructing a replacement
                static_cast<std::shared_ptr<const std::string>*>(address)->~shared_ptr();
                return;
            }

            if (type == ValueType::TranslatedString) {
                // destroy the locale and the shared string pointer (§13.1)
                static_cast<TranslatedString*>(address)->~TranslatedString();
                return;
            }

            if (type == ValueType::Snapshot) {
                // destroy the snapshot box and release its payload (§10.5)
                static_cast<SnapshotBox*>(address)->~SnapshotBox();
            }
        }

        // construct one value of a declared type in place, from the alternative the write carries (§13.3)
        void construct_in_place(void* address, ValueType type, const Value& value) {
            // a scalar is written straight through because it has no lifetime to manage (§13.3)
            switch (type) {
            case ValueType::Bool:
                *static_cast<bool*>(address) = value.as_bool();
                return;
            case ValueType::Int:
                *static_cast<std::int64_t*>(address) = value.as_int();
                return;
            case ValueType::Double:
                *static_cast<double*>(address) = value.as_double();
                return;
            default:
                break;
            }

            // a payload owning type is constructed in place so the slot holds the object itself (§13.1)
            if (type == ValueType::String) {
                new (address) std::shared_ptr<const std::string>(value.as_string());
                return;
            }

            if (type == ValueType::TranslatedString) {
                new (address) TranslatedString(value.as_translated());
                return;
            }

            new (address) SnapshotBox(value.as_snapshot());
        }

    } // namespace

    InstanceId ErasedStore::create(SchemaId schema) {
        // take the property id range of the new instance from the region table (§5)
        const InstanceId instance = m_regions.create(schema);

        // grow the slot array so the new id range is addressable (§13.3)
        grow_to(m_regions.property_id_count());

        // seed every property of the instance with its declared type (§13.4)
        const ComponentSchema& declared = m_regions.schema(schema);

        for (std::size_t local = 0; local < declared.property_count(); local++) {
            // resolve the global id of this component-local property (AD-04a)
            const PropertyId id = m_regions.global(instance, static_cast<LocalPropertyId>(local));

            // start from an empty value of the declared type so a read never sees a mismatched discriminant
            m_slots.at(id).value = Value();

            // record the write source that generated initialization uses (§13.4)
            m_slots.at(id).source = WriteSource::GeneratedInit;
        }

        // hand back the new instance
        return instance;
    }

    void ErasedStore::destroy(InstanceId instance) {
        // release the payload every property of the instance holds (§13.1)
        const Region& slice = m_regions.region(instance);

        for (std::uint32_t local = 0; local < slice.property_count; local++) {
            // resolve the global id and release the value it holds (§13.1)
            const PropertyId id = slice.first_id + local;
            m_slots.at(id).value = Value();
        }

        // return the region to the table (§5)
        m_regions.destroy(instance);
    }

    bool ErasedStore::write(PropertyId id, Value value, WriteSource source) {
        // resolve the slot that receives the write (§13.3)
        ErasedSlot& target = m_slots.at(id);

        // keep the generation when a write leaves the value unchanged (§13.3)
        if (target.value.same_as(value)) {
            return false;
        }

        // publish the new value and record the source that decides precedence (§13.4)
        target.value = std::move(value);
        target.source = source;
        target.generation++;

        // an imperative assignment breaks the binding, while generated init, binding and animation do not (§13.4, §13.5)
        if (source == WriteSource::Imperative) {
            target.attached = 0;
        }

        // report the change that justifies invalidating a dependent
        return true;
    }

    Value ErasedStore::read(PropertyId id) const { return m_slots.at(id).value; }

    ValueType ErasedStore::type_of(PropertyId id) const { return m_slots.at(id).value.type(); }

    WriteSource ErasedStore::source_of(PropertyId id) const { return m_slots.at(id).source; }

    std::uint64_t ErasedStore::generation_of(PropertyId id) const { return m_slots.at(id).generation; }

    void ErasedStore::attach_binding(PropertyId id) {
        m_slots.at(id).source = WriteSource::Binding;
        m_slots.at(id).attached = 1;
    }

    bool ErasedStore::binding_attached(PropertyId id) const { return m_slots.at(id).attached != 0; }

    bool ErasedStore::has_binding(PropertyId id) const { return m_slots.at(id).source == WriteSource::Binding; }

    void ErasedStore::rebind(PropertyId id) {
        m_slots.at(id).source = WriteSource::Binding;
        m_slots.at(id).attached = 1;
    }

    void ErasedStore::set_source(PropertyId id, WriteSource source) {
        m_slots.at(id).source = source;

        // an imperative owner is not an attached binding (§13.4)
        if (source == WriteSource::Imperative) {
            m_slots.at(id).attached = 0;
        }
    }

    std::size_t ErasedStore::payload_bytes() const {
        // accumulate the heap payload of every stored value (§28.2)
        std::size_t total = 0;

        // walk the whole slot array (§13.3)
        for (const ErasedSlot& current : m_slots) {
            // add the payload the stored value knows exactly
            total += current.value.known_heap_bytes();
        }

        // report the accumulated payload
        return total;
    }

    void ErasedStore::grow_to(std::size_t property_count) {
        // do nothing when the slot array already covers the id range (§13.3)
        if (property_count <= m_slots.size()) {
            return;
        }

        // count the reallocation so the allocation measurement is honest (§1 T1.4)
        m_reallocations++;

        // grow the dense slot array to the new id range
        m_slots.resize(property_count);
    }

    void TypedStore::reserve_chunk() {
        // allocate one fixed size chunk so earlier region addresses stay valid (§13.3)
        m_chunks.push_back(std::make_unique<std::byte[]>(SLAB_CHUNK_BYTES));

        // start the new chunk at its first byte
        m_bump = 0;
    }

    InstanceId TypedStore::create(SchemaId schema) {
        // take the property id range of the new instance from the region table (§5)
        const InstanceId instance = m_regions.create(schema);

        // resolve the declared layout of the component type (§13.3)
        const ComponentSchema& declared = m_regions.schema(schema);

        // make sure the addressability table covers the new id range (§5 T5.6)
        const std::size_t id_count = m_regions.property_id_count();
        m_location.resize(id_count, 0);
        m_type.resize(id_count, static_cast<std::uint8_t>(ValueType::None));
        m_source.resize(id_count, static_cast<std::uint8_t>(WriteSource::GeneratedInit));
        m_attached.resize(id_count, 0);

        // make sure the region table of byte offsets covers the new instance (§5)
        m_region_base.resize(m_regions.region_count(), 0);

        // allocate a chunk when the region does not fit in what the last chunk has left (§13.3)
        if (m_chunks.empty() || (m_bump + declared.region_bytes) > SLAB_CHUNK_BYTES) {
            reserve_chunk();
        }

        // remember where this region starts so it can be destroyed later (§5)
        const std::size_t chunk_index = m_chunks.size() - 1;
        m_region_base.at(instance) = static_cast<std::uint32_t>(chunk_index * SLAB_CHUNK_BYTES + m_bump);

        // record the base address of every property of this region (§5 T5.6)
        for (std::size_t local = 0; local < declared.property_count(); local++) {
            // resolve the global id of this component-local property (AD-04a)
            const PropertyId id = m_regions.global(instance, static_cast<LocalPropertyId>(local));

            // remember the byte offset of the value inside the slab (§13.3)
            m_location.at(id) = m_region_base.at(instance) + static_cast<std::uint32_t>(declared.offset_of(static_cast<LocalPropertyId>(local)));

            // remember the declared type so a write can place the right alternative (§13.3)
            m_type.at(id) = static_cast<std::uint8_t>(declared.type_of(static_cast<LocalPropertyId>(local)));
        }

        // leave the bump pointer past the region (§13.3)
        m_bump += declared.region_bytes;

        // hand back the new instance
        return instance;
    }

    void TypedStore::destroy(InstanceId instance) {
        // resolve the declared layout so every value of the region can be destroyed (§13.1)
        const ComponentSchema& declared = m_regions.schema(m_regions.region(instance).schema);

        // destroy the value of every property in declaration order (§13.1)
        for (std::size_t local = 0; local < declared.property_count(); local++) {
            // resolve the global id of this component-local property (AD-04a)
            const PropertyId id = m_regions.global(instance, static_cast<LocalPropertyId>(local));

            // release the payload the value owns
            destroy_value(id);
        }

        // return the region to the table (§5)
        m_regions.destroy(instance);
    }

    void TypedStore::destroy_value(PropertyId id) {
        // only a payload owning type has a destructor to run (§13.1)
        const ValueType type = type_of(id);

        // do nothing for a scalar, which has no lifetime to end (§13.1)
        if (!is_non_trivial(type)) {
            return;
        }

        // release the payload in place (§13.1)
        release_in_place(address_of(id), type);
    }

    void* TypedStore::address_of(PropertyId id) const {
        // resolve the byte offset the addressability table recorded (§5 T5.6)
        const std::uint32_t offset = m_location.at(id);

        // split the offset into the chunk that holds it and the byte inside that chunk (§13.3)
        return m_chunks.at(offset / SLAB_CHUNK_BYTES).get() + (offset % SLAB_CHUNK_BYTES);
    }

    void TypedStore::place(PropertyId id, const Value& value, WriteSource source) {
        // resolve the declared type of the property (§13.3)
        const ValueType type = type_of(id);

        // release the payload the slot currently holds, because it is being replaced (§13.1)
        if (is_non_trivial(type)) {
            release_in_place(address_of(id), type);
        }

        // construct the new value in place (§13.3)
        construct_in_place(address_of(id), type, value);

        // record the write source that decides precedence (§13.4)
        m_source.at(id) = static_cast<std::uint8_t>(source);

        // an imperative assignment breaks the binding, while the other sources do not (§13.4, §13.5)
        if (source == WriteSource::Imperative) {
            m_attached.at(id) = 0;
        }
    }

    bool TypedStore::write(PropertyId id, Value value, WriteSource source) {
        // resolve the declared type of the property (§13.3)
        const ValueType type = type_of(id);

        // compare against the value that is already in the slot, which is what replaces the generation counter (§13.3)
        const Value current = read(id);

        // a payload owning type reads back through a copy, so compare the payloads by value
        if (current.same_as(value)) {
            return false;
        }

        // publish the new value in place (§13.3)
        place(id, value, source);

        // report the change that justifies invalidating a dependent
        return true;
    }

    Value TypedStore::read(PropertyId id) const {
        // resolve the declared type of the property (§13.3)
        const ValueType type = type_of(id);

        // read the alternative the declared type selects, without paying for a variant (§13.3)
        const void* address = address_of(id);

        // rebuild the value the erased representation would have held (§13.3)
        switch (type) {
        case ValueType::None:
            return Value();
        case ValueType::Bool:
            return Value(load_bool(address));
        case ValueType::Int:
            return Value(load_int(address));
        case ValueType::Double:
            return Value(load_double(address));
        case ValueType::String:
            return Value(*static_cast<const std::shared_ptr<const std::string>*>(address));
        case ValueType::TranslatedString:
            return Value(*static_cast<const TranslatedString*>(address));
        case ValueType::Snapshot:
            return Value(*static_cast<const SnapshotBox*>(address));
        }

        // report an empty value for an unknown declared type
        return Value();
    }

    ValueType TypedStore::type_of(PropertyId id) const { return static_cast<ValueType>(m_type.at(id)); }

    WriteSource TypedStore::source_of(PropertyId id) const { return static_cast<WriteSource>(m_source.at(id)); }

    void TypedStore::attach_binding(PropertyId id) {
        m_source.at(id) = static_cast<std::uint8_t>(WriteSource::Binding);
        m_attached.at(id) = 1;
    }

    bool TypedStore::binding_attached(PropertyId id) const { return m_attached.at(id) != 0; }

    bool TypedStore::has_binding(PropertyId id) const { return source_of(id) == WriteSource::Binding; }

    void TypedStore::rebind(PropertyId id) {
        m_source.at(id) = static_cast<std::uint8_t>(WriteSource::Binding);
        m_attached.at(id) = 1;
    }

    void TypedStore::set_source(PropertyId id, WriteSource source) {
        m_source.at(id) = static_cast<std::uint8_t>(source);

        // an imperative owner is not an attached binding (§13.4)
        if (source == WriteSource::Imperative) {
            m_attached.at(id) = 0;
        }
    }

    std::size_t TypedStore::memory_bytes() const { return value_bytes() + address_bytes(); }

    std::size_t TypedStore::value_bytes() const {
        // charge every region for the bytes its schema laid out (§13.3)
        std::size_t total = 0;

        // walk the live regions so destroyed regions stop being charged (§5)
        for (std::size_t index = 0; index < m_regions.region_count(); index++) {
            // skip a region that was destroyed (§5)
            if (!m_regions.alive(static_cast<InstanceId>(index))) {
                continue;
            }

            // add the bytes this component type declared (§13.3)
            total += m_regions.schema(m_regions.region(static_cast<InstanceId>(index)).schema).region_bytes;
        }

        // report the allocated region bytes
        return total;
    }

    std::size_t TypedStore::address_bytes() const {
        // charge the per-property addressability entries that the dense id space needs (§5 T5.6)
        return m_location.size() * sizeof(std::uint32_t) + m_type.size() * sizeof(std::uint8_t) + m_source.size() * sizeof(std::uint8_t) + m_attached.size() * sizeof(std::uint8_t);
    }

    std::size_t TypedStore::payload_bytes() const {
        // accumulate the heap payload of every live property (§28.2)
        std::size_t total = 0;

        // walk the whole addressability table (§5)
        for (std::size_t id = 0; id < m_location.size(); id++) {
            // skip a property whose region was destroyed (§5)
            if (!is_non_trivial(type_of(static_cast<PropertyId>(id)))) {
                continue;
            }

            // add the payload the value in the slot owns (§10.5)
            total += read(static_cast<PropertyId>(id)).known_heap_bytes();
        }

        // report the accumulated payload
        return total;
    }

} // namespace albor::prototype_d2
