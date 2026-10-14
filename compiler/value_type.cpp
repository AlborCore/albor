#include "compiler/value_type.h"

#include <algorithm>

#include <utility>

namespace albor::compiler
{
    ValueType::ValueType() = default;

    ValueType::ValueType(TypeKind kind) :
        m_kind(kind) {}

    ValueType ValueType::parameterized(TypeKind kind, const ValueType& element) {
        // the type is assembled first, because the element has to be owned before it is stored (§11.1)
        ValueType type;
        // the kind is recorded, because it decides whether the element is read at all (§11.1)
        type.m_kind = kind;
        // the element is shared rather than copied, because a parameter type nests arbitrarily deep (§11.1)
        type.m_element = std::make_shared<const ValueType>(element);
        return type;
    }

    ValueType ValueType::native(std::string name) {
        // the type is assembled first, because the name has to be owned before it is stored (§11.2, AD-17)
        ValueType type;
        // the kind is recorded, because a native type is resolved by name rather than by shape (§11.2, AD-17)
        type.m_kind = TypeKind::Native;
        // the name is moved in, because the caller already built it (§11.2, AD-17)
        type.m_native_name = std::move(name);
        return type;
    }

    bool ValueType::is_error() const {
        // the error type is the one kind that suppresses the problems it would otherwise raise (§11.1)
        return m_kind == TypeKind::Error;
    }

    bool ValueType::is_numeric() const {
        // an integer and a float are the only numeric kinds, and nothing else takes arithmetic (§11.1)
        return m_kind == TypeKind::Int || m_kind == TypeKind::Float;
    }

    bool ValueType::is(TypeKind other) const {
        // a parameter or native form never matches a scalar kind, so the kind alone decides (§11.1)
        return m_kind == other;
    }

    bool ValueType::equals(const ValueType& other) const {
        // the kinds differ, so the types cannot be the same (§11.1)
        if (m_kind != other.m_kind)
            return false;
        // a native type is identified by its name, because two natives may share a shape (§11.2, AD-17)
        if (m_kind == TypeKind::Native)
            return m_native_name == other.m_native_name;
        // a parameter form is equal when its elements are, which recurses into the nesting (§11.1)
        if (m_kind == TypeKind::Snapshot || m_kind == TypeKind::Vector)
            return element().equals(other.element());
        // a scalar type carries nothing else, so the kind was the whole of it (§11.1)
        return true;
    }

    std::string ValueType::to_string() const {
        // the spellings are a table indexed by kind, so a new kind cannot leave a path unhandled (§11.1)
        static const char* const SPELLINGS[] = {"<error>", "void", "bool", "int", "float", "string", "color", "align", "scrollbar", "length", "Snapshot<", "vector<", ""};
        // the two parameter forms append their element, so only their prefix comes from the table (§11.1)
        if (m_kind == TypeKind::Snapshot)
            return std::string(SPELLINGS[static_cast<std::size_t>(m_kind)]) + element().to_string() + ">";
        // a vector renders the same way, with its own prefix (§11.1)
        if (m_kind == TypeKind::Vector)
            return std::string(SPELLINGS[static_cast<std::size_t>(m_kind)]) + element().to_string() + ">";
        // a native type prints by name, because that is how the manifest declares it (§11.2, AD-17)
        if (m_kind == TypeKind::Native)
            return m_native_name;
        // every remaining kind is a scalar or the error type, which the table spells (§11.1)
        return SPELLINGS[static_cast<std::size_t>(m_kind)];
    }

    TypeKind ValueType::kind() const {
        // the kind is what every rule switches on (§11.1)
        return m_kind;
    }

    const std::string& ValueType::native_name() const {
        // the name is only meaningful for a native type, and it is empty for every other form (§11.2, AD-17)
        return m_native_name;
    }

    const ValueType& ValueType::element() const {
        // a parameter form always holds an element, because that is what makes it a parameter form (§11.1)
        if (m_element != nullptr)
            return *m_element;
        // a scalar or native form has no element, so the error type stands in for it (§11.1)
        static const ValueType none;
        return none;
    }

    ValueType scalar(TypeKind kind) {
        // the scalar type is the kind alone, because a scalar carries no parameter (§11.1)
        return ValueType(kind);
    }

