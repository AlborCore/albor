#include "compiler/codegen/codegen.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "compiler/ast.h"
#include "compiler/value_type.h"

namespace albor::compiler
{
    namespace
    {
        // a number of bytes into a source file, which is what a span end points at
        bool ends_with(std::string_view text, std::string_view suffix) {
            // a suffix longer than the text cannot match, so the comparison is skipped (§11.3)
            if (suffix.size() > text.size())
                return false;
            // the tail of the text is compared, because that is what the suffix covers (§11.3)
            return text.substr(text.size() - suffix.size()) == suffix;
        }

        // turn a component name into the file base name, which decides what the generator emits (§14.1)
        std::string snake_case(std::string_view name) {
            // the letters accumulate, with an underscore inserted at each word boundary (§14.1)
            std::string out;
            // each letter is walked, because an upper case letter starts a word (§14.1)
            for (std::size_t index = 0; index < name.size(); index++) {
                // the letter is read once, so the checks below look at one byte (§14.1)
                const char letter = name[index];
                // a word starts after lower text or a digit, except inside a run of capitals (§14.1)
                const bool upper = letter >= 'A' && letter <= 'Z';
                // an upper letter is only a boundary when one exists before it (§14.1)
                if (upper && index != 0) {
                    // the previous letter decides whether this one starts a word (§14.1)
                    const char previous = name[index - 1];
                    // lower text or a digit always ends the word before (§14.1)
                    const bool after_text = (previous >= 'a' && previous <= 'z') || (previous >= '0' && previous <= '9');
                    // a capital before a lower letter splits an acronym, which keeps the tail a word (§14.1)
                    const bool split_acronym = previous >= 'A' && previous <= 'Z' && index + 1 < name.size() && name[index + 1] >= 'a' && name[index + 1] <= 'z';
                    // the boundary exists after text or before the tail of an acronym (§14.1)
                    if (after_text || split_acronym)
                        out += '_';
                }
                // the letter is lowered, because the file base is lower case (§14.1)
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
            }
            return out;
        }

        // the reserved C++ names a generated accessor may not shadow, which receive an underscore (§14.1)
        const std::unordered_set<std::string>& reserved_names() {
            // the set is built once, because the reserved list is a fixed corpus (§14.1)
            static const std::unordered_set<std::string> names{"alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq", "parent", "children", "child_count", "child_at", "add_child", "insert_child", "remove_child", "measure", "arrange", "render", "hit_test", "dispatch_event", "bounds", "measured_size", "invalidate", "invalidate_layout", "render_dirty", "layout_dirty", "clear_dirty", "visible", "set_visible", "set_invalidation_handler", "identity", "set_explicit_id", "set_node_id", "visit", "engine", "property_base", "property_count", "preferred_size", "background"};

            return names;
        }

        // pick a C++ name for a written identifier, which appends an underscore when it clashes (§14.1)
        std::string sanitize(std::string_view name) {
            // the name is copied, because the clash check may need to grow it (§14.1)
            std::string out(name);
            // a reserved name appends the underscore, which keeps the facade unambiguous (§14.1)
            if (reserved_names().contains(out))
                out += '_';
            return out;
        }

        // render a colour as the constructor call, which scales the channels into the unit range (§11.3)
        std::string shortest_float(double value) {
            // the buffer is generous, because a long exponent still fits with room (§11.3)
            char buffer[64];
            // the precision grows until the text reads back to the value, which is the shortest round trip (§11.3)
            for (int precision = 1; precision <= 9; precision++) {
                // the text is rendered at the tested precision (§11.3)
                std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
                // a precision that reads back to the value is the one kept (§11.3)
                if (std::strtod(buffer, nullptr) == value)
                    break;
            }
            // the rendered text is finished below, so a copy is taken (§11.3)
            std::string text(buffer);
            // a float literal needs a dot or an exponent, which a whole number does not carry (§11.3)
            if (text.find('.') == std::string::npos && text.find('e') == std::string::npos && text.find('E') == std::string::npos)
                text += ".0";
            // the f suffix makes it a float literal, which is what the api takes (§11.3)
            text += "f";
            return text;
        }

        // read one hex digit as its value, which a channel pair assembles from (§11.3)
        unsigned int nibble(char digit) {
            // a decimal digit carries its own value (§11.3)
            if (digit >= '0' && digit <= '9')
                return static_cast<unsigned int>(digit - '0');
            // a letter carries the value the alphabet gives it, which the caller has already lowered (§11.3)
            return static_cast<unsigned int>(digit - 'a' + 10);
        }

        // read one pair of hex digits as a byte, which is one colour channel (§11.3)
        unsigned int hex_pair(char high, char low) {
            // the value is assembled from the two nibbles (§11.3)
            return nibble(static_cast<char>(std::tolower(static_cast<unsigned char>(high)))) * 16 + nibble(static_cast<char>(std::tolower(static_cast<unsigned char>(low))));
        }

        // read a numeric literal spelling into its value, which handles the hex float form (§11.3)
        double parse_number(std::string_view text) {
            // a hexadecimal spelling is parsed in base sixteen, because the literal says so (§11.3)
            if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                // the integer and fractional parts accumulate in base sixteen (§11.3)
                double value = 0.0;
                // the fraction denominator grows by sixteen, because each digit is a negative power (§11.3)
                double scale = 16.0;
                // whether the scan is on the fractional side, which follows the dot (§11.3)
                bool fractional = false;
                // the digits of the literal are walked, because the whole is read by hand (§11.3)
                for (std::size_t index = 2; index < text.size(); index++) {
                    // the dot moves the scan to the fractional side (§11.3)
                    if (text[index] == '.') {
                        fractional = true;
                        continue;
                    }
                    // the digit value is resolved through its code point (§11.3)
                    const double digit_value = static_cast<double>(nibble(static_cast<char>(std::tolower(static_cast<unsigned char>(text[index])))));
                    // an integer digit multiplies the accumulator, which places it (§11.3)
                    if (!fractional)
                        value = value * 16.0 + digit_value;
                    // a fractional digit divides, which places it after the dot (§11.3)
                    else {
                        value += digit_value / scale;
                        scale *= 16.0;
                    }
                }
                return value;
            }
            // a decimal spelling parses through the runtime, which reads the exponent too (§11.3)
            return std::strtod(std::string(text).c_str(), nullptr);
        }

        // render a decoded string as a std::string constructor, which the codegen emits everywhere (§11.3)
        std::string emit_string_literal(std::string_view text) {
            // the result starts with the constructor, because a binding target is a std::string (§11.3)
            std::string out = "std::string(\"";
            // the raw bytes are escaped, because control and quote characters cannot be written bare (§11.3)
            for (const char byte : text) {
                // a backslash doubles, which keeps the C++ string identical (§11.3)
                if (byte == '\\') {
                    out += "\\\\";
                    continue;
                }
                // a quote escapes, so it does not close the literal (§11.3)
                if (byte == '"') {
                    out += "\\\"";
                    continue;
                }
                // a newline is written escaped, which keeps the generated file on one line (§11.3)
                if (byte == '\n') {
                    out += "\\n";
                    continue;
                }
                // a tab is written escaped, which keeps the generated file printable (§11.3)
                if (byte == '\t') {
                    out += "\\t";
                    continue;
                }
                // a non printable byte is written as hex, because it has no visible form (§11.3)
                if (static_cast<unsigned char>(byte) < 0x20) {
                    char hex[8];
                    std::snprintf(hex, sizeof(hex), "\\x%02X", static_cast<unsigned char>(byte));
                    out += hex;
                    continue;
                }
                // every other byte is copied as written, which is what utf8 needs (§11.3)
                out += byte;
            }
            // the constructor closes, which completes the literal (§11.3)
            out += "\")";
            return out;
        }

        // render a colour literal as a constructor call, which scales the channels into the unit range (§11.3)
        std::string emit_color_literal(std::string_view digits) {
            // each two digit pair is one channel, which is read in hex (§11.3)
            const unsigned int red = hex_pair(digits[0], digits[1]);
            // the second channel follows the first, which is the written order (§11.3)
            const unsigned int green = hex_pair(digits[2], digits[3]);
            // the third channel follows the second, which is the written order (§11.3)
            const unsigned int blue = hex_pair(digits[4], digits[5]);
            // an eight digit spelling carries an alpha channel, which the short form does not (§11.3)
            if (digits.size() == 8) {
                // the alpha is the fourth pair, which sits at the end (§11.3)
                const unsigned int alpha = hex_pair(digits[6], digits[7]);
                // the long form calls rgba with all four channels (§11.3)
                return "Color::rgba(" + shortest_float(red / 255.0) + ", " + shortest_float(green / 255.0) + ", " + shortest_float(blue / 255.0) + ", " + shortest_float(alpha / 255.0) + ")";
            }
            // the short form calls rgb with the three opaque channels (§11.3)
            return "Color::rgb(" + shortest_float(red / 255.0) + ", " + shortest_float(green / 255.0) + ", " + shortest_float(blue / 255.0) + ")";
        }

        // the C++ name of a checked type, which decides what the generated code takes and returns (§11.3)
        std::string cpp_type(const ValueType& type) {
            // each scalar kind maps to one C++ type, which is what the framework uses (§11.3)
            switch (type.kind()) {
            case TypeKind::Bool:
                return "bool";
            case TypeKind::Int:
                return "int";
            case TypeKind::Float:
                return "float";
            case TypeKind::Length:
                return "float";
            case TypeKind::String:
                return "std::string";
            case TypeKind::Color:
                return "Color";
            case TypeKind::Align:
                // the language spells alignment as the framework enumeration, which the generated code names (§11.3)
                return "Align";
            case TypeKind::Scrollbar:
                // the policy set renders as the enumeration the viewport takes, which names itself (§11.3, §8)
                return "ScrollbarVisibility";
            case TypeKind::Vector:
                // a collection becomes the standard vector over its element, which recurses into a nested one (§8.1)
                return "std::vector<" + cpp_type(type.element()) + ">";
            case TypeKind::Native:
                // a record and a data type are both spelled by the name the manifest declared (§11.2, AD-17)
                return type.native_name();
            case TypeKind::Error:
                return "std::string";
            default:
                // a snapshot is still blocked before it reaches here, because its element is a data type (§11.3)
                return "std::string";
            }
        }

        // report whether a type carries a collection at any depth, which decides whether the header is needed (§8.1)
        bool carries_vector(const ValueType& type) {
            // a collection carries one at its own depth, and its element is where a nested one hides (§8.1)
            if (type.is(TypeKind::Vector))
                return true;
            // a parameter form other than a collection has no element to look inside (§11.1)
            if (type.kind() != TypeKind::Snapshot)
                return false;
            // the element is walked, because a snapshot may carry a collection of any depth (§8.1)
            return carries_vector(type.element());
        }

        // the C++ spelling a read accessor returns, which is a reference for a large value and a copy otherwise (§14.1)
        std::string read_return_type(const ValueType& type) {
            // a string and a collection are both large enough that a copy on every read would be a cost (§14.1)
            if (type.is(TypeKind::String) || type.is(TypeKind::Vector))
                return "const " + cpp_type(type) + "&";
            // every other value is cheap enough to return by value, which keeps the signature simple (§14.1)
            return cpp_type(type);
        }

        // the kind of the target a property name maps to, which is the source of the C++ setter (§11.3)
        struct WidgetInfo
        {
            // the header that declares the class, which the generated file includes (§11.3)
            std::string_view header;

            // the C++ class the generated code constructs, which is unqualified in the framework (§11.3)
            std::string_view class_name;

            // whether constructing the class needs the binding engine (§11.3)
            bool needs_engine = false;

            // whether constructing the class needs the text engine (§11.3)
            bool needs_text_engine = false;
        };

        // the builtin element set, which is what the codegen can construct (§11.3)
        const std::map<std::string_view, WidgetInfo>& widget_info() {
            // the table is built once, because the builtin set is closed (§11.3)
            static const std::map<std::string_view, WidgetInfo> table{
                {"Column", {.header = "layout/column_layout.h", .class_name = "ColumnLayout", .needs_engine = false, .needs_text_engine = false}}, {"Row", {.header = "layout/row_layout.h", .class_name = "RowLayout", .needs_engine = false, .needs_text_engine = false}}, {"Padding", {.header = "layout/padding_layout.h", .class_name = "PaddingLayout", .needs_engine = false, .needs_text_engine = false}}, {"Stack", {.header = "layout/stack_layout.h", .class_name = "StackLayout", .needs_engine = false, .needs_text_engine = false}}, {"Grid", {.header = "layout/grid_layout.h", .class_name = "GridLayout", .needs_engine = false, .needs_text_engine = false}}, {"Label", {.header = "widgets/label.h", .class_name = "Label", .needs_engine = true, .needs_text_engine = true}}, {"Button", {.header = "widgets/button.h", .class_name = "Button", .needs_engine = true, .needs_text_engine = true}}, {"Slider", {.header = "widgets/slider.h", .class_name = "Slider", .needs_engine = true, .needs_text_engine = false}}, {"CheckBox", {.header = "widgets/checkbox.h", .class_name = "CheckBox", .needs_engine = true, .needs_text_engine = false}}, {"Canvas", {.header = "widgets/canvas.h", .class_name = "Canvas", .needs_engine = true, .needs_text_engine = false}}, {"ScrollView", {.header = "widgets/scroll_view.h", .class_name = "ScrollView", .needs_engine = true, .needs_text_engine = false}}, {"TextField", {.header = "widgets/text_field.h", .class_name = "TextField", .needs_engine = true, .needs_text_engine = true}},
            };
            return table;
        }

        // the setter a property assignment maps to, keyed by the element and the property (§11.3)
        std::string_view setter_of(std::string_view element, std::string_view property) {
            // the composite key is assembled, because the map is keyed by the pair (§11.3)
            const std::string key = std::string(element) + "." + std::string(property);
            // the property to setter map is the closed catalogue of §8 (§11.3)
            static const std::unordered_map<std::string, std::string_view> setters{
                {"Column.spacing", "set_gap"}, {"Column.padding", "set_padding"}, {"Row.spacing", "set_gap"}, {"Padding.padding", "set_padding"}, {"Grid.spacing", "set_gap"}, {"Label.text", "set_text"}, {"Label.foreground", "set_foreground"}, {"Label.font_size", "set_font_size"}, {"Button.text", "set_label"}, {"Button.background", "set_background"}, {"Button.disabled", "set_disabled"}, {"Slider.value", "set_value"}, {"Slider.disabled", "set_disabled"}, {"Slider.track_color", "set_track_color"}, {"Slider.thumb_color", "set_thumb_color"}, {"CheckBox.checked", "set_checked"}, {"CheckBox.disabled", "set_disabled"}, {"CheckBox.outline", "set_outline"}, {"CheckBox.check_fill", "set_check_fill"}, {"ScrollView.overscan", "set_overscan"}, {"ScrollView.scrollbar", "set_scrollbar_visibility"}, {"ScrollView.row_height", "set_row_height"}, {"TextField.value", "set_value"}, {"TextField.placeholder", "set_placeholder"}, {"TextField.read_only", "set_read_only"}, {"TextField.multiline", "set_multiline"}, {"TextField.disabled", "set_disabled"}, {"TextField.font_size", "set_font_size"},
            };
            // the lookup is a ready made key, because the pair is the identity (§11.3)
            const auto found = setters.find(key);
            // a property every type carries has no per type entry, because it resolves against the common set (§8.2)
            if (found != setters.end())
                return found->second;
            // the common set decides, because alignment is a property of every node a parent arranges (§8.2)
            static const std::vector<std::pair<std::string, std::string>> common_setters = [] {
                // the names come from the catalogue, held apart so a returned view outlives this call (§8.2, §11.3)
                std::vector<std::pair<std::string, std::string>> built;
                for (const ElementProperty& common : common_element_properties())
                    built.emplace_back(common.name, "set_" + common.name);
                return built;
            }();
            for (const auto& entry : common_setters) {
                // the name decides, so a common property resolves to exactly one setter (§8.2)
                if (entry.first == property)
                    return std::string_view(entry.second);
            }
            // an element with no setter carries none, which only the canvas reaches (§11.3)
            return std::string_view{};
        }

        // the callback setter an element signal wires to, keyed by the element and the signal (§11.3)
        std::string_view signal_setter_of(std::string_view element, std::string_view signal) {
            // the composite key is assembled, because the map is keyed by the pair (§11.3)
            const std::string key = std::string(element) + "." + std::string(signal);
            // the signal to setter map is the closed catalogue of §8 (§11.3)
            static const std::unordered_map<std::string, std::string_view> setters{
                {"Button.clicked", "set_on_click"}, {"Slider.changed", "set_on_change"}, {"CheckBox.toggled", "set_on_toggle"}, {"ScrollView.scrolled", "set_on_scroll"}, {"TextField.changed", "set_on_change"}, {"TextField.committed", "set_on_commit"},
            };
            // the lookup is a ready made key, because the pair is the identity (§11.3)
            const auto found = setters.find(key);
            // an element with no signal carries none, which only the three widgets reach (§11.3)
            return found == setters.end() ? std::string_view{} : found->second;
        }

        // the named parameters an element signal carries, which a generated lambda and its body both take (§11.3)
        std::vector<std::pair<std::string, std::string>> named_signal_params(std::string_view element, std::string_view signal) {
            // a slider change carries the new value, and the catalog names it (§11.3)
            if (element == "Slider" && signal == "changed")
                return {{"float", "value"}};
            // a checkbox toggle carries the new state, which the catalog also names (§11.3)
            if (element == "CheckBox" && signal == "toggled")
                return {{"bool", "checked"}};
            // a scroll reports the offset it moved to, which is the one value a viewport raises (§8.3)
            if (element == "ScrollView" && signal == "scrolled")
                return {{"float", "offset"}};
            // a field raises the string it changed to, which a validation and a commit both read (§8)
            if (element == "TextField" && (signal == "changed" || signal == "committed"))
                return {{"std::string", "value"}};
            // a button click carries nothing, so its handler takes no parameter (§11.3)
            return {};
        }

