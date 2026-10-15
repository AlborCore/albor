#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace albor::compiler
{
    // the kinds of value the type system knows, which is a closed set plus two parameter forms (§11.1)
    enum class TypeKind
    {
        // the type of an expression that produced nothing, which is an error rather than a value (§11.1)
        Error,

        // the type a handler body has where it only assigns (§11.1)
        Void,

        // a truth value (§11.1)
        Bool,

        // a signed integer, which never widens to float on its own (§11.1)
        Int,

        // a floating point number, which never narrows to int on its own (§11.1)
        Float,

        // a text value, which concatenation and the string conversion produce (§11.1)
        String,

        // a colour value, which a colour literal produces (§11.1)
        Color,

        // an alignment, which a named member of a value set produces (§8.2)
        Align,

        // when a scrollbar is drawn, which a named member of a value set produces (§8)
        Scrollbar,

        // a length in logical or physical pixels, which a length literal produces (§11.1, AD-27)
        Length,

        // an immutable shared value handed across threads, written Snapshot<T> (§10.5, §13.2)
        Snapshot,

        // an ordered collection of one element type, written vector<T> (§8.1)
        Vector,

        // a native component type the manifest declares, which the checker resolves by name (§11.2, AD-17)
        Native,

        // a reference to a shared command object, which a widget action binds to (§18.1, AD-15)
        Command
    };

    // one value type, which is either a scalar kind, a parameter form or a named native type (§11.1)
    class ValueType
    {
    public:
        // build the error type, which stands for a value whose type could not be established (§11.1)
        ValueType();

        // build a scalar type of the given kind, which carries no parameter (§11.1)
        explicit ValueType(TypeKind kind);

        // build a parameter type over the given element, which Snapshot<T> and vector<T> share (§11.1)
        [[nodiscard]] static ValueType parameterized(TypeKind kind, const ValueType& element);

        // build a native type of the given name, which the manifest is the source of (§11.2, AD-17)
        [[nodiscard]] static ValueType native(std::string name);

        // report whether the type is the error type, which suppresses the problems it would otherwise raise (§11.1)
        [[nodiscard]] bool is_error() const;

        // report whether the type is a number, which is what the arithmetic operators accept (§11.1)
        [[nodiscard]] bool is_numeric() const;

        // report whether the type is the given kind, which is how a scalar rule is written (§11.1)
        [[nodiscard]] bool is(TypeKind other) const;

        // report whether two types are the same, which is what an assignment check asks (§11.1)
        [[nodiscard]] bool equals(const ValueType& other) const;

        // render the type the way the author wrote it, which is what a diagnostic prints (§11.1)
        [[nodiscard]] std::string to_string() const;

        // report which form the type takes, which decides what the accessors below mean (§11.1)
        [[nodiscard]] TypeKind kind() const;

        // hand out the native type name, which is empty for every other form (§11.2, AD-17)
        [[nodiscard]] const std::string& native_name() const;

        // hand out the element type of a parameter form, which is the error type for every other form (§11.1)
        [[nodiscard]] const ValueType& element() const;

    private:
        // which form the type takes, which decides what the fields below mean (§11.1)
        TypeKind m_kind = TypeKind::Error;

        // the native type name, which is set only for a native type (§11.2, AD-17)
        std::string m_native_name;

        // the element type of a parameter form, which is null for every other form (§11.1)
        std::shared_ptr<const ValueType> m_element;
    };

    // the scalar type of the given kind, which is what the builtin rules name (§11.1)
    [[nodiscard]] ValueType scalar(TypeKind kind);

    // the error type, which stands for a value whose type could not be established (§11.1)
    [[nodiscard]] ValueType error_type();

    // the type of the builtin conversion with the given name, which is absent when the name is not one (§11.1)
    [[nodiscard]] bool builtin_conversion_target(std::string_view name, TypeKind& out_kind);

    // report whether the conversion may be applied to a value of the given type, because the rules are closed (§11.1)
    [[nodiscard]] bool conversion_allowed(TypeKind from, TypeKind to);

    // report whether a value of one type may be assigned to a target type, which never widens implicitly (§11.1)
    [[nodiscard]] bool assignable(const ValueType& from, const ValueType& to);

    // one property an element type exposes, which the checker resolves an assignment against (§11.1, §11.2)
    struct ElementProperty
    {
        // the name the author writes, which is what a binding names (§11.1)
        std::string name;

        // the type the property holds, which an assigned value must match (§11.1)
        ValueType type;

        // whether a handler may assign the property, which decides if an InOut direction is compatible (§11.1, §7)
        bool assignable = false;

        // whether the property is a length, which the codegen scales by the density of the surface (§11.1, AD-27)
        bool scaled_by_density = false;
    };

    // one signal an element type raises, which a handler attaches to (§11.1, §11.2)
    struct ElementSignal
    {
        // the name the author writes after the handler prefix, which is what the element raises (§11.1)
        std::string name;

        // the parameter types in declaration order, which an invocation must match (§11.1)
        std::vector<ValueType> parameter_types;

        // the names a handler body writes to read each parameter, which the author sees as its own scope (§11.1)
        std::vector<std::string> parameter_names;
    };

    // one field a manifest record declares, which a repeat body reads by name (§11.2, AD-17, AD-13)
    struct RecordFieldType
    {
        // the field name the author writes after the dot, which is what a binding addresses (§11.2, AD-17)
        std::string name;

        // the type the field holds, which a field read resolves to (§11.2)
        ValueType type;

        // whether the field carries the record's identity, which is the one field a row is keyed by (§18.1, AD-13)
        bool is_key = false;
    };

    // one data record the manifest declares, which a repeat walks as the element of a collection (§11.2, AD-17, AD-13)
    struct RecordType
    {
        // the declared name, which is what an author writes after vector (§11.2, AD-17)
        std::string name;

        // the fields in declaration order, where exactly one carries the identity (§11.2, AD-18)
        std::vector<RecordFieldType> fields;

        // whether the file declared the record in ui rather than a manifest, so the header emits it (§11.1)
        bool from_ui = false;

        // find a field by name, which is what a field read resolves against (§11.2, AD-17)
        [[nodiscard]] const RecordFieldType* find_field(std::string_view field_name) const;

        // hand out the field that carries the identity, which a row is keyed by, and is null when none does (§18.1, AD-13)
        [[nodiscard]] const RecordFieldType* key_field() const;
    };

    // one member of a named value set, which the language and the generated code spell differently (§11.1, §11.3)
    struct NamedValueMember
    {
        // the name the author writes after the dot, which is the lower case form (§11.1)
        std::string written;

        // the enumerator the generated code names, which is the capitalised form (§11.3, §8.2)
        std::string code;
    };

    // the closed set of named values an author may write, which a property of that kind resolves against (§11.1)
    struct NamedValueSet
    {
        // the name the author writes before the dot, which is what the language namespaces its values by (§11.1)
        std::string name;

        // the kind every member of the set produces, which is what the set resolves to (§11.1)
        TypeKind kind = TypeKind::Error;

        // whether the file declared the set in ui, so it resolves as a property type and is emitted (§11.1)
        bool user = false;

        // the C++ enumeration the set renders to, which is the name unless the language spells it differently (§11.3)
        std::string cpp_name;

        // the members in written order, which is what a diagnostic lists (§11.1)
        std::vector<NamedValueMember> members;

        // find a member by its written name, which is what a field read resolves against (§11.1)
        [[nodiscard]] const NamedValueMember* find_member(std::string_view member_name) const;
    };

    // find a named value set by name, which is what a field read resolves against (§11.1)
    [[nodiscard]] const NamedValueSet* find_named_value_set(std::string_view set_name);

    // the properties every element type exposes, because a parent arranges any node it holds (§8.2)
    [[nodiscard]] std::vector<ElementProperty> common_element_properties();

    // one element type a ui file may instantiate, which is either a builtin widget or a manifest component (§11.1, §11.2)
    struct ElementType
    {
        // the name the author writes after an element keyword (§11.1)
        std::string name;

        // the properties the type exposes to bindings (§11.1)
        std::vector<ElementProperty> properties;

        // the signals the type raises, which a handler attaches to (§11.1)
        std::vector<ElementSignal> signals;

        // whether the type may contain children, which is what a container accepts (§11.1)
        bool accepts_children = false;

        // find a property by name, which is what an assignment resolves against (§11.1)
        [[nodiscard]] const ElementProperty* find_property(std::string_view property_name) const;

        // find a signal by name, which is what a handler resolves against (§11.1)
        [[nodiscard]] const ElementSignal* find_signal(std::string_view signal_name) const;
    };

    // the set of element types a component may instantiate, which is the builtins plus the manifests (§11.1, §11.2, AD-17)
    class ElementCatalog
    {
    public:
        // build a catalog holding only the builtin widgets, which every ui file may use (§11.1, §8)
        [[nodiscard]] static ElementCatalog with_builtins();

        // add a native component from a manifest, which is the single source of truth for its types (§11.2, AD-17)
        void add_native(const ElementType& type);

        // add a native data type from a manifest, which a snapshot may carry but which is never an element (§11.2, AD-17)
        void add_native_data(std::string name);

        // add a record from a manifest, which a collection of it holds and a repeat walks (§11.2, AD-17, AD-13)
        void add_record(const RecordType& record);

        // find a record by name, which is what a vector element type resolves against (§11.2, AD-17)
        [[nodiscard]] const RecordType* find_record(std::string_view name) const;

        // add a named value set the file declared in ui, which a property may then hold one member of (§11.1)
        void add_named_value_set(const NamedValueSet& set);

        // find a named value set by name, searching the declared ones then the builtin table (§11.1)
        [[nodiscard]] const NamedValueSet* find_named_value_set(std::string_view name) const;

        // report whether the catalog holds a native type of either form, which is what a type name resolves (§11.2, AD-17)
        [[nodiscard]] bool holds_native(std::string_view name) const;

        // find an element type by name, which is what an element keyword resolves against (§11.1)
        [[nodiscard]] const ElementType* find(std::string_view name) const;

        // hand out every type in the catalog, which is what a diagnostic about the whole set iterates (§11.1)
        [[nodiscard]] const std::vector<ElementType>& types() const;

    private:
        // append the common properties to every type the catalog holds, which a parent arranges on (§8.2)
        void add_common_properties();

        // the known element types, which are looked up by name (§11.1)
        std::vector<ElementType> m_types;

        // the known native data types, which a snapshot may carry but which is never an element (§11.2, AD-17)
        std::vector<std::string> m_native_data;

        // the known records, which a collection holds and a repeat walks (§11.2, AD-17, AD-13)
        std::vector<RecordType> m_records;

        // the named value sets the file declared in ui, which a property may hold one member of (§11.1)
        std::vector<NamedValueSet> m_named_value_sets;
    };

    // resolve a type name as written against the catalog, which is what a property or a field names (§11.1, §11.2)
    [[nodiscard]] ValueType resolve_type_name(std::string_view written, const ElementCatalog& catalog);
} // namespace albor::compiler