    ValueType error_type() {
        // the error type is the default, so it is built without naming a kind (§11.1)
        return ValueType();
    }

    bool builtin_conversion_target(std::string_view name, TypeKind& out_kind) {
        // the four conversions are a closed set, because the language defines no others (§11.1)
        if (name == "string") {
            out_kind = TypeKind::String;
            return true;
        }
        // an integer conversion parses a numeric text (§11.1)
        if (name == "int") {
            out_kind = TypeKind::Int;
            return true;
        }
        // a float conversion parses a numeric text (§11.1)
        if (name == "float") {
            out_kind = TypeKind::Float;
            return true;
        }
        // a bool conversion compares a value against empty (§11.1)
        if (name == "bool") {
            out_kind = TypeKind::Bool;
            return true;
        }
        // any other name is not a conversion, so the caller reports it (§11.1)
        return false;
    }

    bool conversion_allowed(TypeKind from, TypeKind to) {
        // a conversion from the error type is always allowed, because the error was already reported (§11.1)
        if (from == TypeKind::Error)
            return true;
        // a conversion to the error type is always allowed, because a wrong target was already reported (§11.1)
        if (to == TypeKind::Error)
            return true;
        // every scalar may become a string, which is how a value reaches a label (§11.1)
        if (to == TypeKind::String)
            return true;
        // only a number or a text may become a number, because nothing else carries digits (§11.1)
        if (to == TypeKind::Int || to == TypeKind::Float) {
            // a text or a number is what a numeric conversion reads (§11.1)
            if (from == TypeKind::Int || from == TypeKind::Float || from == TypeKind::String)
                return true;
            // anything else has no digits to convert, so the conversion is refused (§11.1)
            return false;
        }
        // a bool conversion reads any scalar, because presence is a reasonable interpretation (§11.1)
        if (to == TypeKind::Bool)
            return true;
        // no other target exists, so every remaining conversion is refused (§11.1)
        return false;
    }

    bool assignable(const ValueType& from, const ValueType& to) {
        // a value whose type could not be established assigns nowhere, but raises nothing further (§11.1)
        if (from.is_error() || to.is_error())
            return true;
        // a length is a pixel count, so it assigns into a float where the density is applied (§11.1, AD-27)
        if (from.is(TypeKind::Length) && to.is(TypeKind::Float))
            return true;
        // a collection of unknown element type assigns into any collection, because only the target knows it (§8.1)
        if (from.is(TypeKind::Vector) && to.is(TypeKind::Vector) && (from.element().is_error() || to.element().is_error()))
            return true;
        // every other assignment must match exactly, because nothing widens implicitly (§11.1)
        return from.equals(to);
    }

    const NamedValueMember* NamedValueSet::find_member(std::string_view member_name) const {
        // the members are scanned, because a set carries only a handful of them (§11.1)
        for (const NamedValueMember& member : members) {
            // the written name decides, because that is what the author wrote (§11.1)
            if (member.written == member_name)
                return &member;
        }
        // a name the set does not carry is an error the checker reports (§11.1)
        return nullptr;
    }

    namespace
    {
        // every value set the language declares, because the checker and the generator both resolve against them
        const std::vector<NamedValueSet>& named_value_sets() {
            static const std::vector<NamedValueSet> sets{
                // the alignment set is what a layout property resolves a written member against (§8.2)
                NamedValueSet{.name = "Align",
                              .kind = TypeKind::Align,
                              .members =
                                  {
                                      // the written form is lower case and the enumerator capitalised (§11.3, §8.2)
                                      NamedValueMember{.written = "start", .code = "Start"},
                                      NamedValueMember{.written = "center", .code = "Center"},
                                      NamedValueMember{.written = "end", .code = "End"},
                                      NamedValueMember{.written = "stretch", .code = "Stretch"},
                                  }},
                // the scrollbar set is what the scrollbar policy resolves a written member against (§8)
                NamedValueSet{.name = "Scrollbar",
                              .kind = TypeKind::Scrollbar,
                              .cpp_name = "ScrollbarVisibility",
                              .members =
                                  {
                                      // the written form is lower case and the enumerator capitalised (§11.3, §8)
                                      NamedValueMember{.written = "as_needed", .code = "AsNeeded"},
                                      NamedValueMember{.written = "off", .code = "AlwaysOff"},
                                      NamedValueMember{.written = "on", .code = "AlwaysOn"},
                                  }},
            };
            return sets;
        }
    } // namespace