        // the signature fragment a handler takes after the context, which carries the raised parameters (§11.3)
        std::string handler_signature(std::string_view element, std::string_view signal) {
            // the named parameters decide the fragment, because the catalog declares them (§11.3)
            std::string fragment;
            for (const std::pair<std::string, std::string>& parameter : named_signal_params(element, signal))
                fragment += ", " + parameter.first + " " + parameter.second;
            return fragment;
        }

        // the emitter for one component, which produces the facade header and its implementation unit (§11.3)
        class ComponentEmitter
        {
        public:
            // the node slot that has no parent, which is what the root records (§8.1)
            static constexpr std::size_t NO_PARENT = static_cast<std::size_t>(-1);

            // one tree node in the flat build order, which the emitter records as it walks (§8.1)
            struct NodeSlot
            {
                // the preorder index, which names the member and the locals of the node (§8.1)
                std::size_t index = 0;

                // the checked node being emitted, which carries the assignments and handlers (§8.1)
                const CheckedNode* node = nullptr;

                // the index of the node that owns this one, which is the adoption target (§8.1)
                std::size_t parent = 0;

                // the child positions from the row root, which is how an update reaches this node (§18.1, AD-13)
                std::vector<std::size_t> row_path;
            };

            // one repeat and the body it materializes per item, which is what a keyed view owns (§18.1, AD-13)
            struct RepeatSlot
            {
                // the preorder index, which names the view member and the three row functions (§18.1)
                std::size_t index = 0;

                // the repeat node being emitted, which carries the source and the bindings (§8.1)
                const CheckedNode* node = nullptr;

                // the index of the container that holds this repeat, which is where its rows hang (§8.1, §18.1)
                std::size_t parent = 0;

                // the body nodes in preorder, which are the nodes one row is made of (§8.1, §18.1)
                std::vector<NodeSlot> body;

                // the child position the first row takes, which keeps rows where the repeat was written (§18.1)
                std::size_t offset = 0;

                // whether the repeat hangs directly under a viewport, which decides a windowed view (§8.2, AD-13)
                bool virtualized = false;
            };

            // build the emitter over the component and the shared problem list (§11.3)
            ComponentEmitter(const CheckedComponent& component, DiagnosticList& diagnostics, const ElementCatalog* catalog, std::vector<std::string> modules) :
                m_component(component), m_diagnostics(diagnostics), m_class(component.name), m_stem(snake_case(component.name)), m_catalog(catalog), m_modules(std::move(modules)) {}

            // generate the two files of the component, which is empty when a problem blocked it (§11.3)
            bool emit();

            // hand out the emitted header, which the caller places under the output directory (§14.2)
            [[nodiscard]] GeneratedFile take_header() { return {.path = m_stem + ".h", .content = std::move(m_header)}; }

            // hand out the emitted implementation, which the caller places under the output directory (§14.2)
            [[nodiscard]] GeneratedFile take_source() { return {.path = m_stem + ".cpp", .content = std::move(m_source)}; }

        private:
            // append one line to the output currently being built, which the emitting methods share (§14.1)
            void line(int depth, std::string_view text) {
                // the indent grows by four spaces per level, which is the tree convention (§14.1)
                m_output->append(static_cast<std::size_t>(depth) * 4, ' ');
                // the text is appended whole, because no generated line ever wraps (§14.1)
                m_output->append(text);
                // every generated line ends with a newline, which is what a source file expects (§14.1)
                m_output->push_back('\n');
            }

            // report every reason the component cannot be generated, which blocks the file (§11.3)
            void validate();

            // walk one node for the blocking conditions, which reports every problem it finds (§11.3)
            void validate_node(const CheckedNode& node);

            // reject a type codegen cannot translate, which is a native or a collection type (§11.3)
            void validate_type(const ValueType& type, const SourceSpan& span);

            // walk one expression for physical lengths, which have no runtime density yet (§11.3, AD-27)
            void scan_expression(const Expression& expression);

            // walk one expression for colour use, which decides the paint and print includes (§11.3)
            void scan_color(const Expression& expression);

            // collect the nodes in preorder, which is the order the build function creates them (§8.1)
            void collect(const CheckedNode& node, std::size_t parent);

            // find a declared property by name, which the type walk resolves against (§11.3)
            const CheckedProperty* property_named(std::string_view name) const;

            // the type of a checked expression, which mirrors the checker's own walk (§11.3)
            ValueType type_of(const Expression& expression) const;

            // render the facade header, which is the stable interface of the component (§14.1, AD-03)
            void emit_header();

            // render the implementation unit, which the facade hides behind its header (§14.1, AD-03)
            void emit_source();

            // render the body struct, which owns the properties and the widgets (§14.1)
            void emit_body();

            // render the constructor and the destructor, which assemble the body once (§14.1)
            void emit_lifecycle();

            // render the property accessors and the signal slots, which is the public surface (§14.1)
            void emit_accessors();

            // render the body builder, which installs the tree and the bindings (§14.1)
            void emit_build();

            // render one node block of the builder, which creates and configures the node (§14.1)
            void emit_node_block(const NodeSlot& slot);

            // render the evaluators of the bound assignments, which the sinks run (§13.3)
            void emit_evaluators();

            // render the evaluators of the bound defaults, which the properties run (§13.3)
            void emit_computes();

            // render the handlers, which are the written bodies behind the callbacks (§11.3)
            void emit_handlers();

            // emit the drawing bodies a canvas wrote, which the builder wires to set_draw (§6.4)
            void emit_draw_bodies();

            // render the colour to text helper, which the string conversion needs (§11.3)
            void emit_color_helper();

            // emit the float printing helper, which a readout reaches (§11.3)
            void emit_float_helper();

            // collect the manifest headers the given type needs, which is where a record struct is declared (§11.2, AD-17)
            void collect_record_headers(const ValueType& type);

            // walk the body of a repeat into its own list, recording how a node is reached from the root (§18.1, AD-13)
            void collect_body(const CheckedNode& node, std::size_t parent, std::vector<NodeSlot>& out, std::vector<std::size_t> path);

            // emit the body members a repeat needs, which is the view and the three row functions (§18.1, AD-13)
            void emit_repeat_members();

            // emit the row functions of every repeat, which are appended once the tree is known (§18.1, AD-13)
            void emit_repeat_row_functions_all();

            // give each repeat the child position its first row takes, because rows keep the written place (§18.1)
            void assign_repeat_offsets();

            // render the source a repeat walks, which the evaluator reads (§8.1, §18.1)
            std::string repeat_source_expression(const RepeatSlot& slot);

            // collect the properties a repeat and its rows read, because a change to any of them reconciles (§18.1, AD-13)
            std::vector<std::string> repeat_dependencies(const RepeatSlot& slot);

            // emit the three row functions of one repeat, which build, update and key a row (§18.1, AD-13)
            void emit_repeat_row_functions(const RepeatSlot& slot);

            // emit the construction of one row body node, whose locals belong to the row (§18.1, AD-13)
            void emit_row_node_block(const NodeSlot& slot);

            // emit the rewrite of one row body node, which walks from the row root to reach it (§18.1, AD-13)
            void emit_row_update_block(const NodeSlot& slot);

            // emit the reconcile call of one repeat, which materializes the rows its source carries (§18.1, AD-13)
            void emit_repeat_reconcile(const RepeatSlot& slot);

            // emit the reconcile statement itself, which the builder and the source binding both call (§18.1, AD-13)
            void reconcile(const RepeatSlot& slot);

            // emit the walk that reaches one body node from the row root, which is how an update finds it (§18.1, AD-13)
            void emit_row_walk(const NodeSlot& slot);

            // the C++ type of the repeated item, which is the element of the source collection (§8.1, §18.1)
            std::string item_type(const RepeatSlot& slot) const;

            // the manifest field carrying an item's identity, which is null when the element is not a record (§18.1)
            [[nodiscard]] const RecordFieldType* item_key_field(const RepeatSlot& slot) const;

            // the element type a repeat walks, which is the collection element the checker resolved (§8.1, §18.1)
            [[nodiscard]] ValueType repeat_element_type(const CheckedNode& node) const;

            // the manifest field carrying an item's identity, which is what a row is keyed by (§18.1, AD-13)
            [[nodiscard]] const RecordFieldType* item_key_field_for(const CheckedNode& node) const;

            // render one expression as C++, which the codegen re-emits the written shape (§11.3)
            std::string emit_expression(const Expression& expression);

            // render one literal, which the value decides (§11.3)
            std::string emit_literal(const LiteralExpression& literal);

            // render one bracketed list as a braced initialiser, whose element type the checker decided (§8.1)
            std::string emit_array(const ArrayExpression& array);

            // render one conversion, which the operand type decides (§11.3)
            std::string emit_conversion(const ConversionExpression& conversion);

            // emit a colour operation, which is a method call on the colour it was given (§6.1)
            std::string emit_call(const CallExpression& call);

            // join the dependency ids as a comma separated list, which a deps array holds (§13.3)
            std::string dep_list(const std::vector<std::string>& dependencies) const;

            // render one binary application, which is parenthesised as written (§11.3)
            std::string emit_binary(const BinaryExpression& binary);

            // render one unary application, which is parenthesised as written (§11.3)
            std::string emit_unary(const UnaryExpression& unary);

            // the component being generated, which the whole output reflects (§11.3)
            const CheckedComponent& m_component;

            // the shared problem list, which reports what blocked the component (§11.3)
            DiagnosticList& m_diagnostics;

            // the generated class name, which is the component name (§11.3)
            std::string m_class;

            // the generated file base, which the two file names share (§11.3)
            std::string m_stem;

            // the output currently being built, which the line helper appends to (§14.1)
            std::string* m_output = nullptr;

            // the assembled header, which the take methods hand out (§14.1)
            std::string m_header;

            // the assembled implementation, which the take methods hand out (§14.1)
            std::string m_source;

            // the tree nodes in build order, which the builder walks once (§8.1)
            std::vector<NodeSlot> m_nodes;

            // the C++ names of the declared properties and signals, keyed by the written names (§14.1)
            std::unordered_map<std::string, std::string> m_names;

            // the signal parameters the handler body reads, which are the lambda arguments (§11.3)
            std::vector<std::string> m_signal_parameters;

            // whether the body being emitted is a draw block, which is what exposes the canvas extent (§6.4)
            bool m_draw_extent = false;

            // the object the generated expressions read through, which is self in the functions (§13.3)
            std::string m_object;

            // whether any node constructs through the engine, which the builder then resolves (§7.2)
            bool m_uses_engine = false;

            // whether any node constructs through the text engine, which the body then stores (§9.1)
            bool m_uses_text_engine = false;

            // whether any emission uses a colour, which decides the headers to include (§11.3)
            bool m_uses_color = false;
            // whether any alignment value reaches the generated units, which the align header follows (§14.1, §8.2)
            bool m_uses_align = false;

            // whether any property carries a collection, which decides the vector header (§8.1)
            bool m_uses_vector = false;

            // the manifest headers the component needs, which the glue generator wrote from the same source (§11.2, AD-17)
            std::vector<std::string> m_record_headers;

            // the catalog the records resolve against, which is null when the caller registered none (§11.2, AD-17)
            const ElementCatalog* m_catalog = nullptr;

            // the manifest modules the file imported, which name the directories the record headers live in (§11.1, AD-17)
            std::vector<std::string> m_modules;

            // the repeats in preorder, each of which is a keyed view over its source (§18.1, AD-13)
            std::vector<RepeatSlot> m_repeats;

            // the next index a body node takes, which keeps row node names unique across repeats (§8.1, §18.1)
            std::size_t m_body_count = 0;

            // the name the item is bound to inside a row body, which is what an expression renders as (§11.1, §18.1)
            std::string m_item;

            // the name the index is bound to inside a row body, which is what an expression renders as (§11.1, §18.1)
            std::string m_index;

            // the author name of the item binding, which decides whether a reference is the row parameter (§11.1, §18.1)
            std::string m_repeat_item_name;

            // the author name of the index binding, which decides whether a reference is the row position (§11.1, §18.1)
            std::string m_repeat_index_name;

            // whether the component translated anything, which is why the catalogue header is pulled (§9.2, §11.3)
            bool m_uses_i18n = false;

            // whether any node is a canvas, which adds the geometry header (§11.3)
            bool m_uses_canvas = false;

            // whether a colour needs the text helper, which adds the cstdio header (§11.3)
            bool m_uses_color_helper = false;

            // whether a float renders as text, which the shortest form helper answers (§11.3)
            bool m_uses_float_helper = false;

            // whether the component declares signals, which adds the slot types (§8.3)
            bool m_has_signals = false;

            // whether a blocking condition was reported, which empties the output (§11.3)
            bool m_blocked = false;
        };

        void ComponentEmitter::collect_record_headers(const ValueType& type) {
            // a record is named by its own name, which the glue generator turned into a snake case file (§11.2, AD-17)
            if (type.is(TypeKind::Native)) {
                // the catalog is what distinguishes a record from a plain data type, because both are native (§11.2)
                if (m_catalog == nullptr || m_catalog->find_record(type.native_name()) == nullptr)
                    return;
                // each imported module is searched, because the record lives in exactly one of them (§11.1, §11.2, AD-17)
                for (const std::string& module : m_modules) {
                    // the header path is the module and the snake cased record, which is what the generator wrote (§11.2)
                    const std::string header = module + "/" + snake_case(type.native_name()) + ".h";
                    // a header already collected is not repeated, because the include is written once (§14.1)
                    if (std::find(m_record_headers.begin(), m_record_headers.end(), header) == m_record_headers.end())
                        m_record_headers.push_back(header);
                    // the first module that declares the record wins, because a name resolves to one module (§11.2, AD-17)
                    if (m_catalog->find_record(type.native_name()) != nullptr)
                        break;
                }
                return;
            }
            // a parameter form carries its element inside, which is where a nested record hides (§8.1, §11.2)
            if (type.kind() == TypeKind::Vector || type.kind() == TypeKind::Snapshot)
                collect_record_headers(type.element());
        }

        void ComponentEmitter::validate() {
            // the property types are rejected first, because every other pass reads them (§11.3)
            for (const CheckedProperty& property : m_component.properties) {
                // a collection anywhere in a declared type pulls the vector header into the facade (§8.1, §14.1)
                if (carries_vector(property.type))
                    m_uses_vector = true;
                // a record anywhere in a declared type pulls the manifest header that declares its struct (§11.2, AD-17)
                collect_record_headers(property.type);
                validate_type(property.type, property.span);
            }
            // the signal parameter types are rejected with them, because the facade signatures need them (§11.3)
            for (const CheckedSignal& signal : m_component.signals) {
                // each parameter is rejected on its own, because one bad type must not hide the rest (§11.3)
                for (const ValueType& parameter : signal.parameter_types) {
                    // a collection parameter is spelled in the facade signature, so the header is needed (§8.1, §14.1)
                    if (carries_vector(parameter))
                        m_uses_vector = true;
                    // a record parameter is spelled in the facade signature, so its manifest header is needed (§11.2)
                    collect_record_headers(parameter);
                    validate_type(parameter, signal.span);
                }
            }
            // each default is scanned for physical lengths, because their density is undefined (§11.3, AD-27)
            for (const CheckedProperty& property : m_component.properties) {
                // a default is scanned only when one exists, which an empty default skips (§11.3, AD-27)
                if (property.has_default)
                    scan_expression(*property.default_value);
            }
            // the tree is walked, which finds a repeat, a native element or a physical length (§11.3)
            if (m_component.root != nullptr)
                validate_node(*m_component.root);
        }

        void ComponentEmitter::validate_node(const CheckedNode& node) {
            // a repeat materializes keyed rows, so it is checked against what a row identity needs (§18.1, AD-13)
            if (node.kind == TreeNodeKind::Repeat) {
                // the source element must be a record, because a row is keyed by a declared field (§18.1, AD-13)
                if (item_key_field_for(node) == nullptr) {
                    // the problem points at the repeat, because that is what cannot be keyed (§18.1, AD-13)
                    m_diagnostics.add(Diagnostic::Severity::Error, node.span, "E0503", "a repeat needs a record element, because a row is keyed by a field the manifest marks (§18.1, AD-13)");
                    // the blocking flag is set, which empties the generated output (§11.3)
                    m_blocked = true;
                    return;
                }
                // the body is validated as a tree of its own, because a row is made of those nodes (§8.1, §18.1)
                if (node.repeat_body != nullptr)
                    validate_node(*node.repeat_body);
                // the children are walked, because a nested problem matters just as much (§11.3)
                for (const std::shared_ptr<CheckedNode>& child : node.children)
                    validate_node(*child);
                return;
            }
            // an element outside the widget set is native, which has no generated construction path (§11.3, AD-17)
            if (!widget_info().contains(std::string_view(node.element_type))) {
                // the problem names the type, because that is what the author wrote (§11.3)
                m_diagnostics.add(Diagnostic::Severity::Error, node.span, "E0501", "'" + node.element_type + "' is not a generated element type");
                // the blocking flag is set, which empties the generated output (§11.3)
                m_blocked = true;
            }
            // each assignment is scanned for its type and its expressions (§11.3)
            for (const CheckedAssignment& assignment : node.assignments) {
                // the value type is rejected, because a native or a collection value cannot be built (§11.3)
                validate_type(assignment.type, assignment.span);
                // the value is scanned for physical lengths, because their density is undefined (§11.3, AD-27)
                scan_expression(*assignment.value);
                // the value is scanned for colour use, which the include set depends on (§11.3)
                scan_color(*assignment.value);
            }
            // each handler body is scanned the same way, because it may hold anything (§11.3)
            for (const CheckedHandler& handler : node.handlers) {
                // each statement is scanned on its own, because one problem must not hide the rest (§11.3)
                for (const CheckedStatement& statement : handler.body) {
                    // an assignment carries its value, which is scanned (§11.3)
                    if (statement.value != nullptr) {
                        // the value is scanned for physical lengths, because their density is undefined (§11.3, AD-27)
                        scan_expression(*statement.value);
                        // the value is scanned for colour use, which the include set depends on (§11.3)
                        scan_color(*statement.value);
                    }
                    // an invocation carries its arguments, which are scanned in order (§11.3)
                    for (const SharedExpression& argument : statement.arguments) {
                        // the argument is scanned for physical lengths, because their density is undefined (§11.3, AD-27)
                        scan_expression(*argument);
                        // the argument is scanned for colour use, which the include set depends on (§11.3)
                        scan_color(*argument);
                    }
                }
            }
            // the children are walked, because a nested problem matters just as much (§11.3)
            for (const std::shared_ptr<CheckedNode>& child : node.children)
                validate_node(*child);
        }

        void ComponentEmitter::validate_type(const ValueType& type, const SourceSpan& span) {
            // a scalar type translates directly, which is what every other check assumes (§11.3)
            if (type.is(TypeKind::Bool) || type.is(TypeKind::Int) || type.is(TypeKind::Float) || type.is(TypeKind::String) || type.is(TypeKind::Color) || type.is(TypeKind::Length) || type.is(TypeKind::Align) || type.is(TypeKind::Scrollbar))
                return;
            // a collection is generatable when its element is, which is the same rule applied one level in (§8.1)
            if (type.is(TypeKind::Vector)) {
                validate_type(type.element(), span);
                return;
            }
            // a record is spelled by its own name, and the manifest header declares the struct (§11.2, AD-17)
            if (type.is(TypeKind::Native)) {
                // a registered record is generatable, because the glue generator wrote its definition (§11.2)
                if (m_catalog != nullptr && m_catalog->find_record(type.native_name()) != nullptr)
                    return;
                // a data type the manifest declared is not a record, so nothing generates it (§11.2, AD-17)
                m_diagnostics.add(Diagnostic::Severity::Error, span, "E0501", "a value of type '" + type.to_string() + "' cannot be generated yet");
                // the blocking flag is set, which empties the generated output (§11.3)
                m_blocked = true;
                return;
            }
            // a snapshot carries an immutable shared value, which a generated property cannot yet hold (§11.3, §13.2)
            if (type.is(TypeKind::Snapshot)) {
                m_diagnostics.add(Diagnostic::Severity::Error, span, "E0501", "a value of type '" + type.to_string() + "' cannot be generated yet");
                // the blocking flag is set, which empties the generated output (§11.3)
                m_blocked = true;
                return;
            }
            // a native element the generator cannot construct has no C++ spell here, which is reported (§11.3, AD-17)
            m_diagnostics.add(Diagnostic::Severity::Error, span, "E0501", "a value of type '" + type.to_string() + "' cannot be generated yet");
            // the blocking flag is set, which empties the generated output (§11.3)
            m_blocked = true;
        }