    const NamedValueSet* find_named_value_set(std::string_view set_name) {
        // the sets are scanned, because the language declares only a few of them (§11.1)
        for (const NamedValueSet& set : named_value_sets()) {
            // the name decides, so a field read resolves to exactly one set (§11.1)
            if (set.name == set_name)
                return &set;
        }
        // a name the language does not declare is not a value set, so the checker falls through to a property read (§11.1)
        return nullptr;
    }

    std::vector<ElementProperty> common_element_properties() {
        // the alignment kind is the one a written member of the alignment set produces (§8.2)
        const ValueType alignment = scalar(TypeKind::Align);
        // every element type arranges inside a parent, so both axes are properties on all of them (§8.2)
        return {
            // the horizontal axis is a screen axis, so it means the same thing in every container (§8.2)
            ElementProperty{.name = "horizontal_align", .type = alignment, .assignable = true, .scaled_by_density = false},
            // the vertical axis is its independent counterpart, and neither one is the parent's main axis (§8.2)
            ElementProperty{.name = "vertical_align", .type = alignment, .assignable = true, .scaled_by_density = false},
        };
    }

    const ElementProperty* ElementType::find_property(std::string_view property_name) const {
        // the properties are scanned, because a type exposes a handful of them (§11.1)
        for (const ElementProperty& property : properties) {
            // the name decides, so an assignment resolves to exactly one property (§11.1)
            if (property.name == property_name)
                return &property;
        }
        // a name the type does not expose is an error the checker reports (§11.1)
        return nullptr;
    }

    const ElementSignal* ElementType::find_signal(std::string_view signal_name) const {
        // the signals are scanned, because a type raises a handful of them (§11.1)
        for (const ElementSignal& signal : signals) {
            // the name decides, so a handler resolves to exactly one signal (§11.1)
            if (signal.name == signal_name)
                return &signal;
        }
        // a name the type does not raise is an error the checker reports (§11.1)
        return nullptr;
    }