        void ComponentEmitter::scan_expression(const Expression& expression) {
            // a physical length is reported, because its density is not known at build time (§11.3, AD-27)
            if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression); literal != nullptr && literal->kind == LiteralExpression::Kind::Length && ends_with(literal->text, "phx")) {
                // the problem points at the literal, because that is what carries the unit (§11.3, AD-27)
                m_diagnostics.add(Diagnostic::Severity::Error, literal->span, "E0502", "a physical pixel length cannot be generated yet");
                // the blocking flag is set, which empties the generated output (§11.3, AD-27)
                m_blocked = true;
            }
            // a conversion wraps a value, which is walked behind it (§11.3)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression)) {
                // the wrapped value is walked, because the whole expression is scanned (§11.3)
                scan_expression(*conversion->value);
            }
            // a colour operation has two operands, which are both walked (§11.3)
            if (const auto* call = dynamic_cast<const CallExpression*>(&expression)) {
                // every argument is walked in written order, because a bound operand is a dependency (§11.3, AD-04a)
                for (const ExpressionPtr& argument : call->arguments)
                    scan_expression(*argument);
            }
            // a binary application has two operands, which are both walked (§11.3)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
                // the left operand comes first, which is the written order (§11.3)
                scan_expression(*binary->left);
                // the right operand follows, which is the written order (§11.3)
                scan_expression(*binary->right);
            }
            // a unary application has one operand, which is walked behind it (§11.3)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                scan_expression(*unary->value);
            // a grouping has an inner expression, which is walked behind it (§11.3)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
                scan_expression(*grouping->inner);
            // a bracketed list has elements, each of which is walked because the whole expression is scanned (§8.1)
            if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression))
                for (const ExpressionPtr& element : array->elements)
                    scan_expression(*element);
            // a field read walks its object, because the object may hold anything a scan must refuse (§11.2)
            if (const auto* access = dynamic_cast<const FieldAccessExpression*>(&expression))
                scan_expression(*access->object);
        }

        void ComponentEmitter::scan_color(const Expression& expression) {
            // a string conversion of a float needs the shortest form helper, which prints it (§11.3)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression); conversion != nullptr && conversion->target_type == "string" && type_of(*conversion->value).is(TypeKind::Float))
                m_uses_float_helper = true;
            // a translation resolves through the catalogue, which needs its own header (§9.2, §11.3)
            if (dynamic_cast<const TranslationExpression*>(&expression) != nullptr)
                m_uses_i18n = true;
            // a colour literal needs the paint header, which its constructor call requires (§11.3)
            if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression); literal != nullptr && literal->kind == LiteralExpression::Kind::Color)
                m_uses_color = true;
            // a string conversion of a colour needs the hex helper too, which also needs printing (§11.3)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression); conversion != nullptr && conversion->target_type == "string" && type_of(*conversion->value).is(TypeKind::Color)) {
                // the hex helper forces the cstdio header, which the printer needs (§11.3)
                m_uses_color_helper = true;
                m_uses_color = true;
            }
            // a conversion wraps a value, which is walked behind it (§11.3)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression))
                scan_color(*conversion->value);
            // a binary application has two operands, which are both walked (§11.3)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
                // the left operand comes first, which is the written order (§11.3)
                scan_color(*binary->left);
                // the right operand follows, which is the written order (§11.3)
                scan_color(*binary->right);
            }
            // a unary application has one operand, which is walked behind it (§11.3)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                scan_color(*unary->value);
            // a grouping has an inner expression, which is walked behind it (§11.3)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
                scan_color(*grouping->inner);
        }

        void ComponentEmitter::collect(const CheckedNode& node, std::size_t parent) {
            // a repeat is not a widget, so it becomes a keyed view rather than a node of its own (§18.1, AD-13)
            if (node.kind == TreeNodeKind::Repeat) {
                // the repeat takes the next index, which names its view member (§18.1)
                const std::size_t index = m_repeats.size();
                // the slot collects the repeat and the body it materializes (§18.1)
                RepeatSlot slot{.index = index, .node = &node, .parent = parent};
                // a repeat whose container is a viewport builds only the window, which is virtualization (§8.2, AD-13)
                if (parent != NO_PARENT && m_nodes.at(parent).node->element_type == "ScrollView")
                    slot.virtualized = true;
                // the body is walked into its own list, because a row is made of those nodes (§18.1)
                if (node.repeat_body != nullptr)
                    collect_body(*node.repeat_body, NO_PARENT, slot.body, {});
                // the repeat is appended, which fixes its order against the other repeats (§18.1)
                m_repeats.push_back(std::move(slot));
                return;
            }
            // the node takes the next index, which is its slot in the build order (§8.1)
            const std::size_t index = m_nodes.size();
            // the node is appended, so its children can reference its index (§8.1)
            m_nodes.push_back({.index = index, .node = &node, .parent = parent});
            // the children follow in order, which keeps their preorder contiguous (§8.1)
            for (const std::shared_ptr<CheckedNode>& child : node.children)
                collect(*child, index);
        }

        ValueType ComponentEmitter::repeat_element_type(const CheckedNode& node) const {
            // a vector source carries its element, because a row is built from one item (§8.1)
            if (node.repeat_source_type.is(TypeKind::Vector))
                return node.repeat_source_type.element();
            // any other source is walked as a whole, which the checker required to be a collection (§11.1)
            return node.repeat_source_type;
        }

        const RecordFieldType* ComponentEmitter::item_key_field_for(const CheckedNode& node) const {
            // a non repeat node has no rows, so there is nothing to key (§18.1)
            if (node.kind != TreeNodeKind::Repeat)
                return nullptr;
            // the item is the element, because that is what the body binds (§11.1, §18.1)
            const ValueType item = repeat_element_type(node);
            // only a record declares a key, because only a record has fields to mark (§18.1, AD-13)
            if (!item.is(TypeKind::Native))
                return nullptr;
            // a caller that registered no catalog declares no records, which is not a keyable element (§11.2, AD-17)
            if (m_catalog == nullptr)
                return nullptr;
            // the catalog resolves the name, because the manifest is the only source of a record (§11.2, AD-17)
            const RecordType* record = m_catalog->find_record(item.native_name());
            // an unknown name declares no fields, which the checker already reported (§11.2, AD-17)
            if (record == nullptr)
                return nullptr;
            return record->key_field();
        }

        const RecordFieldType* ComponentEmitter::item_key_field(const RepeatSlot& slot) const {
            // the slot carries the repeat, whose element decides the key (§18.1, AD-13)
            return item_key_field_for(*slot.node);
        }

        std::string ComponentEmitter::item_type(const RepeatSlot& slot) const {
            // the item is the element of the source, which is what one row is built from (§8.1, §18.1)
            return cpp_type(repeat_element_type(*slot.node));
        }

        void ComponentEmitter::collect_body(const CheckedNode& node, std::size_t parent, std::vector<NodeSlot>& out, std::vector<std::size_t> path) {
            // the body node takes the next index, which names its local inside one row (§8.1, §18.1)
            const std::size_t index = m_body_count++;
            // the slot carries the path from the row root, which is how an update reaches this node (§18.1, AD-13)
            out.push_back({.index = index, .node = &node, .parent = parent, .row_path = path});
            // the children follow in order, because a row nests exactly as the author wrote it (§8.1, §18.1)
            for (std::size_t position = 0; position < node.children.size(); position++) {
                // the extended path names this child under its parent, which is what an update walks (§18.1, AD-13)
                std::vector<std::size_t> child_path = path;
                // the position is appended, because the path is the chain of child positions (§8.1, §18.1)
                child_path.push_back(position);
                collect_body(*node.children.at(position), index, out, std::move(child_path));
            }
        }

        const CheckedProperty* ComponentEmitter::property_named(std::string_view name) const {
            // the declarations are scanned, because a component holds only a handful of them (§11.3)
            for (const CheckedProperty& property : m_component.properties) {
                // the name decides, so a reference resolves to exactly one declaration (§11.3)
                if (property.name == name)
                    return &property;
            }
            // a name that is not declared is a checker defect, because the checker already resolved it (§11.3)
            return nullptr;
        }

        ValueType ComponentEmitter::type_of(const Expression& expression) const {
            // a literal carries its own type, because its kind says what it is (§11.3)
            if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression)) {
                // the kind is mapped, which mirrors the checker's literal rule (§11.3)
                switch (literal->kind) {
                case LiteralExpression::Kind::Integer:
                    return scalar(TypeKind::Int);
                case LiteralExpression::Kind::Float:
                    return scalar(TypeKind::Float);
                case LiteralExpression::Kind::String:
                    return scalar(TypeKind::String);
                case LiteralExpression::Kind::Color:
                    return scalar(TypeKind::Color);
                case LiteralExpression::Kind::Length:
                    return scalar(TypeKind::Length);
                case LiteralExpression::Kind::Boolean:
                    return scalar(TypeKind::Bool);
                }
            }
            // a translation always produces a string, because that is what the catalog holds (§11.3)
            if (dynamic_cast<const TranslationExpression*>(&expression) != nullptr)
                return scalar(TypeKind::String);
            // a bracketed list is the collection its first element declares, which is what the initialiser names (§8.1)
            if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression)) {
                // an empty list names no element, and its value initialisation takes the type from the target (§8.1)
                if (array->elements.empty())
                    return ValueType::parameterized(TypeKind::Vector, error_type());
                // the first element decides, because the checker rejected a list whose elements disagree (§8.1)
                return ValueType::parameterized(TypeKind::Vector, type_of(*array->elements.front()));
            }
            // a reference resolves against the component, which is what a generated read needs (§11.3)
            if (const auto* reference = dynamic_cast<const PropertyExpression*>(&expression)) {
                // the declared property decides, which the checker already confirmed exists (§11.3)
                const CheckedProperty* property = property_named(reference->name);
                // a missing property cannot be typed, which a checker defect would cause (§11.3)
                return property != nullptr ? property->type : error_type();
            }
            // a conversion produces its target, which is the closed rule set (§11.3)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression)) {
                // the target name maps onto a kind, which is how the checker reads it (§11.3)
                TypeKind target = TypeKind::Error;
                // a name the checker already reported is typed as nothing (§11.3)
                return builtin_conversion_target(conversion->target_type, target) ? scalar(target) : error_type();
            }
            // a binary application types through the same closed rules, which decides the operator (§11.3)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
                return type_of(*binary->left).is(TypeKind::String) || type_of(*binary->right).is(TypeKind::String) ? scalar(TypeKind::String) : type_of(*binary->left).is_numeric() && type_of(*binary->right).is_numeric() ? scalar(TypeKind::Float) : error_type();
            // a unary application types through its operand, because only that is read (§11.3)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                return type_of(*unary->value).is(TypeKind::Bool) ? scalar(TypeKind::Bool) : error_type();
            // a grouping contributes nothing, so its inner expression types the whole (§11.3)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
                return type_of(*grouping->inner);
            // a shape the checker already rejected cannot be typed here, which is safe because it blocked (§11.3)
            return error_type();
        }

        void ComponentEmitter::emit_header() {
            // the header output is the active target, which the line helper appends to (§14.1)
            m_output = &m_header;
            // every generated file starts with its provenance, which is what the generator owns (§14, AD-03a)
            line(0, "// generated from " + m_class + ".ui, do not edit (§14, AD-03a)");
            line(0, "#pragma once");
            line(0, "");
            line(0, "#include <functional>");
            // the owning pointer needs the memory header, which is in every header (§14.1)
            line(0, "#include <memory>");
            // the string signatures need the string header, which is in every header (§14.1)
            line(0, "#include <string>");
            // a collection signature needs the vector header, which only a collection component has (§8.1, §14.1)
            if (m_uses_vector)
                line(0, "#include <vector>");
            // a repeat materializes keyed rows, so the facade names the view and the key (§18.1, AD-13)
            if (!m_repeats.empty())
                line(0, "#include \"model/keyed_view.h\"");
            // a repeat under a viewport names the windowed view as well (§8.2, AD-13)
            if (std::any_of(m_repeats.begin(), m_repeats.end(), [](const RepeatSlot& slot) { return slot.virtualized; }))
                line(0, "#include \"model/scrollable_rows.h\"");
            line(0, "");
            // each record the component names pulls the manifest header the glue generator wrote (§11.2, AD-17)
            for (const std::string& header : m_record_headers)
                line(0, "#include \"" + header + "\"");
            // a colour signature pulls the paint header, which only a colour component needs (§14.1)
            if (m_uses_color)
                line(0, "#include \"core/paint.h\"");
            // the text engine parameter pulls its header, which every component takes (§14.1)
            line(0, "#include \"text/text_engine.h\"");
            // the base class pulls the component header, which every facade inherits (§14.1)
            line(0, "#include \"ui/component.h\"");
            line(0, "");
            line(0, "namespace albor");
            line(0, "{");
            line(1, "// the concrete facade of the " + m_class + " component, which hides its generated body (§14.1, AD-03)");
            line(1, "class " + m_class + " final : public Component");
            line(1, "{");
            line(1, "public:");
            line(2, "// build the component over the engines its body reads, which constructs the whole tree (§14.1)");
            line(2, m_class + "(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id = {});");
            line(0, "");
            line(2, "// destroy the component, which tears down the tree the body owns (§14.1)");
            line(2, "~" + m_class + "() override;");
            line(0, "");
            // every property contributes a read and a write, which is what a host binds against (§14.1)
            for (const CheckedProperty& property : m_component.properties) {
                // the accessor name is the shared C++ name of the property (§14.1)
                const std::string& name = m_names.at(property.name);
                // a large value is handed out by reference, which avoids a copy on the read path (§14.1)
                const std::string ret = read_return_type(property.type);
                line(2, "// hand out the current value of the " + property.name + " property, which reads the reactive value (§14.1, §7.1)");
                line(2, "[[nodiscard]] " + ret + " " + name + "() const;");
                line(0, "");
                line(2, "// replace the value of the " + property.name + " property, which drives the bindings that read it (§14.1, §7.1)");
                line(2, "void set_" + name + "(" + cpp_type(property.type) + " value);");
                line(0, "");
            }
            // every signal contributes a slot, which is how a host receives an invocation (§8.3)
            for (const CheckedSignal& signal : m_component.signals) {
                // the slot name is the shared C++ name of the signal (§14.1)
                const std::string& name = m_names.at(signal.name);
                // the parameters are rendered as the C++ list, which the handler signature needs (§14.1)
                std::string params;
                // each parameter appends its own text, which is the declaration order (§11.3)
                for (const ValueType& parameter : signal.parameter_types) {
                    // a comma joins the earlier parameters, which is how a signature reads (§11.3)
                    if (!params.empty())
                        params += ", ";
                    // the type is the C++ spelling of a checked scalar (§11.3)
                    params += cpp_type(parameter);
                }
                line(2, "// install the work a " + signal.name + " invocation reports, which the generated handler runs (§14.1, §8.3)");
                line(2, "void set_on_" + name + "(std::function<void(" + params + ")> handler);");
                line(0, "");
            }
            line(1, "private:");
            line(2, "// the generated body, which owns the widget tree and the bindings (§14.1, AD-03)");
            line(2, "struct Impl;");
            line(0, "");
            line(2, "// the owned body, which the whole facade hides (§14.1, AD-03)");
            line(2, "std::unique_ptr<Impl> m_impl;");
            line(1, "};");
            line(0, "} // namespace albor");
        }

        void ComponentEmitter::emit_source() {
            // the implementation output is the active target, which the line helper appends to (§14.1)
            m_output = &m_source;
            line(0, "// generated from " + m_class + ".ui, do not edit (§14, AD-03a)");
            line(0, "");
            line(0, "#include \"" + m_stem + ".h\"");
            line(0, "");
            // the printing helpers need them, which only a number or colour to text conversion reaches (§11.3)
            if (m_uses_color_helper || m_uses_float_helper) {
                line(0, "#include <array>");
                line(0, "#include <cstdio>");
                line(0, "#include <cstdlib>");
                line(0, "#include <cstring>");
            }
            // the signal slots need the functional header, which a signal component reaches (§8.3)
            if (m_has_signals)
                line(0, "#include <functional>");
            // the owning pointers need the memory header, which the builder uses (§8.1)
            line(0, "#include <memory>");
            // the text types need the string header, which every generated file uses (§14.1)
            line(0, "#include <string>");
            // the move calls need the utility header, which the setters use (§14.1)
            line(0, "#include <utility>");
            line(0, "");
            std::vector<std::string> project_headers;
            // the emits above only need the cstdio section, so the project section is the tree's (§14.1)
            if (m_uses_canvas)
                project_headers.push_back("core/geometry.h");
            // a colour emission needs the paint header, which only a colour component reaches (§14.1)
            if (m_uses_color)
                project_headers.push_back("core/paint.h");
            // an alignment emission names an enumerator, which the align header declares (§14.1, §8.2)
            if (m_uses_align)
                project_headers.push_back("core/align.h");
            // a translated emission resolves through the catalogue, which only a translated component needs (§9.2)
            if (m_uses_i18n)
                project_headers.push_back("i18n/catalog.h");
            // the widget headers join the set, because each constructed class must be declared (§14.1)
            for (const NodeSlot& slot : m_nodes) {
                // the widget info of the element names the header to include (§14.1)
                const WidgetInfo& info = widget_info().at(std::string_view(slot.node->element_type));
                // the header is added once, because the set deduplicates (§14.1)
                project_headers.push_back(std::string(info.header));
            }
            // a repeat body builds widgets inside the row functions, so its headers join the set (§18.1, AD-13)
            for (const RepeatSlot& repeat : m_repeats)
                // each body node contributes its header, because the row functions construct it (§11.3)
                for (const NodeSlot& slot : repeat.body)
                    // the widget info of the element names the header to include (§14.1)
                    project_headers.push_back(std::string(widget_info().at(std::string_view(slot.node->element_type)).header));
            // the engine type needs its header, which every widget body uses (§14.1)
            project_headers.push_back("runtime/binding_engine.h");
            // the property type needs its header, which every body uses (§14.1)
            project_headers.push_back("runtime/property.h");
            // the text engine type needs its header, which a label body uses (§14.1)
            if (m_uses_text_engine)
                project_headers.push_back("text/text_engine.h");
            // the base class needs its header, which every facade inherits (§14.1)
            project_headers.push_back("ui/component.h");
            // the set is sorted, because generated includes must be canonical (§14.1)
            std::sort(project_headers.begin(), project_headers.end());
            // the set is deduplicated, because a repeated element includes its header once (§14.1)
            project_headers.erase(std::unique(project_headers.begin(), project_headers.end()), project_headers.end());
            // the project includes are emitted in canonical order, which is what the build checks (§14.1)
            for (const std::string& header : project_headers)
                line(0, "#include \"" + header + "\"");
            line(0, "");
            line(0, "namespace albor");
            line(0, "{");
            // the printing helpers come first, because an evaluator below may call one (§11.3)
            if (m_uses_float_helper)
                emit_float_helper();
            if (m_uses_color_helper)
                emit_color_helper();
            emit_body();
            emit_lifecycle();
            emit_accessors();
            emit_build();
            emit_evaluators();
            emit_computes();
            emit_handlers();
            emit_draw_bodies();
            // the row functions belong to this unit, which is why they are emitted inside it (§18.1, AD-13)
            emit_repeat_row_functions_all();
            // each repeat emits its evaluator, which is what a source change calls (§18.1, AD-13)
            for (const RepeatSlot& slot : m_repeats) {
                // the suffix names every member of this repeat, which keeps them unique across repeats (§18.1)
                const std::string at = "_" + std::to_string(slot.index);
                line(0, "");
                line(1, "std::vector<" + item_type(slot) + "> " + m_class + "::Impl::evaluate_repeat" + at + "(void* raw) {");
                line(2, "// the body is recovered from the context, which keeps the row path allocation free (§13.3)");
                line(2, "auto* self = static_cast<Impl*>(raw);");
                m_object = "self";
                line(2, "// the source is read once, because the rows reconcile against that one value (§18.1)");
                line(2, "const std::vector<" + item_type(slot) + "> value = " + repeat_source_expression(slot) + ";");
                // a windowed repeat borrows a body member, because the rows outlive this frame's local (§8.2, AD-13)
                if (slot.virtualized) {
                    line(2, "// the source is stored in the body, because the rows borrow it past this call (§8.2)");
                    line(2, "self->m_repeat_items" + at + " = value;");
                    line(2, "self->m_repeat" + at + "->set_items(self->m_repeat_items" + at + ");");
                }
                // an unwindowed repeat reconciles the whole container, because every item has a row (§18.1, AD-13)
                else
                    line(2, "self->reconcile" + at + "();");
                line(2, "// the value is returned, which the engine stores as the bound value (§13.3)");
                line(2, "return value;");
                line(1, "}");
                m_object.clear();
            }
            // each repeat emits its reconcile entry points, which are what the source binding calls (§18.1, AD-13)
            for (const RepeatSlot& slot : m_repeats) {
                // the suffix names every member of this repeat, which keeps them unique across repeats (§18.1)
                const std::string at = "_" + std::to_string(slot.index);
                line(0, "");
                line(1, "void " + m_class + "::Impl::reconcile" + at + "() {");
                reconcile(slot);
                line(1, "}");
            }
            line(0, "} // namespace albor");
        }

        void ComponentEmitter::emit_body() {
            line(1, "// the generated body of " + m_class + ", which owns the widget tree and the bindings (§14.1, AD-03)");
            line(1, "struct " + m_class + "::Impl");
            line(1, "{");
            line(2, "// the owning facade, which the body registers through (§7.2)");
            line(2, m_class + "* owner = nullptr;");
            line(0, "");
            // the text engine is stored only when a label reads it, which keeps the body tight (§9.1)
            if (m_uses_text_engine) {
                line(2, "// the text engine the labels measure through, which the application owns (§9.1)");
                line(2, "TextEngine* text_engine = nullptr;");
                line(0, "");
            }
            // each property becomes a reactive member, which holds its current value (§7.2)
            for (const CheckedProperty& property : m_component.properties) {
                // the member name is the shared C++ name behind the accessor (§14.1)
                const std::string& name = m_names.at(property.name);
                line(2, "// the property behind " + property.name + ", which the bindings read (§7.2)");
                // a literal default initialises the member, which is what the facade presents at once (§14.1)
                if (property.has_default && property.default_dependencies.empty()) {
                    // the default expression is rendered as a member initializer, which the literal decides (§14.1)
                    line(2, "Property<" + cpp_type(property.type) + "> m_" + name + "{" + emit_expression(*property.default_value) + "};");
                }
                // any other default is installed by the builder, so the member starts empty (§7.2)
                else {
                    line(2, "Property<" + cpp_type(property.type) + "> m_" + name + ";");
                }
                line(0, "");
            }
            // each widget becomes an owned pointer member, which the evaluators write through (§8.1)
            for (const NodeSlot& slot : m_nodes) {
                // the class name is the constructed type, which decides the pointer type (§8.1)
                const std::string_view class_name = widget_info().at(std::string_view(slot.node->element_type)).class_name;
                line(2, "// the " + slot.node->element_type + " node, which the bindings write through (§8.1)");
                line(2, std::string(class_name) + "* m_node_" + std::to_string(slot.index) + " = nullptr;");
                line(0, "");
            }
            // a canvas tracks its two dimensions, which the intrinsic size combines (§11.3)
            for (const NodeSlot& slot : m_nodes) {
                // only a canvas carries the tracks, because its size is declared not measured (§11.3)
                if (slot.node->element_type != "Canvas")
                    continue;
                line(2, "// the declared canvas width, which the intrinsic size reads (§11.3)");
                line(2, "float m_canvas_w_" + std::to_string(slot.index) + " = 0.0f;");
                line(2, "// the declared canvas height, which the intrinsic size reads (§11.3)");
                line(2, "float m_canvas_h_" + std::to_string(slot.index) + " = 0.0f;");
                line(0, "");
            }
            // a scroll view tracks its two viewport extents, which one setter combines (§11.3)
            for (const NodeSlot& slot : m_nodes) {
                // only a scroll view carries the tracks, because its viewport is written as an extent (§11.3)
                if (slot.node->element_type != "ScrollView")
                    continue;
                line(2, "// the declared viewport width, which the viewport extent combines (§11.3)");
                line(2, "float m_viewport_w_" + std::to_string(slot.index) + " = 0.0f;");
                line(2, "// the declared viewport height, which the viewport extent combines (§11.3)");
                line(2, "float m_viewport_h_" + std::to_string(slot.index) + " = 0.0f;");
                line(0, "");
            }
            // a slider tracks its two bounds, which the range setter combines (§11.3)
            for (const NodeSlot& slot : m_nodes) {
                // only a slider carries the tracks, because a range is written as bounds (§11.3)
                if (slot.node->element_type != "Slider")
                    continue;
                // a bound the author did not write keeps the widget default, which is zero to one (§8)
                bool wrote_min = false;
                bool wrote_max = false;
                for (const CheckedAssignment& assignment : slot.node->assignments) {
                    wrote_min = wrote_min || assignment.name == "min";
                    wrote_max = wrote_max || assignment.name == "max";
                }
                line(2, "// the lower bound, which defaults to the widget range when the author wrote none (§8)");
                line(2, "float m_slider_min_" + std::to_string(slot.index) + " = 0.0f;");
                line(2, "// the upper bound, which defaults to the widget range when the author wrote none (§8)");
                line(2, "float m_slider_max_" + std::to_string(slot.index) + (wrote_max ? " = 0.0f;" : " = 1.0f;"));
                line(0, "");
            }
            // each bound assignment becomes a sink property, which the engine keeps current (§13.3)
            for (const NodeSlot& slot : m_nodes) {
                // the sinks are walked per assignment, which matches the builder's order (§13.3)
                for (const CheckedAssignment& assignment : slot.node->assignments) {
                    // an assignment with no sources is applied directly, so it needs no sink (§13.3)
                    if (assignment.dependencies.empty())
                        continue;
                    // a bound offset writes the widget's own property, so a sink would only detach it (§7.3, AD-24)
                    if (slot.node->element_type == "ScrollView" && assignment.name == "offset")
                        continue;
                    // the sink name is the composite of the node and the property (§13.3)
                    const std::string sink = "m_sink_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                    line(2, "// the sink behind the " + assignment.name + " assignment, which the engine keeps current (§13.3)");
                    line(2, "Property<" + cpp_type(assignment.type) + "> " + sink + ";");
                    line(0, "");
                }
            }
            // each signal becomes a slot, which the generated handler invokes (§8.3)
            for (const CheckedSignal& signal : m_component.signals) {
                // the slot name is the shared C++ name of the signal (§14.1)
                const std::string& name = m_names.at(signal.name);
                std::string params;
                // each parameter appends its own text, which is the declaration order (§11.3)
                for (const ValueType& parameter : signal.parameter_types) {
                    // a comma joins the earlier parameters, which is how a signature reads (§11.3)
                    if (!params.empty())
                        params += ", ";
                    // the type is the C++ spelling of a checked scalar (§11.3)
                    params += cpp_type(parameter);
                }
                line(2, "// the slot behind " + signal.name + ", which the generated handler invokes (§8.3)");
                line(2, "std::function<void(" + params + ")> m_on_" + name + ";");
                line(0, "");
            }
            // each repeat declares its view and its row functions, which is what materializes a row (§18.1, AD-13)
            if (!m_repeats.empty())
                emit_repeat_members();
            // each canvas body node declares row local tracks, because every row owns its own (§11.3, §18.1)
            for (const RepeatSlot& slot : m_repeats) {
                // the body is walked per node, because only a canvas publishes two dimensions (§11.3, §18.1)
                for (const NodeSlot& body : slot.body) {
                    // a canvas row needs the two tracks, because its setter takes one size (§11.3, §18.1)
                    if (body.node->element_type != "Canvas")
                        continue;
                    line(0, "");
                    line(2, "// the canvas tracks of this row node, which every row owns separately (§11.3, §18.1)");
                    line(2, "float m_row_canvas_w_" + std::to_string(body.index) + " = 0.0f;");
                    line(2, "float m_row_canvas_h_" + std::to_string(body.index) + " = 0.0f;");
                }
            }
            line(0, "");
            line(2, "// build the whole tree and install the bindings, which runs once (§14.1)");
            line(2, "void build();");
            // a blank line separates each declaration, which keeps the body readable (§14.1)
            for (const NodeSlot& slot : m_nodes) {
                // the evaluator is walked per assignment, which names it after the property (§13.3)
                for (const CheckedAssignment& assignment : slot.node->assignments) {
                    // a constant assignment has no evaluator, because the builder applies it directly (§13.3)
                    if (assignment.dependencies.empty())
                        continue;
                    // the declaration reads after a blank, which separates it from the previous one (§14.1)
                    line(0, "");
                    const std::string evaluator = "evaluate_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                    line(2, "// evaluate the " + assignment.name + " into the widget and the sink, which is the binding body (§13.3)");
                    line(2, "static " + cpp_type(assignment.type) + " " + evaluator + "(void* raw);");
                }
            }
            // each bound default declares its evaluator, which the property thunk runs (§13.3)
            for (const CheckedProperty& property : m_component.properties) {
                // a default with no sources has no evaluator, because the builder applies it directly (§13.3)
                if (!property.has_default || property.default_dependencies.empty())
                    continue;
                // the declaration reads after a blank, which separates it from the previous one (§14.1)
                line(0, "");
                // the evaluator is named after the property, which the builder registered (§13.3)
                const std::string& name = m_names.at(property.name);
                line(2, "// evaluate the default of " + property.name + ", which recomputes it when a source changes (§13.3)");
                line(2, "static " + cpp_type(property.type) + " compute_" + name + "(void* raw);");
            }
            // each handler declares its body, which the wiring runs (§11.3)
            for (const NodeSlot& slot : m_nodes) {
                // the handler is walked per attachment, which wires it after the node is built (§11.3)
                for (const CheckedHandler& handler : slot.node->handlers) {
                    // the declaration reads after a blank, which separates it from the previous one (§14.1)
                    line(0, "");
                    const std::string handle = "handle_" + std::to_string(slot.index) + "_" + sanitize(handler.name);
                    line(2, "// run the " + handler.name + " body, which is what the written handler does (§11.3)");
                    line(2, "static void " + handle + "(void* raw" + handler_signature(slot.node->element_type, handler.name) + ");");
                }
            }
            // a canvas with a draw block declares its body, which the wiring runs (§6.4)
            for (const NodeSlot& slot : m_nodes) {
                // a node with no draw block declares nothing, because it paints itself (§6.4)
                if (slot.node->draw_calls.empty())
                    continue;
                // the declaration reads after a blank, which separates it from the previous one (§14.1)
                line(0, "");
                line(2, "// draw the canvas content, which is what the written draw block does (§6.4)");
                line(2, "static void draw_" + std::to_string(slot.index) + "_body(void* raw, RenderContext& context);");
            }
            line(1, "};");
        }

        void ComponentEmitter::emit_lifecycle() {
            line(0, "");
            line(1, m_class + "::" + m_class + "(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id) :");
            line(1, "    Component(engine, std::move(explicit_id)), m_impl(std::make_unique<Impl>()) {");
            line(2, "// the body needs the facade, which owns the widgets and the bindings (§14.1)");
            line(2, "m_impl->owner = this;");
            // the text engine is handed over only when a label reads it, which the body needs (§9.1)
            if (m_uses_text_engine) {
                line(2, "// the body measures labels through the engine, which the application owns (§9.1)");
                line(2, "m_impl->text_engine = text_engine;");
            }
            // a component with no label never reads the engine, which marks the parameter used (§9.1)
            else {
                line(2, "// no label reads the engine, so the parameter stays unused (§9.1)");
                line(2, "(void)text_engine;");
            }
            line(2, "// the tree and the bindings are installed exactly once, at construction (§14.1)");
            line(2, "m_impl->build();");
            line(1, "}");
            line(0, "");
            line(1, m_class + "::~" + m_class + "() = default;");
        }

        void ComponentEmitter::emit_accessors() {
            // each property contributes its read and its write, which the host uses (§14.1)
            for (const CheckedProperty& property : m_component.properties) {
                // the accessor name is the shared C++ name of the property (§14.1)
                const std::string& name = m_names.at(property.name);
                // the read returns a reference for a large value, which avoids a copy (§14.1)
                const std::string ret = read_return_type(property.type);
                line(0, "");
                line(1, ret + " " + m_class + "::" + name + "() const {");
                line(2, "// the value is read through the body, which the facade hides (§14.1)");
                line(2, "return m_impl->m_" + name + ".get();");
                line(1, "}");
                line(0, "");
                line(1, "void " + m_class + "::set_" + name + "(" + cpp_type(property.type) + " value) {");
                line(2, "// the imperative write drives the bindings that read it, which the engine settles (§7.1)");
                line(2, "m_impl->m_" + name + ".set(std::move(value));");
                line(1, "}");
            }
            // each signal contributes its slot, which the host wires (§8.3)
            for (const CheckedSignal& signal : m_component.signals) {
                // the slot name is the shared C++ name of the signal (§14.1)
                const std::string& name = m_names.at(signal.name);
                std::string params;
                // each parameter appends its own text, which is the declaration order (§11.3)
                for (const ValueType& parameter : signal.parameter_types) {
                    // a comma joins the earlier parameters, which is how a signature reads (§11.3)
                    if (!params.empty())
                        params += ", ";
                    // the type is the C++ spelling of a checked scalar (§11.3)
                    params += cpp_type(parameter);
                }
                line(0, "");
                line(1, "void " + m_class + "::set_on_" + name + "(std::function<void(" + params + ")> handler) {");
                line(2, "// the slot is stored, which the generated handler invokes (§8.3)");
                line(2, "m_impl->m_on_" + name + " = std::move(handler);");
                line(1, "}");
            }
        }

        void ComponentEmitter::emit_build() {
            line(0, "");
            line(1, "void " + m_class + "::Impl::build() {");
            // the engine is resolved once, which every widget construction below takes (§7.2)
            if (m_uses_engine) {
                line(2, "// the engine settles the registered properties, which the widgets read (§7.2)");
                line(2, "BindingEngine* const engine = owner->engine();");
                line(0, "");
            }
            // every property joins the component scope, which gives it an identifier (§7.2)
            for (const CheckedProperty& property : m_component.properties) {
                // the member name is the shared C++ name of the property (§7.2)
                const std::string& name = m_names.at(property.name);
                line(2, "// the property joins the component scope, which gives it a unique id (§7.2)");
                line(2, "owner->register_property(m_" + name + ");");
            }
            // every sink joins the scope too, which the binding engine keys on (§13.3)
            for (const NodeSlot& slot : m_nodes) {
                // the sink is walked per assignment, which matches the registration order (§13.3)
                for (const CheckedAssignment& assignment : slot.node->assignments) {
                    // a constant assignment has no sink, because the builder applies it directly (§13.3)
                    if (assignment.dependencies.empty())
                        continue;
                    // a bound offset has no sink, because it is the widget property that is bound (§7.3, AD-24)
                    if (slot.node->element_type == "ScrollView" && assignment.name == "offset")
                        continue;
                    const std::string sink = "m_sink_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                    line(2, "// the sink joins the component scope, which gives the binding a unique id (§13.3)");
                    line(2, "owner->register_property(" + sink + ");");
                }
            }
            // a default that reads no source is computed once, which is what the builder owns (§14.1)
            for (const CheckedProperty& property : m_component.properties) {
                // the member name is the shared C++ name of the property (§14.1)
                const std::string& name = m_names.at(property.name);
                // a default that reads sources is bound, which is handled after the plain ones (§13.3)
                if (!property.has_default || !property.default_dependencies.empty())
                    continue;
                line(0, "");
                line(2, "// the default of " + property.name + " is computed once, because it reads no source (§14.1)");
                line(2, "m_" + name + ".set(" + emit_expression(*property.default_value) + ");");
            }
            // a default that reads sources is bound, which the engine keeps live (§13.3)
            for (const CheckedProperty& property : m_component.properties) {
                // the member name and the dependencies decide, which is what the binding needs (§13.3)
                if (!property.has_default || property.default_dependencies.empty())
                    continue;
                // the member name is the shared C++ name of the property (§13.3)
                const std::string& name = m_names.at(property.name);
                line(0, "");
                line(2, "// the default of " + property.name + " reads its sources, so it is bound (§13.3)");
                line(2, "const PropertyId deps_" + name + "[] = {" + dep_list(property.default_dependencies) + "};");
                line(2, "m_" + name + ".bind_static(&Impl::compute_" + name + ", this, deps_" + name + ");");
            }
            // each repeat claims the position its rows occupy, which must precede its siblings (§18.1, AD-13)
            assign_repeat_offsets();
            // the tree is built in preorder, which hangs every node of the facade together (§8.1)
            for (const NodeSlot& slot : m_nodes) {
                line(0, "");
                emit_node_block(slot);
                // each repeat inside this container is created where it was written (§18.1, AD-13)
                for (const RepeatSlot& repeat : m_repeats) {
                    // a repeat belongs to this container, which is what makes its rows hang here (§8.1, §18.1)
                    if (repeat.parent != slot.index)
                        continue;
                    line(0, "");
                    emit_repeat_reconcile(repeat);
                }
            }
            // the root is adopted last, which is the only owned node of the facade (§8.1)
            if (!m_nodes.empty())
                line(2, "owner->add_child(std::move(node_0));");
            line(1, "}");
        }

        void ComponentEmitter::emit_node_block(const NodeSlot& slot) {
            // the class name decides the constructor, which the element carries (§8.1)
            const WidgetInfo& info = widget_info().at(std::string_view(slot.node->element_type));
            // the constructor arguments depend on the engines the widget reads (§8.1)
            std::string arguments = "()";
            // a widget construction pulls the engine, which the builder resolved above (§7.2)
            if (info.needs_engine && info.needs_text_engine)
                arguments = "(engine, text_engine)";
            // a widget without a label only pulls the engine, which the other widgets take (§7.2)
            else if (info.needs_engine)
                arguments = "(engine)";
            line(2, "// allocate the " + slot.node->element_type + " node, which hangs under its parent (§8.1)");
            line(2, "auto node_" + std::to_string(slot.index) + " = std::make_unique<" + std::string(info.class_name) + ">" + arguments + ";");
            line(2, "// the node is configured through the raw pointer, which the unique pointer moves later (§8.1)");
            line(2, std::string(info.class_name) + "* const n" + std::to_string(slot.index) + " = node_" + std::to_string(slot.index) + ".get();");
            line(2, "// record the node, which the bindings write through (§8.1)");
            line(2, "m_node_" + std::to_string(slot.index) + " = n" + std::to_string(slot.index) + ";");
            // the author identity is applied, which makes the node addressable (AD-11)
            if (!slot.node->explicit_id.empty()) {
                line(2, "// assign the author identity, which everything selects by (AD-11)");
                line(2, "n" + std::to_string(slot.index) + "->set_explicit_id(std::string(\"" + slot.node->explicit_id + "\"));");
            }
            // the assignments are applied in order, which a constant reaches directly (§14.1)
            bool canvas_written = false;
            // whether a slider range was written, which is applied after both bounds (§8)
            bool range_written = false;
            // whether a viewport extent was written, which is applied after both halves (§8.2)
            bool viewport_written = false;
            // each assignment decides between a direct set and a bound sink (§13.3)
            for (const CheckedAssignment& assignment : slot.node->assignments) {
                // the canvas width and height are tracked separately, because one setter combines them (§11.3)
                if (slot.node->element_type == "Canvas" && (assignment.name == "width" || assignment.name == "height")) {
                    // the identity decision is the member to write, which the two dimensions share (§11.3)
                    const std::string member = assignment.name == "width" ? "m_canvas_w_" + std::to_string(slot.index) : "m_canvas_h_" + std::to_string(slot.index);
                    // a constant dimension is written straight into the track, which the intrinsic size reads (§11.3)
                    if (assignment.dependencies.empty()) {
                        line(2, "// track the canvas " + assignment.name + ", which the intrinsic size reads (§11.3)");
                        line(2, member + " = " + emit_expression(*assignment.value) + ";");
                    }
                    // a bound dimension runs through a sink, which recomputes it when a source changes (§13.3)
                    else {
                        const std::string sink = "m_sink_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                        line(2, "// bind the canvas " + assignment.name + " over its sources, which recomputes it when one changes (§13.3)");
                        line(2, "const PropertyId deps_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + "[] = {" + dep_list(assignment.dependencies) + "};");
                        line(2, sink + ".bind_static(&Impl::evaluate_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + ", this, deps_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + ");");
                    }
                    canvas_written = true;
                    continue;
                }
                // the viewport extent is one setter for two properties, because the widget takes the pair (§8.2)
                if (slot.node->element_type == "ScrollView" && (assignment.name == "viewport_width" || assignment.name == "viewport_height")) {
                    // the identity decision is the member to write, which the two extents share (§11.3)
                    const std::string member = assignment.name == "viewport_width" ? "m_viewport_w_" + std::to_string(slot.index) : "m_viewport_h_" + std::to_string(slot.index);
                    // the extent is tracked per node, because set_viewport_extent takes the pair (§8.2)
                    line(2, "// track the " + assignment.name + ", which the viewport extent reads (§11.3)");
                    line(2, member + " = " + emit_expression(*assignment.value) + ";");
                    viewport_written = true;
                    // a constant extent is settled here, while a bound one needs a sink (§13.3)
                    if (assignment.dependencies.empty())
                        continue;
                    // a bound extent runs through a sink, which the evaluator keeps current (§13.3)
                    const std::string sink = "m_sink_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                    line(2, "// bind the " + assignment.name + " over its sources, which recomputes it when one changes (§13.3)");
                    line(2, "const PropertyId deps_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + "[] = {" + dep_list(assignment.dependencies) + "};");
                    line(2, sink + ".bind_static(&Impl::evaluate_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + ", this, deps_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + ");");
                    continue;
                }
                // a bound offset attaches to the widget property, which a user scroll outranks (§7.3, AD-24)
                if (slot.node->element_type == "ScrollView" && assignment.name == "offset" && !assignment.dependencies.empty()) {
                    line(2, "// bind the offset onto the widget property, which a user scroll outranks (§7.3, AD-24)");
                    line(2, "const PropertyId deps_" + std::to_string(slot.index) + "_offset[] = {" + dep_list(assignment.dependencies) + "};");
                    line(2, "n" + std::to_string(slot.index) + "->bind_offset(&Impl::evaluate_" + std::to_string(slot.index) + "_offset, this, deps_" + std::to_string(slot.index) + "_offset);");
                    continue;
                }
                // the slider range is one setter for two properties, because the widget takes both bounds (§8)
                if (slot.node->element_type == "Slider" && (assignment.name == "min" || assignment.name == "max")) {
                    // the missing bound is the default the widget already carries, so one written bound is enough (§8)
                    const std::string low = assignment.name == "min" ? emit_expression(*assignment.value) : "m_slider_min_" + std::to_string(slot.index);
                    const std::string high = assignment.name == "max" ? emit_expression(*assignment.value) : "m_slider_max_" + std::to_string(slot.index);
                    // the bounds are tracked per node, because set_range takes them together (§8)
                    line(2, "// track the slider " + assignment.name + ", which the range reads (§11.3)");
                    line(2, (assignment.name == "min" ? "m_slider_min_" : "m_slider_max_") + std::to_string(slot.index) + " = " + emit_expression(*assignment.value) + ";");
                    // the range is applied after both bounds, which is what the setter needs (§8)
                    range_written = true;
                    continue;
                }
                // a constant reaches the widget setter directly, which what a static value is (§14.1)
                if (assignment.dependencies.empty()) {
                    // the setter is fetched by the element and the property, which the catalog named (§11.3)
                    const std::string_view setter = setter_of(std::string_view(slot.node->element_type), assignment.name);
                    line(2, "// apply the static " + assignment.name + ", which is the value the author wrote (§14.1)");
                    line(2, "n" + std::to_string(slot.index) + "->" + std::string(setter) + "(" + emit_expression(*assignment.value) + ");");
                }
                // a bound assignment runs through its sink, which the evaluator keeps live (§13.3)
                else {
                    const std::string sink = "m_sink_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                    line(2, "// bind the " + assignment.name + " over its sources, which recomputes it when one changes (§13.3)");
                    line(2, "const PropertyId deps_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + "[] = {" + dep_list(assignment.dependencies) + "};");
                    line(2, sink + ".bind_static(&Impl::evaluate_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + ", this, deps_" + std::to_string(slot.index) + "_" + sanitize(assignment.name) + ");");
                }
            }
            // the canvas intrinsic size is applied once, after both tracks were written (§11.3)
            if (canvas_written)
                line(2, "n" + std::to_string(slot.index) + "->set_intrinsic_size(Size{m_canvas_w_" + std::to_string(slot.index) + ", m_canvas_h_" + std::to_string(slot.index) + "});");
            // the viewport extent is applied once, after both halves were written (§8.2)
            if (viewport_written)
                line(2, "n" + std::to_string(slot.index) + "->set_viewport_extent(Size{m_viewport_w_" + std::to_string(slot.index) + ", m_viewport_h_" + std::to_string(slot.index) + "});");
            // the slider range is applied once, after both bounds were written (§8)
            if (range_written)
                line(2, "n" + std::to_string(slot.index) + "->set_range(m_slider_min_" + std::to_string(slot.index) + ", m_slider_max_" + std::to_string(slot.index) + ");");
            // a canvas with a draw block installs its body, which the callback runs every frame (§6.4)
            if (!slot.node->draw_calls.empty()) {
                line(2, "// install the drawn body, which records the canvas content into the context (§6.4)");
                line(2, "n" + std::to_string(slot.index) + "->set_draw([this](RenderContext& context) { draw_" + std::to_string(slot.index) + "_body(this, context); });");
            }
            // each handler wires its callback, which a widget signal triggers (§11.3)
            for (const CheckedHandler& handler : slot.node->handlers) {
                // the callback setter comes from the element and the signal, which the catalog named (§11.3)
                const std::string_view setter = signal_setter_of(std::string_view(slot.node->element_type), handler.name);
                // the lambda carries the signal parameters by name, so the body reads what the widget raised (§11.3)
                const std::vector<std::pair<std::string, std::string>> named = named_signal_params(slot.node->element_type, handler.name);
                std::string lambda_parameters;
                std::string lambda_arguments;
                // each parameter joins both lists, because a lambda declares and forwards the same names (§11.3)
                for (const std::pair<std::string, std::string>& parameter : named) {
                    lambda_parameters += (lambda_parameters.empty() ? "" : ", ") + parameter.first + " " + parameter.second;
                    lambda_arguments += (lambda_arguments.empty() ? "" : ", ") + parameter.second;
                }
                // the wiring forwards every parameter, which is how the body reads the raised value (§11.3)
                const std::string lambda = "[this](" + lambda_parameters + ") { handle_" + std::to_string(slot.index) + "_" + sanitize(handler.name) + "(this" + (lambda_arguments.empty() ? "" : ", " + lambda_arguments) + "); }";
                line(2, "// wire the " + handler.name + " to the generated handler, which runs the written body (§11.3)");
                line(2, "n" + std::to_string(slot.index) + "->" + std::string(setter) + "(" + lambda + ");");
            }
            // the node is adopted by its parent, which is the only place the ownership moves (§8.1)
            if (slot.parent != NO_PARENT)
                line(2, "n" + std::to_string(slot.parent) + "->add_child(std::move(node_" + std::to_string(slot.index) + "));");
        }

        void ComponentEmitter::emit_row_node_block(const NodeSlot& slot) {
            // the class name decides the constructor, which the element carries (§8.1, §18.1)
            const WidgetInfo& info = widget_info().at(std::string_view(slot.node->element_type));
            // the constructor arguments depend on the engines the widget reads (§8.1, §18.1)
            std::string arguments = "()";
            // a widget construction pulls the engine, which the builder resolved above (§7.2, §18.1)
            if (info.needs_engine && info.needs_text_engine)
                arguments = "(engine, text_engine)";
            // a widget without a label only pulls the engine, which the other widgets take (§7.2, §18.1)
            else if (info.needs_engine)
                arguments = "(engine)";
            // the local is per row rather than per component, because every row owns its own nodes (§18.1, AD-13)
            line(2, "// allocate the row node, which is local because every row owns its own (§18.1, AD-13)");
            line(2, "auto node_" + std::to_string(slot.index) + " = std::make_unique<" + std::string(info.class_name) + ">" + arguments + ";");
            line(2, "// the node is reached through the raw pointer, which the unique pointer moves later (§18.1)");
            line(2, std::string(info.class_name) + "* const n" + std::to_string(slot.index) + " = node_" + std::to_string(slot.index) + ".get();");
            // the author identity is applied, which makes the node addressable (AD-11)
            if (!slot.node->explicit_id.empty()) {
                line(2, "// assign the author identity, which everything selects by (AD-11)");
                line(2, "n" + std::to_string(slot.index) + "->set_explicit_id(std::string(\"" + slot.node->explicit_id + "\"));");
            }
            // every assignment is written straight into the widget, because a row has no sink of its own (§18.1)
            for (const CheckedAssignment& assignment : slot.node->assignments) {
                // the canvas tracks are local too, because one row must not overwrite another's (§11.3, §18.1)
                if (slot.node->element_type == "Canvas" && (assignment.name == "width" || assignment.name == "height")) {
                    const bool width = assignment.name == "width";
                    line(2, "// track the canvas " + assignment.name + ", which the intrinsic size reads (§11.3, §18.1)");
                    line(2, std::string(width ? "m_row_canvas_w_" : "m_row_canvas_h_") + std::to_string(slot.index) + " = " + emit_expression(*assignment.value) + ";");
                    continue;
                }
                // the setter is fetched by the element and the property, which the catalog named (§11.3)
                const std::string_view setter = setter_of(std::string_view(slot.node->element_type), assignment.name);
                line(2, "// apply the " + assignment.name + ", which is what this row shows (§18.1)");
                line(2, "n" + std::to_string(slot.index) + "->" + std::string(setter) + "(" + emit_expression(*assignment.value) + ");");
            }
            // a canvas row publishes its declared size once both tracks were written (§11.3, §18.1)
            if (slot.node->element_type == "Canvas")
                line(2, "n" + std::to_string(slot.index) + "->set_intrinsic_size(Size{m_row_canvas_w_" + std::to_string(slot.index) + ", m_row_canvas_h_" + std::to_string(slot.index) + "});");
            // each handler wires its callback, because a row handler runs the written body (§11.3, §18.1)
            for (const CheckedHandler& handler : slot.node->handlers) {
                // the callback setter comes from the element and the signal, which the catalog named (§11.3)
                const std::string_view setter = signal_setter_of(std::string_view(slot.node->element_type), handler.name);
                // the lambda carries the signal parameters by name, so the body reads what the widget raised (§11.3)
                const std::vector<std::pair<std::string, std::string>> named = named_signal_params(slot.node->element_type, handler.name);
                std::string lambda_parameters;
                std::string lambda_arguments;
                // each parameter joins both lists, because a lambda declares and forwards the same names (§11.3)
                for (const std::pair<std::string, std::string>& parameter : named) {
                    lambda_parameters += (lambda_parameters.empty() ? "" : ", ") + parameter.first + " " + parameter.second;
                    lambda_arguments += (lambda_arguments.empty() ? "" : ", ") + parameter.second;
                }
                // the handler body is the written one, which is what the row runs (§11.3, §18.1)
                const std::string lambda = "[this](" + lambda_parameters + ") { handle_" + std::to_string(slot.index) + "_" + sanitize(handler.name) + "(this" + (lambda_arguments.empty() ? "" : ", " + lambda_arguments) + "); }";
                line(2, "// wire the " + handler.name + ", which runs the written handler for this row (§11.3, §18.1)");
                line(2, "n" + std::to_string(slot.index) + "->" + std::string(setter) + "(" + lambda + ");");
            }
            // the node is adopted by its row parent, which is the only place the ownership moves (§8.1, §18.1)
            if (slot.parent != NO_PARENT)
                line(2, "n" + std::to_string(slot.parent) + "->add_child(std::move(node_" + std::to_string(slot.index) + "));");
        }

        void ComponentEmitter::emit_row_update_block(const NodeSlot& slot) {
            // the canvas tracks are declared first, because a canvas publishes them together (§11.3, §18.1)
            if (slot.node->element_type == "Canvas") {
                line(2, "// the canvas tracks are row local, because one row must not overwrite another (§11.3, §18.1)");
                line(2, "float m_row_canvas_w_" + std::to_string(slot.index) + " = 0.0f;");
                line(2, "float m_row_canvas_h_" + std::to_string(slot.index) + " = 0.0f;");
            }
            // every assignment is rewritten, because a matched row must show the item it now holds (§18.1, AD-13)
            for (const CheckedAssignment& assignment : slot.node->assignments) {
                // the canvas tracks are rewritten first, because one setter combines them (§11.3, §18.1)
                if (slot.node->element_type == "Canvas" && (assignment.name == "width" || assignment.name == "height")) {
                    const bool width = assignment.name == "width";
                    line(2, "// track the canvas " + assignment.name + ", which the intrinsic size reads (§11.3, §18.1)");
                    line(2, std::string(width ? "m_row_canvas_w_" : "m_row_canvas_h_") + std::to_string(slot.index) + " = " + emit_expression(*assignment.value) + ";");
                    continue;
                }
                // the setter is the same one the build used, because both paths write the same widget (§11.3)
                const std::string_view setter = setter_of(std::string_view(slot.node->element_type), assignment.name);
                line(2, "// rewrite the " + assignment.name + ", because the row now shows another item (§18.1, AD-13)");
                line(2, "n" + std::to_string(slot.index) + "->" + std::string(setter) + "(" + emit_expression(*assignment.value) + ");");
            }
            // a canvas row publishes its declared size once both tracks were written (§11.3, §18.1)
            if (slot.node->element_type == "Canvas")
                line(2, "n" + std::to_string(slot.index) + "->set_intrinsic_size(Size{m_row_canvas_w_" + std::to_string(slot.index) + ", m_row_canvas_h_" + std::to_string(slot.index) + "});");
        }

        std::string ComponentEmitter::repeat_source_expression(const RepeatSlot& slot) {
            // the source is read through the body, because the evaluator runs inside it (§18.1, AD-13)
            m_object = "self";
            // the item binding is not in scope here, because the source is a component property (§8.1, §18.1)
            m_repeat_item_name.clear();
            m_repeat_index_name.clear();
            // the rendered source is what the evaluator reads, which is the property the author named (§8.1, §18.1)
            const std::string rendered = emit_expression(*slot.node->repeat_source);
            m_object.clear();
            return rendered;
        }

        std::vector<std::string> ComponentEmitter::repeat_dependencies(const RepeatSlot& slot) {
            // the dependencies start as the source's own, because a change materializes rows (§18.1, AD-13)
            std::vector<std::string> dependencies = slot.node->repeat_dependencies;
            // a row body that reads a component property needs a reconcile when that property changes (§18.1, AD-13)
            for (const NodeSlot& body : slot.body) {
                // each assignment contributes, because a row shows whatever its bindings compute (§13.3, §18.1)
                for (const CheckedAssignment& assignment : body.node->assignments) {
                    // each source joins once, because the engine keeps a set (§7.3, AD-04a)
                    for (const std::string& source : assignment.dependencies) {
                        // a duplicate would register the same id twice (§7.3, AD-04a)
                        if (std::find(dependencies.begin(), dependencies.end(), source) == dependencies.end())
                            dependencies.push_back(source);
                    }
                }
            }
            return dependencies;
        }

        void ComponentEmitter::assign_repeat_offsets() {
            // each repeat needs the child position its first row takes, which is where it was written (§18.1, AD-13)
            for (RepeatSlot& repeat : m_repeats) {
                // a repeat with no parent has no container to count, which the checker already refused (§11.1)
                if (repeat.parent == NO_PARENT) {
                    // the offset stays zero, because an orphan is reported rather than emitted (§18.1)
                    repeat.offset = 0;
                    continue;
                }
                // the parent is located, because its children decide where the rows begin (§8.1, §18.1)
                const NodeSlot* parent = nullptr;
                for (const NodeSlot& slot : m_nodes) {
                    // the index decides, so the parent is the one this repeat hangs from (§8.1, §18.1)
                    if (slot.index == repeat.parent)
                        parent = &slot;
                }
                // a repeat whose parent is missing cannot place its rows, which would be a generator defect (§18.1)
                if (parent == nullptr) {
                    repeat.offset = 0;
                    continue;
                }
                // the ordinary children before the repeat are counted, because the rows start after them (§8.1, §18.1)
                std::size_t preceding = 0;
                // the siblings are walked in order, which is what fixes the row position (§8.1, §18.1)
                for (const std::shared_ptr<CheckedNode>& child : parent->node->children) {
                    // the first repeat sibling ends the count, because that is where the rows begin (§18.1, AD-13)
                    if (child->kind == TreeNodeKind::Repeat)
                        break;
                    preceding++;
                }
                repeat.offset = preceding;
            }
        }

        void ComponentEmitter::emit_repeat_row_functions_all() {
            // each repeat emits its own three functions, because they share the body node names (§18.1, AD-13)
            for (const RepeatSlot& slot : m_repeats)
                emit_repeat_row_functions(slot);
        }

        void ComponentEmitter::emit_repeat_row_functions(const RepeatSlot& slot) {
            // the suffix names every function of this repeat, which keeps them unique across repeats (§18.1)
            const std::string at = "_" + std::to_string(slot.index);
            // the item type is the element of the source, which every row function receives (§8.1, §18.1)
            const std::string item = item_type(slot);
            // the key field is the manifest declaration the row is keyed by (§18.1, AD-13)
            const RecordFieldType* key = item_key_field(slot);
            // the author bindings decide which names render as the row parameters (§11.1, §18.1)
            m_repeat_item_name = slot.node->repeat_item_name;
            m_repeat_index_name = slot.node->repeat_index_name;

            line(0, "");
            line(1, "std::unique_ptr<UiNode> " + m_class + "::Impl::build_row" + at + "(void* raw, const " + item + "& item, std::size_t index) {");
            line(2, "// the body is recovered from the context, which keeps the row path allocation free (§13.3)");
            line(2, "auto* self = static_cast<Impl*>(raw);");
            m_object = "self";
            line(2, "// the engine is resolved once, because a widget construction takes it (§7.2, §18.1)");
            line(2, "BindingEngine* const engine = self->owner->engine();");
            line(2, "// the text engine is resolved once, because a label measures through it (§9.1, §18.1)");
            line(2, "TextEngine* const text_engine = self->text_engine;");
            // the index is marked used, because a body that binds no index would otherwise leave it unused (§11.1, §18.1)
            if (slot.node->repeat_index_name.empty())
                line(2, "(void)index;");
            // the body is built in preorder, which hangs one row together exactly as written (§8.1, §18.1)
            for (const NodeSlot& body : slot.body)
                emit_row_node_block(body);
            line(2, "// the row root is handed back, because the view owns it from here (§18.1)");
            line(2, slot.body.empty() ? "return nullptr;" : "return std::move(node_" + std::to_string(slot.body.front().index) + ");");
            line(1, "}");
            m_object.clear();

            line(0, "");
            line(1, "void " + m_class + "::Impl::update_row" + at + "(void* raw, UiNode& row, const " + item + "& item, std::size_t index) {");
            line(2, "// the body is recovered from the context, which keeps the row path allocation free (§13.3)");
            line(2, "auto* self = static_cast<Impl*>(raw);");
            m_object = "self";
            line(2, "// the row root is the node the view owns, which every body node is reached from (§18.1, AD-13)");
            line(2, "UiNode* const row_root = &row;");
            // the index is marked used, because a body that binds no index would otherwise leave it unused (§11.1, §18.1)
            if (slot.node->repeat_index_name.empty())
                line(2, "(void)index;");
            // each body node is reached by walking down from the row root, the root included (§18.1, AD-13)
            for (const NodeSlot& body : slot.body)
                emit_row_walk(body);
            // every body assignment is rewritten, because a matched row shows the item it now holds (§18.1, AD-13)
            for (const NodeSlot& body : slot.body)
                emit_row_update_block(body);
            line(2, "// the body is recovered only to keep the generated signature uniform (§13.3)");
            line(2, "(void)self;");
            line(1, "}");
            m_object.clear();

            line(0, "");
            line(1, "albor::model::ItemKey " + m_class + "::Impl::key_of" + at + "(void* raw, const " + item + "& item) {");
            line(2, "// the context is recovered only to keep the generated signature uniform (§13.3)");
            line(2, "(void)static_cast<Impl*>(raw);");
            // the identity is the manifest field, because a row is keyed by a declared field (§18.1, AD-13)
            line(2, "// the identity is the manifest field the record marked, which is what a row is keyed by (§18.1, AD-13)");
            line(2, "return albor::model::ItemKey(item." + key->name + ");");
            line(1, "}");
            m_repeat_item_name.clear();
            m_repeat_index_name.clear();
        }

        void ComponentEmitter::emit_row_walk(const NodeSlot& slot) {
            // the concrete class is what the walk casts to, because an assignment needs the widget setter (§11.3, §18.1)
            const std::string cls(widget_info().at(std::string_view(slot.node->element_type)).class_name);
            // the body root is the node the view handed back, so its walk is a cast rather than a descent (§18.1, AD-13)
            if (slot.row_path.empty()) {
                line(2, "// the body root is the row itself, which the view handed back (§18.1, AD-13)");
                line(2, cls + "* const n" + std::to_string(slot.index) + " = static_cast<" + cls + "*>(row_root);");
                return;
            }
            // the parent is the node one step up, because preorder guarantees it was reached already (§8.1, §18.1)
            const std::size_t parent = slot.row_path.at(slot.row_path.size() - 2);
            // the step is the child position under the parent, which the walk recorded (§8.1, §18.1)
            const std::size_t position = slot.row_path.back();
            // the node is reached by its child position, which keeps the shape the author wrote (§18.1)
            line(2, cls + "* const n" + std::to_string(slot.index) + " = static_cast<" + cls + "*>(static_cast<UiNode*>(n" + std::to_string(parent) + ")->child_at(" + std::to_string(position) + "));");
        }

        void ComponentEmitter::emit_repeat_reconcile(const RepeatSlot& slot) {
            // the suffix names every function of this repeat, which keeps them unique across repeats (§18.1)
            const std::string at = "_" + std::to_string(slot.index);
            // a windowed repeat hangs under a viewport, so it owns its own rows (§8.2, AD-13)
            if (slot.virtualized) {
                // the view is built in a local, because the tree owns it once it is adopted (§8.2, AD-13)
                const std::string rows = "rows" + at;
                line(2, "// the windowed view owns the rows, keyed by the manifest identity (§8.2, AD-13)");
                line(2, "auto " + rows + " = std::make_unique<albor::model::ScrollableRows<" + item_type(slot) + ">>(0.0f, 64);");
                line(2, "// the row height comes from the viewport, which makes an index a position (§8.2, AD-13)");
                line(2, rows + "->set_row_extent(n" + std::to_string(slot.parent) + "->row_height());");
                line(2, "// the three plain functions the rows are built and matched through (§13.3, §18.1)");
                line(2, rows + "->set_view(&Impl::build_row" + at + ", &Impl::update_row" + at + ", &Impl::key_of" + at + ", this);");
                line(2, "// the rows are reconciled now, so the tree is complete at construction (§18.1)");
                line(2, "m_repeat_items" + at + " = m_repeat_source" + at + ".get();");
                line(2, rows + "->set_items(m_repeat_items" + at + ");");
                line(2, "// the sink joins the component scope, which gives the reconcile a unique id (§13.3, §18.1)");
                line(2, "owner->register_property(m_repeat_source" + at + ");");
                line(2, "// the binding is declared here, because the evaluator below writes through it (§13.3, §18.1)");
                line(2, "const PropertyId deps_repeat" + at + "[] = {" + dep_list(repeat_dependencies(slot)) + "};");
                line(2, "m_repeat_source" + at + ".bind_static(&Impl::evaluate_repeat" + at + ", this, deps_repeat" + at + ");");
                line(2, "// the source reaches the rows, which borrow a member that outlives them (§8.2, AD-13)");
                line(2, rows + "->set_items(m_repeat_items" + at + ");");
                line(2, "// the viewport adopts the view, which is what hands ownership to the tree (§8.2, AD-13)");
                line(2, "n" + std::to_string(slot.parent) + "->add_child(std::move(" + rows + "));");
                line(2, "// the body keeps a view onto the rows, because the tree owns them from here (§8.2, AD-13)");
                line(2, "m_repeat" + at + " = static_cast<albor::model::ScrollableRows<" + item_type(slot) + ">*>(n" + std::to_string(slot.parent) + "->child_at(0));");
                return;
            }
            // the view is built against the container, because its rows hang where the repeat was written (§8.1, §18.1)
            line(2, "// the view owns the rows this repeat materializes, keyed by the manifest identity (§18.1, AD-13)");
            line(2, "m_repeat" + at + " = std::make_unique<albor::model::KeyedView<" + item_type(slot) + ">>(n" + std::to_string(slot.parent) + ", " + std::to_string(slot.offset) + ");");
            line(2, "// the rows are reconciled now, so the tree is complete at construction (§18.1)");
            reconcile(slot);
            // the sink joins the component scope, which gives the reconcile a unique id (§13.3, §18.1)
            line(2, "owner->register_property(m_repeat_source" + at + ");");
            // the binding is declared here, because the evaluator below writes through it (§13.3, §18.1)
            line(2, "const PropertyId deps_repeat" + at + "[] = {" + dep_list(repeat_dependencies(slot)) + "};");
            line(2, "m_repeat_source" + at + ".bind_static(&Impl::evaluate_repeat" + at + ", this, deps_repeat" + at + ");");
        }

        void ComponentEmitter::reconcile(const RepeatSlot& slot) {
            // the suffix names every function of this repeat, which keeps them unique across repeats (§18.1)
            const std::string at = "_" + std::to_string(slot.index);
            // the source is read through the body, because the reconcile runs inside it (§18.1, AD-13)
            m_object = "this";
            // the item binding is not in scope here, because the source is a component property (§8.1, §18.1)
            m_repeat_item_name.clear();
            m_repeat_index_name.clear();
            // the rows are reconciled against the source, matching by identity (§18.1, AD-13)
            if (slot.virtualized) {
                line(2, "m_repeat_items" + at + " = m_repeat_source" + at + ".get();");
                line(2, "m_repeat" + at + "->set_items(m_repeat_items" + at + ");");
                m_object.clear();
                return;
            }
            line(2, "m_repeat" + at + "->reconcile(this, " + emit_expression(*slot.node->repeat_source) + ", &Impl::build_row" + at + ", &Impl::update_row" + at + ", &Impl::key_of" + at + ");");
            m_object.clear();
        }

        void ComponentEmitter::emit_repeat_members() {
            // each repeat contributes a view and three row functions, which is what a keyed row needs (§18.1, AD-13)
            for (const RepeatSlot& slot : m_repeats) {
                // the suffix names every member of this repeat, which keeps them unique across repeats (§18.1)
                const std::string at = "_" + std::to_string(slot.index);
                // the item type is the element of the source, which the view is keyed over (§8.1, §18.1)
                const std::string item = item_type(slot);
                line(0, "");
                // a windowed repeat views the rows the tree owns, because only the window is built (§8.2, AD-13)
                if (slot.virtualized) {
                    line(2, "// the view onto the windowed rows, which the tree owns as the viewport's child (§8.2, AD-13)");
                    line(2, "albor::model::ScrollableRows<" + item + ">* m_repeat" + at + " = nullptr;");
                    line(0, "");
                    line(2, "// the source the rows borrow, which the body owns so it outlives every frame (§8.2, AD-13)");
                    line(2, "std::vector<" + item + "> m_repeat_items" + at + ";");
                }
                // an unwindowed repeat hangs its rows from the container it was written in (§18.1, AD-13)
                else {
                    line(2, "// the view over the rows this repeat materializes, which owns them keyed (§18.1, AD-13)");
                    line(2, "std::unique_ptr<albor::model::KeyedView<" + item + ">> m_repeat" + at + ";");
                }
                line(0, "");
                line(2, "// materialize one row from its item, which is what a new key builds (§18.1, AD-13)");
                line(2, "static std::unique_ptr<UiNode> build_row" + at + "(void* raw, const " + item + "& item, std::size_t index);");
                line(0, "");
                line(2, "// rewrite one matched row from the item it now shows, which is what a moved key does (§18.1, AD-13)");
                line(2, "static void update_row" + at + "(void* raw, UiNode& row, const " + item + "& item, std::size_t index);");
                line(0, "");
                line(2, "// read the identity an item carries, which is the field the manifest marked (§18.1, AD-13)");
                line(2, "static albor::model::ItemKey key_of" + at + "(void* raw, const " + item + "& item);");
                line(0, "");
                line(2, "// reconcile the rows against the source, which runs when the source changes (§18.1, AD-13)");
                line(2, "void reconcile" + at + "();");
                line(0, "");
                line(2, "// the source the rows are reconciled against, which the engine keeps current (§18.1, §13.3)");
                line(2, "Property<std::vector<" + item + ">> m_repeat_source" + at + ";");
                line(0, "");
                line(2, "// recompute the rows when any source they read changes, which is what keeps them live (§18.1)");
                line(2, "static std::vector<" + item + "> evaluate_repeat" + at + "(void* raw);");
            }
        }

        void ComponentEmitter::emit_evaluators() {
            // the evaluator output is a set of functions, which the implementation unit appends (§13.3)
            for (const NodeSlot& slot : m_nodes) {
                // the evaluator is walked per assignment, which matches the bind above (§13.3)
                for (const CheckedAssignment& assignment : slot.node->assignments) {
                    // a constant assignment has no evaluator, because the builder applies it directly (§13.3)
                    if (assignment.dependencies.empty())
                        continue;
                    const std::string evaluator = "evaluate_" + std::to_string(slot.index) + "_" + sanitize(assignment.name);
                    line(0, "");
                    line(1, cpp_type(assignment.type) + " " + m_class + "::Impl::" + evaluator + "(void* raw) {");
                    line(2, "// the body is recovered from the context, which keeps the path allocation free (§13.3)");
                    line(2, "auto* self = static_cast<Impl*>(raw);");
                    m_object = "self";
                    line(2, "// the value is computed once, which the widget and the sink both take (§13.3)");
                    line(2, "const " + cpp_type(assignment.type) + " value = " + emit_expression(*assignment.value) + ";");
                    // the widget setter receives the value, which is what makes the binding visible (§13.3)
                    if (slot.node->element_type == "Canvas" && (assignment.name == "width" || assignment.name == "height")) {
                        // the width track updates first, which the intrinsic size then combines (§13.3)
                        const bool width = assignment.name == "width";
                        line(2, "// the " + std::string(width ? "width" : "height") + " track updates, which the intrinsic size reads (§11.3)");
                        line(2, "self->m_canvas_" + std::string(width ? "w" : "h") + "_" + std::to_string(slot.index) + " = value;");
                        line(2, "// the canvas resizes, which is what the binding changes (§11.3)");
                        line(2, "self->m_node_" + std::to_string(slot.index) + "->set_intrinsic_size(Size{self->m_canvas_w_" + std::to_string(slot.index) + ", self->m_canvas_h_" + std::to_string(slot.index) + "});");
                    }
                    // a bound viewport extent updates its track and re-applies the pair (§8.2, §11.3)
                    else if (slot.node->element_type == "ScrollView" && (assignment.name == "viewport_width" || assignment.name == "viewport_height")) {
                        // the width track updates first, which the viewport extent then combines (§11.3)
                        const bool width = assignment.name == "viewport_width";
                        line(2, "// the " + std::string(width ? "width" : "height") + " track updates, which the viewport reads (§11.3)");
                        line(2, "self->m_viewport_" + std::string(width ? "w" : "h") + "_" + std::to_string(slot.index) + " = value;");
                        line(2, "self->m_node_" + std::to_string(slot.index) + "->set_viewport_extent(Size{self->m_viewport_w_" + std::to_string(slot.index) + ", self->m_viewport_h_" + std::to_string(slot.index) + "});");
                    }
                    // a bound offset writes the widget property, so the evaluator only produces the value (§7.3)
                    else if (slot.node->element_type == "ScrollView" && assignment.name == "offset") {
                        line(2, "// the widget property takes this value, so nothing is set here (§7.3, AD-24)");
                        line(2, "(void)value;");
                    }
                    else {
                        const std::string_view setter = setter_of(std::string_view(slot.node->element_type), assignment.name);
                        line(2, "// the widget shows the computed value, which is what makes the binding visible (§13.3)");
                        line(2, "self->m_node_" + std::to_string(slot.index) + "->" + std::string(setter) + "(value);");
                    }
                    line(2, "// the computed value is returned, which the engine stores as the bound value (§13.3)");
                    line(2, "return value;");
                    line(1, "}");
                    m_object.clear();
                }
            }
        }

        void ComponentEmitter::emit_computes() {
            // the evaluator output is a set of functions, which the implementation unit appends (§13.3)
            for (const CheckedProperty& property : m_component.properties) {
                // a default with no sources is applied directly, so it needs no evaluator (§13.3)
                if (!property.has_default || property.default_dependencies.empty())
                    continue;
                // the evaluator is named after the property, which the builder registered (§13.3)
                const std::string& name = m_names.at(property.name);
                line(0, "");
                line(1, cpp_type(property.type) + " " + m_class + "::Impl::compute_" + name + "(void* raw) {");
                line(2, "// the body is recovered from the context, which keeps the path allocation free (§13.3)");
                line(2, "auto* self = static_cast<Impl*>(raw);");
                m_object = "self";
                line(2, "// the fresh value is computed, which the engine stores in the property (§13.3)");
                line(2, "const " + cpp_type(property.type) + " value = " + emit_expression(*property.default_value) + ";");
                line(2, "// the computed value is returned, which the engine records as the bound value (§13.3)");
                line(2, "return value;");
                line(1, "}");
                m_object.clear();
            }
        }

        void ComponentEmitter::emit_handlers() {
            // the handler output is a set of functions, which the implementation unit appends (§11.3)
            for (const NodeSlot& slot : m_nodes) {
                // each handler is emitted on its own, which the builder wired above (§11.3)
                for (const CheckedHandler& handler : slot.node->handlers) {
                    const std::string handle = "handle_" + std::to_string(slot.index) + "_" + sanitize(handler.name);
                    line(0, "");
                    line(1, "void " + m_class + "::Impl::" + handle + "(void* raw" + handler_signature(slot.node->element_type, handler.name) + ") {");
                    line(2, "// the body is recovered from the context, which keeps the path allocation free (§13.3)");
                    line(2, "auto* self = static_cast<Impl*>(raw);");
                    m_object = "self";
                    // the signal parameters are readable as names, because they are the arguments this body took (§11.1)
                    m_signal_parameters.clear();
                    for (const std::pair<std::string, std::string>& parameter : named_signal_params(slot.node->element_type, handler.name))
                        m_signal_parameters.push_back(parameter.second);
                    // each statement is emitted as a property write or a signal invocation (§11.3)
                    for (const CheckedStatement& statement : handler.body) {
                        // an assignment writes the target property, which the engine settles (§11.3)
                        if (statement.kind == HandlerStatementKind::Assignment) {
                            line(2, "// the written assignment runs, which updates the component property (§11.3)");
                            line(2, "self->m_" + sanitize(statement.target) + ".set(" + emit_expression(*statement.value) + ");");
                        }
                        // an invocation reports the slot, which the host installed (§11.3)
                        else {
                            std::string arguments;
                            // each argument appends its own text, which is the invocation order (§11.3)
                            for (const SharedExpression& argument : statement.arguments) {
                                // a comma joins the earlier arguments, which is how a call reads (§11.3)
                                if (!arguments.empty())
                                    arguments += ", ";
                                arguments += emit_expression(*argument);
                            }
                            line(2, "// the written invocation runs, which reports the component signal (§11.3)");
                            line(2, "if (self->m_on_" + sanitize(statement.target) + ")");
                            line(3, "self->m_on_" + sanitize(statement.target) + "(" + arguments + ");");
                        }
                    }
                    line(1, "}");
                    m_object.clear();
                    m_signal_parameters.clear();
                }
            }
        }

        void ComponentEmitter::emit_draw_bodies() {
            // the draw output is one function per canvas, which the builder wired above (§6.4)
            for (const NodeSlot& slot : m_nodes) {
                // a node with no draw block contributes nothing, because it paints itself (§6.4)
                if (slot.node->draw_calls.empty())
                    continue;
                // the body is named after the node, because a canvas may write several (§6.4)
                const std::string body = "draw_" + std::to_string(slot.index) + "_body";
                line(0, "");
                line(1, "void " + m_class + "::Impl::" + body + "(void* raw, RenderContext& context) {");
                line(2, "// the body is recovered from the context, which keeps the path allocation free (§13.3)");
                line(2, "auto* self = static_cast<Impl*>(raw);");
                line(2, "// the canvas extent is read here, because a drawing is laid out in the node (§6.4)");
                line(2, "const float canvas_width = self->m_node_" + std::to_string(slot.index) + "->bounds().width;");
                line(2, "const float canvas_height = self->m_node_" + std::to_string(slot.index) + "->bounds().height;");
                // the origin is the node corner, because a recorded rect is absolute in the scene (§6.1, §6.2)
                line(2, "const Rect canvas_bounds = self->m_node_" + std::to_string(slot.index) + "->bounds();");
                line(2, "const Point canvas_origin{canvas_bounds.x, canvas_bounds.y};");
                m_object = "self";
                // the extent names resolve inside the body, because that is where the layout answers (§6.4)
                m_draw_extent = true;
                // the paint is declared once, because a block fills and then draws with it (§6.2)
                bool painted = false;
                // each call emits one primitive, which is the vocabulary the checker matched (§6.4)
                for (const CheckedDrawCall& call : slot.node->draw_calls) {
                    // the colour is the last argument, because every primitive ends with its paint (§6.4)
                    std::string arguments;
                    for (std::size_t index = 0; index + 1 < call.arguments.size(); index++)
                        arguments += (arguments.empty() ? "" : ", ") + emit_expression(*call.arguments[index]);
                    const std::string colour = emit_expression(*call.arguments.back());
                    // the numbers are split into origin and size, which is what the rect is built from (§6.4)
                    std::vector<std::string> numbers;
                    std::string current;
                    for (const char character : arguments) {
                        // a comma closes one number, because the list is written flat (§6.4)
                        if (character == ',') {
                            numbers.push_back(current);
                            current.clear();
                            continue;
                        }
                        // every other character belongs to the number being read (§6.4)
                        if (character != ' ')
                            current.push_back(character);
                    }
                    if (!current.empty())
                        numbers.push_back(current);
                    // the origin is the canvas corner plus the written offset, because the scene is absolute (§6.2)
                    const std::string rect_value = numbers.size() >= 4 ? "Rect::from_origin_size(Point{canvas_origin.x + " + numbers[0] + ", canvas_origin.y + " + numbers[1] + "}, Size{" + numbers[2] + ", " + numbers[3] + "})" : "Rect::from_origin_size(canvas_origin, Size{0, 0})";
                    // the paint is declared once for the block, because a primitive fills and then draws (§6.2)
                    if (!painted) {
                        // the declaration reads before the first primitive, which is where the paint lives (§6.2)
                        line(2, "// the paint every primitive records, which the draw block writes (§6.2)");
                        line(2, "Paint paint;");
                        painted = true;
                    }
                    line(2, "// the " + call.primitive + " primitive records its rect and paint (§6.4)");
                    line(2, "paint.fill = " + colour + ";");
                    if (call.primitive == "fill_rect") {
                        line(2, "context.draw_rect(" + rect_value + ", paint);");
                    }
                    else {
                        // the radius is the fifth number, because the rounded primitive takes one more (§6.4)
                        const std::string radius = numbers.size() >= 5 ? numbers[4] : "0.0f";
                        line(2, "context.draw_rounded_rect(" + rect_value + ", " + radius + ", paint);");
                    }
                }
                m_draw_extent = false;
                m_object.clear();
                line(1, "}");
            }
        }

        void ComponentEmitter::emit_float_helper() {
            line(0, "");
            line(1, "// render a float as the shortest text that reads it back, which is what a readout shows (§11.3)");
            line(1, "std::string " + m_stem + "_float_to_text(float value) {");
            line(2, "// six digits is the shortest form that reads a float back exactly, which is what %g guarantees (§11.3)");
            line(2, "std::array<char, 32> buffer{};");
            line(2, "const int written = std::snprintf(buffer.data(), buffer.size(), \"%g\", value);");
            line(2, "if (written <= 0 || static_cast<std::size_t>(written) >= buffer.size())");
            line(3, "return std::to_string(value);");
            // the text ends where snprintf says, because the array may hold more (§11.3)
            line(2, "std::string text(buffer.data(), static_cast<std::size_t>(written));");
            line(2, "// a whole value renders without a decimal point, because a readout should not say 2 (§11.3)");
            line(2, "if (text.find('.') == std::string::npos && text.find('e') == std::string::npos && text.find('E') == std::string::npos)");
            line(3, "text += \".0\";");
            line(2, "return text;");
            line(1, "}");
        }

        void ComponentEmitter::emit_color_helper() {
            line(0, "");
            line(1, "// render a colour as hex text, which is what the string conversion produces (§11.3)");
            line(1, "std::string " + m_stem + "_color_to_hex(Color value) {");
            line(2, "// the red channel is printed first, which is the digit pair at the front (§11.3)");
            line(2, "const unsigned int red = static_cast<unsigned int>(value.red * 255.0f + 0.5f);");
            line(2, "// the green channel follows the red, which is the digit pair in the middle (§11.3)");
            line(2, "const unsigned int green = static_cast<unsigned int>(value.green * 255.0f + 0.5f);");
            line(2, "// the blue channel follows the green, which is the digit pair at the end (§11.3)");
            line(2, "const unsigned int blue = static_cast<unsigned int>(value.blue * 255.0f + 0.5f);");
            line(2, "// the buffer holds the six digits and the terminator, which is the whole text (§11.3)");
            line(2, "char buffer[8];");
            line(2, "// the channels are printed, which is the #RRGGBB spelling of §11.1 (§11.3)");
            line(2, "std::snprintf(buffer, sizeof(buffer), \"#%02X%02X%02X\", red, green, blue);");
            line(2, "// the text is returned, which concatenation and the label both take (§11.3)");
            line(2, "return std::string(buffer);");
            line(1, "}");
        }

        std::string ComponentEmitter::emit_expression(const Expression& expression) {
            // a literal renders from its value, which carries its own spelling (§11.3)
            if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression))
                return emit_literal(*literal);
            // a translation is resolved through the catalog, which is what makes @tr mean something (§9.2, §11.3)
            if (const auto* translation = dynamic_cast<const TranslationExpression*>(&expression))
                return "albor::i18n::translate(" + emit_string_literal(translation->text) + ")";
            // a reference reads the property member, which is the reactive value (§11.3)
            if (const auto* reference = dynamic_cast<const PropertyExpression*>(&expression)) {
                // a repeat item is the row parameter, because the body binds it for the row being built (§11.1, §18.1)
                if (!m_repeat_item_name.empty() && reference->name == m_repeat_item_name)
                    return "item";
                // a repeat index is the row position, because the body binds it for the row being built (§11.1, §18.1)
                if (!m_repeat_index_name.empty() && reference->name == m_repeat_index_name)
                    return "index";
                // the canvas extent is the arranged node, which only a draw body reads (§6.4)
                if (m_draw_extent && reference->name == "canvas_width")
                    return "canvas_width";
                if (m_draw_extent && reference->name == "canvas_height")
                    return "canvas_height";
                // a signal parameter is the handler argument, which the catalog names for the body (§11.1)
                for (const std::string& parameter : m_signal_parameters) {
                    // the name decides, so a parameter resolves to the argument the lambda forwarded (§11.3)
                    if (parameter == reference->name)
                        return parameter;
                }
                return m_object + "->m_" + sanitize(reference->name) + ".get()";
            }
            // a conversion renders through its operand, which the operand type decides (§11.3)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression))
                return emit_conversion(*conversion);
            // a colour operation renders as the method call it names, which the core colour carries (§6.1)
            if (const auto* call = dynamic_cast<const CallExpression*>(&expression))
                return emit_call(*call);
            // a binary application renders its operands, which keep the written parentheses (§11.3)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
                return emit_binary(*binary);
            // a unary application renders its operand, which keeps the written parentheses (§11.3)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
                return emit_unary(*unary);
            // a grouping re-emits its inner expression, which keeps the written parentheses (§11.3)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
                return "(" + emit_expression(*grouping->inner) + ")";
            // a bracketed list renders as a braced initialiser, whose element type comes from the checker (§8.1)
            if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression))
                return emit_array(*array);
            // a field read renders as the member access the record declares (§11.2, AD-17)
            if (const auto* access = dynamic_cast<const FieldAccessExpression*>(&expression)) {
                // a written value renders as the enumerator the set declares, which the checker resolved (§11.1, §8.2)
                if (const auto* set_name = dynamic_cast<const PropertyExpression*>(access->object.get()); set_name != nullptr) {
                    // the name decides, so a written value resolves to exactly one set (§11.1)
                    if (const NamedValueSet* set = find_named_value_set(set_name->name); set != nullptr) {
                        // the member decides the enumerator, because the two spellings differ (§11.3, §8.2)
                        if (const NamedValueMember* member = set->find_member(access->field); member != nullptr)
                            return "albor::" + (set->cpp_name.empty() ? set->name : set->cpp_name) + "::" + member->code;
                    }
                }
                return emit_expression(*access->object) + "." + access->field;
            }
            // a form the checker rejected cannot render, which the empty literal stands in for (§11.3)
            return emit_string_literal({});
        }

        std::string ComponentEmitter::emit_literal(const LiteralExpression& literal) {
            // an integer is emitted as written, because its digits are the value (§11.3)
            if (literal.kind == LiteralExpression::Kind::Integer)
                return literal.text.empty() ? "0" : literal.text;
            // a float is emitted as the shortest round trip, which reads the hex form too (§11.3)
            if (literal.kind == LiteralExpression::Kind::Float)
                return shortest_float(parse_number(literal.text));
            // a string is re-escaped, which keeps the C++ literal identical (§11.3)
            if (literal.kind == LiteralExpression::Kind::String)
                return emit_string_literal(literal.text);
            // a colour is emitted as its constructor, which scales the channels (§11.3)
            if (literal.kind == LiteralExpression::Kind::Color)
                return emit_color_literal(literal.text);
            // a length is emitted as its number, which drops the unit that the density owns (§11.3)
            if (literal.kind == LiteralExpression::Kind::Length) {
                // the trailing unit is removed, because only the number reaches the C++ (§11.3, AD-27)
                std::string_view text = literal.text;
                // the two logical unit spell the tail, which the number precedes (§11.3, AD-27)
                if (ends_with(text, "px"))
                    text.remove_suffix(2);
                return shortest_float(parse_number(text));
            }
            // a truth literal is emitted as its keyword, which is the only spelling (§11.3)
            return literal.text;
        }

        std::string ComponentEmitter::emit_array(const ArrayExpression& array) {
            // an empty list renders as braces, because the target property names the element type it needs (§8.1)
            if (array.elements.empty())
                return "{}";
            // the collection is named so the initialiser lands in exactly that type rather than a deduced one (§8.1)
            std::string out = cpp_type(type_of(array)) + "{";
            // each element appends itself in written order, which is the order the collection preserves (§8.1)
            for (std::size_t index = 0; index < array.elements.size(); index++) {
                // a comma joins the earlier elements, which is how an initialiser list reads (§8.1)
                if (index != 0)
                    out += ", ";
                out += emit_expression(*array.elements.at(index));
            }
            // the closing brace ends the initialiser (§8.1)
            return out + "}";
        }

        std::string ComponentEmitter::emit_call(const CallExpression& call) {
            // the colour renders first, because every operation is a method on it (§6.1)
            const std::string colour = emit_expression(*call.arguments.at(0));
            // the amount renders second, because it is the fraction or alpha the operation works by (§6.1)
            const std::string amount = emit_expression(*call.arguments.at(1));
            // the three operations are spelled as the methods core/paint.h declares (§6.1)
            if (call.function == "transparentize")
                return "(" + colour + ").transparentized(" + amount + ")";
            // an alpha modulation scales the alpha rather than replacing it (§6.1)
            if (call.function == "alpha")
                return "(" + colour + ").with_alpha(" + amount + ")";
            // an alpha replacement sets the alpha outright, which is what an explicit opacity asks for (§6.1)
            return "(" + colour + ").alpha_set(" + amount + ")";
        }

        std::string ComponentEmitter::emit_conversion(const ConversionExpression& conversion) {
            // the operand renders first, which every target wraps (§11.3)
            const std::string operand = emit_expression(*conversion.value);
            // the operand type decides the form, which mirrors the conversion rules (§11.3)
            const TypeKind from = type_of(*conversion.value).kind();
            // a string target renders the value as text, which each type spells differently (§11.3)
            if (conversion.target_type == "string") {
                // a colour has its own text spelling, which the helper below produces (§11.3)
                if (from == TypeKind::Color) {
                    // the helper is needed, which the source then includes and defines (§11.3)
                    m_uses_color_helper = true;
                    return m_stem + "_color_to_hex(" + operand + ")";
                }
                // a bool renders as the truth words, which the ternary spells (§11.3)
                if (from == TypeKind::Bool)
                    return "(" + operand + " ? std::string(\"true\") : std::string(\"false\"))";
                // a text renders as itself, which is the identity of the conversion (§11.3)
                if (from == TypeKind::String)
                    return "(" + operand + ")";
                // a whole number renders without a decimal, because a readout should not say 2.000000 (§11.3)
                if (from == TypeKind::Int)
                    return "std::to_string(" + operand + ")";
                // a float renders through the shortest round trip, which to_string lacks (§11.3)
                m_uses_float_helper = true;
                return m_stem + "_float_to_text(" + operand + ")";
            }
            // an integer target parses or narrows, which the operand type decides (§11.3)
            if (conversion.target_type == "int") {
                // a text parses, which reads the digits off the string (§11.3)
                if (from == TypeKind::String)
                    return "std::stoi(" + operand + ")";
                // a float narrows, which is the explicit cast of §11.1 (§11.3)
                if (from == TypeKind::Float)
                    return "static_cast<int>(" + operand + ")";
                // an integer renders as itself, which is the identity of the conversion (§11.3)
                return "(" + operand + ")";
            }
            // a float target parses or widens, which the operand type decides (§11.3)
            if (conversion.target_type == "float") {
                // a text parses, which reads the digits off the string (§11.3)
                if (from == TypeKind::String)
                    return "std::stof(" + operand + ")";
                // an integer widens, which is the explicit cast of §11.1 (§11.3)
                if (from == TypeKind::Int)
                    return "static_cast<float>(" + operand + ")";
                // a float renders as itself, which is the identity of the conversion (§11.3)
                return "(" + operand + ")";
            }
            // a bool target compares against empty, which every scalar interprets (§11.3)
            if (from == TypeKind::String)
                return "!" + operand + ".empty()";
            // a colour is present unless it is the default black, which is the comparison (§11.3)
            if (from == TypeKind::Color)
                return "(" + operand + " != Color{})";
            // a length or a float is present unless it is zero, which the comparison spells (§11.3)
            if (from == TypeKind::Length || from == TypeKind::Float)
                return "(" + operand + " != 0.0f)";
            // an integer is present unless it is zero, which the comparison spells (§11.3)
            if (from == TypeKind::Int)
                return "(" + operand + " != 0)";
            // a bool renders as itself, which is the identity of the conversion (§11.3)
            return "(" + operand + ")";
        }

        std::string ComponentEmitter::emit_binary(const BinaryExpression& binary) {
            // both operands render first, which the operator then joins (§11.3)
            const std::string left = emit_expression(*binary.left);
            // the right operand renders after the left, which is the written order (§11.3)
            const std::string right = emit_expression(*binary.right);
            // the operator spells as written, which every form keeps its own symbol (§11.3)
            switch (binary.op) {
            case BinaryOperator::Add:
                return "(" + left + " + " + right + ")";
            case BinaryOperator::Subtract:
                return "(" + left + " - " + right + ")";
            case BinaryOperator::Multiply:
                return "(" + left + " * " + right + ")";
            case BinaryOperator::Divide:
                return "(" + left + " / " + right + ")";
            case BinaryOperator::Modulo:
                return "(" + left + " % " + right + ")";
            case BinaryOperator::Equal:
                return "(" + left + " == " + right + ")";
            case BinaryOperator::NotEqual:
                return "(" + left + " != " + right + ")";
            case BinaryOperator::Less:
                return "(" + left + " < " + right + ")";
            case BinaryOperator::LessEqual:
                return "(" + left + " <= " + right + ")";
            case BinaryOperator::Greater:
                return "(" + left + " > " + right + ")";
            case BinaryOperator::GreaterEqual:
                return "(" + left + " >= " + right + ")";
            case BinaryOperator::And:
                return "(" + left + " && " + right + ")";
            case BinaryOperator::Or:
                return "(" + left + " || " + right + ")";
            }
            // an operator the checker rejected cannot render, which the empty literal stands in for (§11.3)
            return emit_string_literal({});
        }

        std::string ComponentEmitter::emit_unary(const UnaryExpression& unary) {
            // the operand renders first, which the operator then wraps (§11.3)
            const std::string operand = emit_expression(*unary.value);
            // a logical negation wraps the operand, which flips the truth (§11.3)
            if (unary.op == UnaryOperator::Not)
                return "(" + operand + " == false)";
            // a numeric negation wraps the operand, which flips the sign (§11.3)
            return "(-" + operand + ")";
        }

        std::string ComponentEmitter::dep_list(const std::vector<std::string>& dependencies) const {
            // the ids accumulate, which the array initialiser lists in order (§13.3)
            std::string out;
            // each dependency appends its member id, which is what the array holds (§13.3)
            for (const std::string& dependency : dependencies) {
                // a comma joins the earlier ids, which is how an array initialiser reads (§13.3)
                if (!out.empty())
                    out += ", ";
                // the dependency id is the property member id, which the engine keys on (§13.3)
                out += "m_" + sanitize(dependency) + ".id()";
            }
            return out;
        }

        bool ComponentEmitter::emit() {
            // the component is validated first, because a blocked one has nothing to generate (§11.3)
            validate();
            // a blocking condition empties the output, which the caller reports as empty (§11.3)
            if (m_blocked)
                return false;
            // the names are fixed up front, which every emitted line reads (§14.1)
            m_names.reserve(m_component.properties.size() + m_component.signals.size());
            // each property gets its C++ name, which the accessors and the members share (§14.1)
            for (const CheckedProperty& property : m_component.properties)
                m_names[property.name] = sanitize(property.name);
            // each signal gets its C++ name, which the slot and the handler share (§14.1)
            for (const CheckedSignal& signal : m_component.signals)
                m_names[signal.name] = sanitize(signal.name);
            // the signal flag drives the functional headers, which a signal component needs (§8.3)
            m_has_signals = !m_component.signals.empty();
            // the tree is walked into preorder, which is how the builder emits it (§8.1)
            m_nodes.clear();
            // the repeats are cleared with the tree, because both are rebuilt per component (§18.1, AD-13)
            m_repeats.clear();
            // the body counter restarts, because body names need only be unique within one component (§18.1, AD-13)
            m_body_count = 0;
            // the walk starts at the root, which has no parent (§8.1)
            if (m_component.root != nullptr)
                collect(*m_component.root, NO_PARENT);
            // the engine and the colour flags are gathered from the tree, which the headers need (§14.1)
            for (const NodeSlot& slot : m_nodes) {
                // the widget info decides when each engine is needed, which the builder then resolves (§14.1)
                const WidgetInfo& info = widget_info().at(std::string_view(slot.node->element_type));
                // the engine flag rises with each widget, because the builder resolves it once (§14.1)
                m_uses_engine = m_uses_engine || info.needs_engine;
                // the text engine flag rises with each label, because the body stores it (§14.1)
                m_uses_text_engine = m_uses_text_engine || info.needs_text_engine;
                // the canvas flag rises with each canvas, which adds the geometry header (§14.1)
                m_uses_canvas = m_uses_canvas || slot.node->element_type == "Canvas";
                // an alignment assignment reaches the generated unit, so the enumerator needs its header (§14.1, §8.2)
                for (const CheckedAssignment& assignment : slot.node->assignments) {
                    // only an alignment property raises the flag, because nothing else names an enumerator (§8.2)
                    if (assignment.type.is(TypeKind::Align))
                        m_uses_align = true;
                }
            }
            // a repeat body builds widgets inside the row functions, so its needs join the include set (§18.1, AD-13)
            for (const RepeatSlot& repeat : m_repeats) {
                // each body node is scanned alone, because the row functions are a separate path (§11.3)
                for (const NodeSlot& slot : repeat.body) {
                    // the widget info decides when each engine is needed, exactly as an ordinary node does (§11.3)
                    const WidgetInfo& info = widget_info().at(std::string_view(slot.node->element_type));
                    // the engine flag rises with each widget, because the builder resolves it once (§14.1)
                    m_uses_engine = m_uses_engine || info.needs_engine;
                    // the text engine flag rises with each label, because the row functions store it (§9.1)
                    m_uses_text_engine = m_uses_text_engine || info.needs_text_engine;
                    // the canvas flag rises with each canvas, which adds the geometry header (§14.1)
                    m_uses_canvas = m_uses_canvas || slot.node->element_type == "Canvas";
                    // an alignment assignment inside a row reaches the generated unit too (§8.2)
                    for (const CheckedAssignment& assignment : slot.node->assignments)
                        // only an alignment property raises the flag, because nothing else names an enumerator (§8.2)
                        if (assignment.type.is(TypeKind::Align))
                            m_uses_align = true;
                }
            }
            // the colour flag rises with any colour surface, which the paint header then joins (§14.1)
            for (const CheckedProperty& property : m_component.properties)
                m_uses_color = m_uses_color || property.type.is(TypeKind::Color);
            // the signal parameters carry the same need, which the facade signatures then spell (§14.1)
            for (const CheckedSignal& signal : m_component.signals) {
                // each parameter is checked, because any one of them forces the header (§14.1)
                for (const ValueType& parameter : signal.parameter_types)
                    m_uses_color = m_uses_color || parameter.is(TypeKind::Color);
            }
            // the defaults may hold colour text as well, which the include set must not miss (§11.3)
            for (const CheckedProperty& property : m_component.properties) {
                // a default is scanned only when one exists, which an empty default skips (§11.3)
                if (property.has_default)
                    scan_color(*property.default_value);
            }
            // the header is emitted first, which the implementation then includes (§14.1)
            emit_header();
            // the implementation follows, which defines everything the header declared (§14.1)
            emit_source();
            // the two files are ready, which the caller places under the output directory (§14.2)
            return true;
        }
    } // namespace

    CodeGenerator::CodeGenerator(DiagnosticList& diagnostics) :
        m_diagnostics(diagnostics) {}

    void CodeGenerator::set_catalog(const ElementCatalog* catalog) {
        // the catalog is stored as given, because a caller that registered none simply has no records (§11.2, AD-17)
        m_catalog = catalog;
    }

    void CodeGenerator::set_tu_chunk_size(std::size_t size) {
        // a size of zero would leave no components in a chunk, so it is clamped back to the default (AD-03b)
        m_tu_chunk_size = size == 0 ? 1 : size;
    }

    std::vector<GeneratedFile> CodeGenerator::generate(const CheckedFile& file) {
        // a file the checker blocked produces nothing, because the generator cannot trust it (§11.3)
        if (m_diagnostics.has_errors())
            return {};
        // the files accumulate in declaration order, which matches the source (§11.3)
        std::vector<GeneratedFile> files;
        // the implementation units accumulate when chunking, which the merge below groups (AD-03b)
        std::vector<std::string> chunk_contents;
        for (const CheckedComponent& component : file.components) {
            // the emitter is built per component, because the output files are per component (§14.1)
            ComponentEmitter emitter(component, m_diagnostics, m_catalog, file.imported_modules);
            // a component that was blocked empties the whole output, which the driver then treats as failed (§11.3)
            if (!emitter.emit())
                return {};
            // the header joins the files, which is what a translation unit includes (§14.2)
            files.push_back(emitter.take_header());
            // a single unit mode keeps one implementation per component, which is the default (AD-03b)
            if (m_tu_chunk_size <= 1)
                files.push_back(emitter.take_source());
            // a chunked mode collects the implementation, which the merge below groups (AD-03b)
            else
                chunk_contents.push_back(emitter.take_source().content);
        }
        if (m_tu_chunk_size > 1) {
            // the group counter names the chunk unit, which is what the build log shows (AD-03b)
            std::size_t chunk_index = 0;
            for (std::size_t begin = 0; begin < chunk_contents.size(); begin += m_tu_chunk_size) {
                // the chunk text opens with its provenance, which names the rule that produced it (AD-03b)
                std::string content = "// generated group of components, do not edit (§14.2, AD-03b)\n\n";
                // the group ends before the last whole chunk, which a remainder still forms its own unit (AD-03b)
                const std::size_t end = begin + m_tu_chunk_size < chunk_contents.size() ? begin + m_tu_chunk_size : chunk_contents.size();
                for (std::size_t index = begin; index < end; index++) {
                    // a blank separates the merged sources, which keeps each unit readable (AD-03b)
                    if (index != begin)
                        content += "\n";
                    content += chunk_contents[index];
                }
                // the chunk unit joins the files, which the driver writes under the output directory (AD-03b)
                files.push_back(GeneratedFile{.path = "facades_" + std::to_string(chunk_index) + ".cpp", .content = std::move(content)});
                chunk_index++;
            }
        }
        // the generated files are handed out, which the driver writes under its output directory (§14.2)
        return files;
    }

    bool write_if_changed(const std::filesystem::path& path, std::string_view content) {
        // the existing content is compared first, so an up to date file is left untouched (§14.2)
        std::ifstream existing(path, std::ios::binary);
        // a file that exists is read whole, which is what the comparison needs (§14.2)
        if (existing.is_open()) {
            // the previous bytes are read, which is the exact prior content (§14.2)
            const std::string previous((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
            // a file that already matches is not rewritten, which is what keeps the build quiet (§14.2)
            if (previous == content)
                return false;
        }
        // a changed file is truncated and rewritten, which makes the output canonical (§14.2)
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        // the content is written whole, which is what the next run compares against (§14.2)
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        // the stream is closed before the state is judged, because the flush is the write (§14.2)
        out.close();
        // the stream state decides the result, because a failed flush is a lost write (§14.2)
        return out.good();
    }
} // namespace albor::compiler