    ElementCatalog ElementCatalog::with_builtins() {
        // the catalog is assembled locally, because the builtin set is a closed list (§11.1, §8)
        ElementCatalog catalog;
        // a container groups children in a column, which is the default vertical stack (§8.2)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Column",
                                              // the gap and the inset are what §11.1 writes on a column, and both scale with the surface (§8.2, AD-27)
                                              .properties = {ElementProperty{.name = "spacing", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}, ElementProperty{.name = "padding", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a column raises no signal of its own (§8.2)
                                              .signals = {},
                                              // a column is a container, so it accepts children (§8.2)
                                              .accepts_children = true});
        // a container groups children in a row, which is the horizontal stack (§8.2)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Row",
                                              // the gap between children is the only property the language exposes (§8.2)
                                              .properties = {ElementProperty{.name = "spacing", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a row raises no signal of its own (§8.2)
                                              .signals = {},
                                              // a row is a container, so it accepts children (§8.2)
                                              .accepts_children = true});
        // a container insets its children, which is what a padded area is (§8.2)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Padding",
                                              // the inset is a length, because it scales with the surface (§8.2, AD-27)
                                              .properties = {ElementProperty{.name = "padding", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a padded area raises no signal of its own (§8.2)
                                              .signals = {},
                                              // a padded area is a container, so it accepts children (§8.2)
                                              .accepts_children = true});
        // a container stacks its children in place, which is what an overlay area is (§8.2)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Stack",
                                              // a stack exposes no property, because its children decide the extent (§8.2)
                                              .properties = {},
                                              // a stack raises no signal of its own (§8.2)
                                              .signals = {},
                                              // a stack is a container, so it accepts children (§8.2)
                                              .accepts_children = true});
        // a container lays its children out in a grid, which is what a table is (§8.2)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Grid",
                                              // the gap between cells is a length, because it scales with the surface (§8.2, AD-27)
                                              .properties = {ElementProperty{.name = "spacing", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a grid raises no signal of its own (§8.2)
                                              .signals = {},
                                              // a grid is a container, so it accepts children (§8.2)
                                              .accepts_children = true});
        // a container shows a window over content larger than itself, which scrolls (§8)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "ScrollView",
                                              // the extent and the policy and the overscan are what a view exposes (§8)
                                              .properties = {ElementProperty{.name = "viewport_width", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}, ElementProperty{.name = "viewport_height", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}, ElementProperty{.name = "overscan", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "offset", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "scrollbar", .type = scalar(TypeKind::Scrollbar), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "row_height", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = false}},
                                              // a scroll view raises when its offset moves (§8.3)
                                              .signals = {ElementSignal{.name = "scrolled", .parameter_types = {scalar(TypeKind::Float)}, .parameter_names = {"offset"}}},
                                              // a scroll view holds one content container, so it accepts children (§8.2)
                                              .accepts_children = true});
        // a leaf draws a run of text, which is a caption or a value readout (§8)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Label",
                                              // the text and the foreground and the size are what a label exposes (§8)
                                              .properties = {ElementProperty{.name = "text", .type = scalar(TypeKind::String), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "foreground", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "font_size", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a label raises no signal of its own (§8)
                                              .signals = {},
                                              // a label is a leaf, so it takes no children (§8)
                                              .accepts_children = false});
        // a leaf edits a string the user types, which is what a form field or a search box is (§8)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "TextField",
                                              // the value and the hint and the two states are what a field exposes (§8)
                                              .properties = {ElementProperty{.name = "value", .type = scalar(TypeKind::String), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "placeholder", .type = scalar(TypeKind::String), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "read_only", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "multiline", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "disabled", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "font_size", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a field raises per keystroke and on its commit (§8)
                                              .signals = {ElementSignal{.name = "changed", .parameter_types = {scalar(TypeKind::String)}, .parameter_names = {"value"}}, ElementSignal{.name = "committed", .parameter_types = {scalar(TypeKind::String)}, .parameter_names = {"value"}}},
                                              // a field is a leaf, so it takes no children (§8)
                                              .accepts_children = false});
        // a leaf performs an action when it is pressed, which is a push control (§8)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Button", // the label and the plate and the disabled state are what a button exposes (§8)
                                              .properties = {ElementProperty{.name = "text", .type = scalar(TypeKind::String), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "background", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "disabled", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}},
                                              // a button raises a click when it is pressed (§8)
                                              .signals = {ElementSignal{.name = "clicked", .parameter_types = {}}},
                                              // a button is a leaf, so it takes no children (§8)
                                              .accepts_children = false});
        // a leaf carries a continuous value the user drags (§8)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Slider",
                                              // the value and the disabled state and the two colours are what a slider exposes (§8)
                                              .properties = {ElementProperty{.name = "value", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "min", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "max", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "disabled", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "track_color", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "thumb_color", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}},
                                              // a slider raises a change when the user drags it, and names the value (§8)
                                              .signals = {ElementSignal{.name = "changed", .parameter_types = {scalar(TypeKind::Float)}, .parameter_names = {"value"}}},
                                              // a slider is a leaf, so it takes no children (§8)
                                              .accepts_children = false});
        // a leaf carries a truth value the user toggles (§8)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "CheckBox",
                                              // the checked and the disabled state and the two colours are what a checkbox exposes (§8)
                                              .properties = {ElementProperty{.name = "checked", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "disabled", .type = scalar(TypeKind::Bool), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "outline", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}, ElementProperty{.name = "check_fill", .type = scalar(TypeKind::Color), .assignable = true, .scaled_by_density = false}},
                                              // a checkbox raises a toggle with its new value (§8)
                                              .signals = {ElementSignal{.name = "toggled", .parameter_types = {scalar(TypeKind::Bool)}, .parameter_names = {"checked"}}},
                                              // a checkbox is a leaf, so it takes no children (§8)
                                              .accepts_children = false});
        // a leaf hands its surface to application code, which is custom drawing (§8, §6.4)
        catalog.m_types.push_back(ElementType{// the name the author writes after an element keyword (§11.1)
                                              .name = "Canvas",
                                              // the intrinsic size is what a canvas exposes, because its content does not measure (§8, §6.4)
                                              .properties = {ElementProperty{.name = "width", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}, ElementProperty{.name = "height", .type = scalar(TypeKind::Float), .assignable = true, .scaled_by_density = true}},
                                              // a canvas raises no signal of its own (§8, §6.4)
                                              .signals = {},
                                              // a canvas is a leaf, so it takes no children (§8, §6.4)
                                              .accepts_children = false});
        // every builtin arranges inside a parent, so the common properties are appended once (§8.2)
        catalog.add_common_properties();
        return catalog;
    }

    void ElementCatalog::add_common_properties() {
        // the common set is built once per call, because it is the same two properties on every type (§8.2)
        const std::vector<ElementProperty> common = common_element_properties();
        // every type is visited, because a native component arranges like a builtin one (§8.2, AD-17)
        for (ElementType& type : m_types) {
            // each common property is added on its own, because a manifest may already declare one of them (§11.2, AD-17)
            for (const ElementProperty& property : common) {
                // a type that already exposes the name keeps its own, because the manifest decides (§11.2, AD-17)
                if (type.find_property(property.name) == nullptr)
                    type.properties.push_back(property);
            }
        }
    }

    void ElementCatalog::add_native(const ElementType& type) {
        // a name the catalog already holds is not redefined, because a ui file would resolve two shapes (§11.2, AD-17)
        if (find(type.name) != nullptr)
            return;
        // a native type is appended, because the manifest is the single source of truth for it (§11.2, AD-17)
        m_types.push_back(type);
        // a native component arranges inside a parent like a builtin one, so it takes the common properties (§8.2, AD-17)
        add_common_properties();
    }

    void ElementCatalog::add_native_data(std::string name) {
        // a name the catalog already holds is not redefined, because a ui file would resolve two shapes (§11.2, AD-17)
        if (holds_native(name))
            return;
        // the name is appended, because the manifest is the single source of truth for it (§11.2, AD-17)
        m_native_data.push_back(std::move(name));
    }

    const RecordFieldType* RecordType::find_field(std::string_view field_name) const {
        // the fields are scanned, because a record carries only a handful of them (§11.2, AD-17)
        for (const RecordFieldType& field : fields) {
            // the name decides, so a field read resolves to exactly one field (§11.2, AD-17)
            if (field.name == field_name)
                return &field;
        }
        // a field the record does not declare is a report the checker raises (§11.2, AD-17)
        return nullptr;
    }

    const RecordFieldType* RecordType::key_field() const {
        // the fields are scanned, because exactly one of them carries the identity (§18.1, AD-13)
        for (const RecordFieldType& field : fields) {
            // the flagged field is the key, which is what a row is keyed by (§18.1, AD-13)
            if (field.is_key)
                return &field;
        }
        // a record with no key cannot be keyed, which the manifest validator already reported (§11.2, AD-18)
        return nullptr;
    }

    void ElementCatalog::add_record(const RecordType& record) {
        // a record the catalog already holds is not redefined, because a ui file would resolve two shapes (§11.2, AD-17)
        if (find_record(record.name) != nullptr)
            return;
        // the record is appended, because the manifest is the single source of truth for it (§11.2, AD-17, AD-13)
        m_records.push_back(record);
    }

    const RecordType* ElementCatalog::find_record(std::string_view name) const {
        // the records are scanned, because a component names only a handful of them (§11.2, AD-17)
        for (const RecordType& record : m_records) {
            // the name decides, so a vector element type resolves to exactly one record (§11.2, AD-17)
            if (record.name == name)
                return &record;
        }
        // a record the manifest never declared is a report the checker raises (§11.2, AD-17)
        return nullptr;
    }

    bool ElementCatalog::holds_native(std::string_view name) const {
        // a data type is not an element, so the element lookup does not find it (§11.2, AD-17)
        return std::find(m_native_data.begin(), m_native_data.end(), name) != m_native_data.end();
    }

    const ElementType* ElementCatalog::find(std::string_view name) const {
        // the types are scanned, because a component names only a handful of them (§11.1)
        for (const ElementType& type : m_types) {
            // the name decides, so an element keyword resolves to exactly one type (§11.1)
            if (type.name == name)
                return &type;
        }
        // a name the catalog does not hold is an error the checker reports (§11.1)
        return nullptr;
    }

    const std::vector<ElementType>& ElementCatalog::types() const {
        // the set is handed out as it stands, because a caller only reads it (§11.1)
        return m_types;
    }
} // namespace albor::compiler
