#include "manifest/glue.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace albor::manifest
{
    namespace
    {
        // the comment that opens every generated unit, which names the manifest and the rule that produced it (§11.2, §14)
        std::string provenance(std::string_view module) { return "// generated from " + std::string(module) + ".ui-manifest, do not edit (§14, AD-17)"; }

        // render a component name in snake case, which is how its generated units are named (§11.3)
        std::string snake_case(std::string_view name) {
            // the output starts empty, which the words fill (§11.3)
            std::string out;
            // each letter is walked, because an upper case letter starts a word (§11.3)
            for (std::size_t i = 0; i < name.size(); ++i) {
                // an upper letter after any letter starts a word here, because a manifest name is plain words (§11.3)
                const bool boundary = i > 0 && name[i] >= 'A' && name[i] <= 'Z';
                // a boundary word is marked with the separator, which the file name needs (§11.3)
                if (boundary)
                    out.push_back('_');
                // the letter joins the name lowered, because the file base is lower case (§11.3)
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
            }
            // the assembled name is returned, which names the generated units (§11.3)
            return out;
        }

        // the c++ spelling of a manifest scalar type, which is absent when the glue cannot translate it (§11.2)
        std::optional<std::string> scalar_cpp(std::string_view type) {
            // the boolean maps onto the c++ boolean, which a binding stores directly (§11.2)
            if (type == "bool")
                return std::string("bool");
            // the integer maps onto the c++ integer, which a binding stores directly (§11.2)
            if (type == "int")
                return std::string("int");
            // the floating point maps onto the c++ float, which a binding stores directly (§11.2)
            if (type == "float")
                return std::string("float");
            // the text maps onto the c++ string, which a binding stores directly (§11.2)
            if (type == "string")
                return std::string("std::string");
            // the colour maps onto the framework colour, which carries no platform type with it (AD-05)
            if (type == "color")
                return std::string("Color");
            // a type outside the scalars carries no spelling, which blocks the output (§11.2)
            return std::nullopt;
        }

        // the c++ spelling of a Snapshot<T> type, which carries the element name as written (§11.2, §13.2)
        std::optional<std::string> snapshot_cpp(std::string_view type) {
            // the open bracket is the fixed lead, which the close bracket must match (§11.2)
            constexpr std::string_view prefix = "Snapshot<";
            // a spelling that lacks the brackets is not a snapshot, which the caller reports (§11.2)
            if (!type.starts_with(prefix) || !type.ends_with(">"))
                return std::nullopt;
            // the element name sits between the brackets, which is the type argument (§11.2)
            const std::string_view element = type.substr(prefix.size(), type.size() - prefix.size() - 1);
            // an empty argument cannot name a type, which the caller reports (§11.2)
            if (element.empty())
                return std::nullopt;
            // a nested bracket would need a template argument in the generated glue, which is out of scope (§11.2)
            if (element.find('<') != std::string_view::npos || element.find('>') != std::string_view::npos)
                return std::nullopt;
            // the shared pointer takes the element as const, which is what immutable snapshots need (§13.2)
            return "std::shared_ptr<const " + std::string(element) + ">";
        }

        // the element type of a Snapshot<T> property, which the glue forwards rather than includes (§11.2, AD-17)
        std::optional<std::string> snapshot_element(std::string_view type) {
            // only a snapshot names an application type, which is what the facade needs to declare ahead (§11.2, AD-17)
            if (const auto cpp = snapshot_cpp(type)) {
                // the open bracket is the fixed lead again, which sizes the element slice (§11.2)
                constexpr std::string_view prefix = "Snapshot<";
                // the element name sits between the brackets, which is what the forward declaration names (§11.2)
                const std::string_view element = type.substr(prefix.size(), type.size() - prefix.size() - 1);
                return std::string(element);
            }
            // a type that is not a snapshot carries no element, which only snapshots need (§11.2)
            return std::nullopt;
        }

        // the c++ spelling of a manifest type, which decides what the accessor signatures use (§11.2)
        std::optional<std::string> glue_cpp(std::string_view type) {
            // the scalar spellings take precedence, which cover the simple types (§11.2)
            if (const auto scalar = scalar_cpp(type))
                return scalar;
            // the snapshot spelling is the fallback, which covers the wrapped types (§11.2)
            return snapshot_cpp(type);
        }

        // one property ready to emit, which the generated accessors are built from (§11.2)
        struct GluedProperty
        {
            // the declared name, which the accessors are named after (§11.2)
            std::string name;

            // the c++ spelling of the type, which the accessor signatures use (§11.2)
            std::string cpp_type;
        };

        // one signal ready to emit, which the generated setter and raiser are built from (§11.2)
        struct GluedSignal
        {
            // the declared name, which the setter and the raiser share (§11.2)
            std::string name;

            // the parameter list in declaration order, which the signatures and the invocation both use (§11.2)
            std::string parameter_list;
            std::string argument_list;
        };

        // one component ready to emit, which is the generated class plus its include needs (§11.2)
        struct GluedComponent
        {
            // the declared name, which becomes the class name (§11.2)
            std::string name;

            // the snake case unit name, which the generated files are named after (§11.3)
            std::string stem;

            // the accessors and the handlers the declaration holds (§11.2)
            std::vector<GluedProperty> properties;
            std::vector<GluedSignal> signals;

            // the declared semantic role, which is absent when the manifest published none (§10.1, §11.2)
            std::optional<std::string> semantic_role;

            // the application data types, which this unit declares ahead rather than includes (§11.2, AD-17)
            std::vector<std::string> forward_types;

            // whether a signal needs the function include, which the slots carry (§11.2)
            bool needs_functional = false;

            // whether a snapshot needs the memory include, which the data type carries (§11.2)
            bool needs_memory = false;

            // whether a colour needs the paint include, which the framework type lives in (§11.2)
            bool needs_paint = false;
        };

        // append one line to the text being emitted, which pads by four spaces per level (§14.1)
        void emit_line(std::string& out, int depth, std::string_view text) {
            // an empty line carries no padding, because trailing spaces are not clang format clean (§14.1)
            if (text.empty()) {
                // the bare newline is all an empty line is, which marks the blank separator (§14.1)
                out.push_back('\n');
                return;
            }
            // the indent grows by four spaces per level, which is the tree convention (§14.1)
            out.append(static_cast<std::size_t>(depth) * 4, ' ');
            // the text is appended whole, because no generated line ever wraps (§14.1)
            out.append(text);
            // every generated line ends with a newline, which is what a source file expects (§14.1)
            out.push_back('\n');
        }

        // emit the facade header of one component, which the application subclasses (§11.2, AD-17)
        std::string emit_header(std::string_view module, const GluedComponent& component) {
            // the header output accumulates from empty, which the emitted lines fill (§14.1)
            std::string out;
            // the provenance opens the unit, which identifies the manifest that produced it (§14, AD-17)
            emit_line(out, 0, provenance(module));
            // the include guard is the pragma, which is the only guard the project uses (§14.1)
            emit_line(out, 0, "#pragma once");
            // the blank opens the include section, which separates the guard (§14.1)
            emit_line(out, 0, "");
            // the slot type needs the functional header, which a signal component reaches (§8.3)
            if (component.needs_functional)
                emit_line(out, 0, "#include <functional>");
            // the snapshot signature needs the memory header, which a data component reaches (§13.2)
            if (component.needs_memory)
                emit_line(out, 0, "#include <memory>");
            // the string signatures need the string header, which every facade uses (§14.1)
            emit_line(out, 0, "#include <string>");
            // the role read needs the string view header, which a role component reaches (§10.1)
            if (component.semantic_role)
                emit_line(out, 0, "#include <string_view>");
            // the standard section closes, which separates it from the project headers (§14.1)
            emit_line(out, 0, "");
            // a colour signature pulls the paint header, which only a colour component needs (§11.2)
            if (component.needs_paint)
                emit_line(out, 0, "#include \"core/paint.h\"");
            // the base class pulls the component header, which every facade inherits (§8.1)
            emit_line(out, 0, "#include \"ui/component.h\"");
            // the widget base pulls its header, which every facade inherits beside the component (§8.1)
            emit_line(out, 0, "#include \"widgets/widget.h\"");
            // the blank opens the namespace, which separates the includes (§14.1)
            emit_line(out, 0, "");
            // the facade lives in the framework namespace, which is what the build expects (§14.1)
            emit_line(out, 0, "namespace albor");
            emit_line(out, 0, "{");
            // the application data types are declared when the manifest names any, which keeps the signatures compilable (AD-17)
            if (!component.forward_types.empty()) {
                emit_line(out, 1, "// the application data types the manifest names, which this unit declares ahead rather than includes (§11.2, AD-17)");
                // each declared type is forwarded once, which is all the accessor signatures need (§11.2)
                for (const std::string& forward : component.forward_types)
                    emit_line(out, 1, "struct " + forward + ";");
                // the blank closes the forward section, which separates it from the class (§14.1)
                emit_line(out, 0, "");
            }
            emit_line(out, 1, "// the facade the " + std::string(module) + " manifest declares, which the application subclass implements (§11.2, AD-17)");
            emit_line(out, 1, "class " + component.name + " : public Widget");
            emit_line(out, 1, "{");
            emit_line(out, 1, "public:");
            emit_line(out, 2, "// build the view, which is a widget over the engine that settles its bindings (§11.2, §8.1)");
            emit_line(out, 2, "explicit " + component.name + "(BindingEngine* engine, std::string explicit_id = {});");
            emit_line(out, 2, "");
            emit_line(out, 2, "// destroy the view, which ends its lifetime in the tree (§11.2)");
            emit_line(out, 2, "~" + component.name + "() override;");
            emit_line(out, 2, "");
            // whether any member precedes, which decides the separating blanks (§11.2)
            bool first = true;
            // each property emits its accessor pair, which is the whole public surface (§11.2)
            for (const GluedProperty& property : component.properties) {
                // a further member takes the blank separator, which keeps the sections readable (§11.2)
                if (!first)
                    emit_line(out, 2, "");
                emit_line(out, 2, "// replace the " + property.name + " value, which a binding writes (§11.2)");
                emit_line(out, 2, "virtual void set_" + property.name + "(" + property.cpp_type + " value) = 0;");
                emit_line(out, 2, "");
                emit_line(out, 2, "// hand out the " + property.name + " value, which a binding reads (§11.2)");
                emit_line(out, 2, "[[nodiscard]] virtual " + property.cpp_type + " " + property.name + "() const = 0;");
                first = false;
            }
            // each signal emits its installed slot, which is how the application receives a report (§8.3)
            for (const GluedSignal& signal : component.signals) {
                // a further member takes the blank separator, which keeps the sections readable (§11.2)
                if (!first)
                    emit_line(out, 2, "");
                emit_line(out, 2, "// install the work a " + signal.name + " report runs, which the application wires (§11.2)");
                emit_line(out, 2, "void set_on_" + signal.name + "(std::function<void(" + signal.parameter_list + ")> handler);");
                first = false;
            }
            // the declared role emits its read, which names the widget to assistive technology (§10.1)
            if (component.semantic_role) {
                // a further member takes the blank separator, which keeps the sections readable (§11.2)
                if (!first)
                    emit_line(out, 2, "");
                emit_line(out, 2, "// hand out the role the manifest declared, which named the widget to assistive technology (§10.1, §11.2)");
                emit_line(out, 2, "[[nodiscard]] std::string_view declared_role() const;");
                first = false;
            }
            emit_line(out, 1, "");
            emit_line(out, 1, "protected:");
            // whether any emitter precedes, which opens the section with the first emitters (§11.2)
            bool first_protected = true;
            // each signal emits its raiser, which is what the subclass calls when the event lands (§8.3)
            for (const GluedSignal& signal : component.signals) {
                // a further emitter takes the blank separator, which keeps the sections readable (§11.2)
                if (!first_protected)
                    emit_line(out, 2, "");
                emit_line(out, 2, "// raise the " + signal.name + " signal, which the subclass calls when the event lands (§11.2)");
                emit_line(out, 2, "void emit_" + signal.name + "(" + signal.parameter_list + ");");
                first_protected = false;
            }
            emit_line(out, 1, "");
            emit_line(out, 1, "private:");
            // whether any slot precedes, which opens the section with the first slots (§11.2)
            bool first_private = true;
            // each signal emits its slot, which is where the raiser ends (§11.2)
            for (const GluedSignal& signal : component.signals) {
                // a further slot takes the blank separator, which keeps the sections readable (§11.2)
                if (!first_private)
                    emit_line(out, 2, "");
                emit_line(out, 2, "// the slot behind the " + signal.name + " signal, which is empty until installed (§11.2)");
                emit_line(out, 2, "std::function<void(" + signal.parameter_list + ")> m_on_" + signal.name + ";");
                first_private = false;
            }
            emit_line(out, 1, "};");
            emit_line(out, 0, "} // namespace albor");
            return out;
        }

        // emit the implementation unit of one component, which the application links against (§11.2, AD-17)
        std::string emit_source(std::string_view module, const GluedComponent& component) {
            // the implementation output accumulates from empty, which the emitted lines fill (§14.1)
            std::string out;
            // the provenance opens the unit, which identifies the manifest that produced it (§14, AD-17)
            emit_line(out, 0, provenance(module));
            emit_line(out, 0, "");
            // the facade header is included first, which is what this unit implements (§11.2)
            emit_line(out, 0, "#include \"" + std::string(module) + "/" + component.stem + ".h\"");
            emit_line(out, 0, "");
            // the setters need the functional header, which a signal component reaches (§8.3)
            if (component.needs_functional)
                emit_line(out, 0, "#include <functional>");
            // the role read needs the string view header, which a role component reaches (§10.1)
            if (component.semantic_role)
                emit_line(out, 0, "#include <string_view>");
            // the move calls need the utility header, which the setters use (§14.1)
            emit_line(out, 0, "#include <utility>");
            emit_line(out, 0, "");
            // the implementation lives in the framework namespace, which is what the build expects (§14.1)
            emit_line(out, 0, "namespace albor");
            emit_line(out, 0, "{");
            // the constructor forwards to the widget base, which settles the explicit id (§11.2, §8.1)
            emit_line(out, 1, component.name + "::" + component.name + "(BindingEngine* engine, std::string explicit_id) :");
            emit_line(out, 1, "    Widget(engine, std::move(explicit_id)) {}");
            // the destructor is defaulted, which lets the widget base tear the tree down (§11.2)
            emit_line(out, 1, component.name + "::~" + component.name + "() = default;");
            // each signal emits its slot setter, which stores the installed handler (§11.2)
            for (const GluedSignal& signal : component.signals) {
                emit_line(out, 1, "");
                emit_line(out, 1, "void " + component.name + "::set_on_" + signal.name + "(std::function<void(" + signal.parameter_list + ")> handler) {");
                emit_line(out, 2, "// the slot is stored, which the emit path runs when installed (§11.2)");
                emit_line(out, 2, "m_on_" + signal.name + " = std::move(handler);");
                emit_line(out, 1, "}");
            }
            // each signal emits its raiser, which runs the installed handler (§11.2)
            for (const GluedSignal& signal : component.signals) {
                emit_line(out, 1, "");
                emit_line(out, 1, "void " + component.name + "::emit_" + signal.name + "(" + signal.parameter_list + ") {");
                emit_line(out, 2, "// the slot runs only when installed, which is what a signal means (§11.2)");
                emit_line(out, 2, "if (m_on_" + signal.name + ")");
                emit_line(out, 3, "m_on_" + signal.name + "(" + signal.argument_list + ");");
                emit_line(out, 1, "}");
            }
            // the declared role emits its read, which returns the manifest spelling (§10.1)
            if (component.semantic_role) {
                emit_line(out, 1, "");
                emit_line(out, 1, "std::string_view " + component.name + "::declared_role() const {");
                emit_line(out, 2, "// the role is what assistive technology reads, which the runtime maps onto a known one (§10.1, §11.2)");
                emit_line(out, 2, "return \"" + *component.semantic_role + "\";");
                emit_line(out, 1, "}");
            }
            emit_line(out, 0, "} // namespace albor");
            return out;
        }
    } // namespace

    // build the generator over the shared problem list, which the diagnostics report into (§11.2)
    GlueGenerator::GlueGenerator(DiagnosticList& diagnostics) :
        m_diagnostics(diagnostics) {}

    // generate the glued units of one manifest, which is empty when a component blocked the output (§11.2)
    std::vector<GeneratedFile> GlueGenerator::generate(const ManifestFile& file) {
        // the units accumulate, which the caller writes out under the module directory (§14.2)
        std::vector<GeneratedFile> outputs;
        // whether any component could not translate, which voids the whole output (§11.2)
        bool blocked = false;
        // each component is processed on its own, because one problem must not hide the rest (§11.2)
        for (const NativeComponent& native : file.components) {
            // the component ready to emit is fresh, which the manifest fields fill (§11.2)
            GluedComponent component;
            // the class name is the declared name, which is what the manifest holds (§11.2)
            component.name = native.name;
            // the unit name is the lowered name, which is what the files are called (§11.3)
            component.stem = snake_case(native.name);
            // the declared role is carried when the manifest published one, which names the widget to assistive technology (§10.1)
            if (native.semantic)
                component.semantic_role = native.semantic->role;
            // each property is translated on its own, because one bad type must not hide the rest (§11.2)
            for (const NativeProperty& property : native.properties) {
                // the c++ spelling decides, which only the known types have (§11.2)
                const auto cpp = glue_cpp(property.type);
                // an untranslatable property is reported, which also voids the output (§11.2)
                if (!cpp) {
                    // the flag is set, which empties the generated output at the end (§11.2)
                    blocked = true;
                    // the problem names the type, because that is what the manifest wrote (§11.2)
                    m_diagnostics.add(Diagnostic::Severity::Error, property.span, "E0305", "the glue generator cannot translate the '" + property.type + "' property type");
                    // the remaining properties still translate, which reports every bad type (§11.2)
                    continue;
                }
                // the translated property joins the set, which the accessor pairs come from (§11.2)
                component.properties.push_back(GluedProperty{.name = property.name, .cpp_type = *cpp});
                // a colour property pulls the paint header, which the framework type lives in (AD-05)
                if (property.type == "color")
                    component.needs_paint = true;
                // a snapshot property pulls the memory header and a forward declaration, which the signature needs (§13.2, AD-17)
                if (property.type.starts_with("Snapshot<")) {
                    // the header is forced, which the shared pointer type requires (§13.2)
                    component.needs_memory = true;
                    // the element names the declared type, which the accessor signatures use (§11.2)
                    const auto forward = snapshot_element(property.type);
                    // the element is declared when it extracts, which is always for a snapshot (§11.2)
                    if (forward) {
                        // a data type is forwarded once, because the accessor signatures only need the name (§11.2, AD-17)
                        const bool known = std::find(component.forward_types.begin(), component.forward_types.end(), *forward) != component.forward_types.end();
                        // a type that is not yet known joins the list, which stays deduplicated (§11.2)
                        if (!known)
                            component.forward_types.push_back(*forward);
                    }
                }
            }
            // each signal is translated on its own, because one bad parameter must not hide the rest (§11.2)
            for (const NativeSignal& signal : native.signals) {
                // the signal ready to emit is fresh, which the parameters fill (§11.2)
                GluedSignal glued;
                // the declared name is the signal name, which the setter and the raiser share (§11.2)
                glued.name = signal.name;
                // whether every parameter translated, which admits the signal (§11.2)
                bool acceptable = true;
                // each parameter is translated in order, which is the declaration order (§11.2)
                for (const SignalParameter& parameter : signal.parameters) {
                    // the c++ spelling decides, which only the known types have (§11.2)
                    const auto cpp = glue_cpp(parameter.type);
                    // an untranslatable parameter is reported, which also voids the output (§11.2)
                    if (!cpp) {
                        // the flag is set, which empties the generated output at the end (§11.2)
                        blocked = true;
                        // the problem names the type, because that is what the manifest wrote (§11.2)
                        m_diagnostics.add(Diagnostic::Severity::Error, signal.span, "E0306", "the glue generator cannot translate the '" + parameter.type + "' signal parameter type");
                        // this signal is dropped, which keeps a partial signature out of the output (§11.2)
                        acceptable = false;
                        continue;
                    }
                    // a later parameter takes the separator, which is how a signature reads (§11.2)
                    if (!glued.parameter_list.empty()) {
                        // the parameter list grows the comma, which joins the declarations (§11.2)
                        glued.parameter_list += ", ";
                        // the argument list grows the comma, which joins the invocation (§11.2)
                        glued.argument_list += ", ";
                    }
                    // the parameter declares its type and its name, which is what a signature needs (§11.2)
                    glued.parameter_list += *cpp + " " + parameter.name;
                    // the argument is the name alone, which is what a raiser invocation needs (§11.2)
                    glued.argument_list += parameter.name;
                    // a colour parameter pulls the paint header, which the framework type lives in (AD-05)
                    if (parameter.type == "color")
                        component.needs_paint = true;
                }
                // a signal that translated wholly is admitted, which emits its setter and its raiser (§11.2)
                if (acceptable) {
                    // a signal slot pulls the functional header, which its setter carries (§11.2)
                    component.needs_functional = true;
                    // the glued signal joins the set, which the generated methods come from (§11.2)
                    component.signals.push_back(std::move(glued));
                }
            }
            // a component that blocked cannot emit, which leaves the output empty at the end (§11.2)
            if (blocked)
                continue;
            // the module path of the unit is the file module, which the caller places under (§14.2)
            const std::string module_name = std::string(file.module);
            // the facade header joins the files, which the application subclasses (§11.2, AD-17)
            outputs.push_back(GeneratedFile{.path = module_name + "/" + component.stem + ".h", .content = emit_header(file.module, component)});
            // the implementation unit joins the files, which the application links against (§11.2, AD-17)
            outputs.push_back(GeneratedFile{.path = module_name + "/" + component.stem + ".cpp", .content = emit_source(file.module, component)});
        }
        // a blocked manifest yields nothing, which the driver reports as failure (§11.2)
        if (blocked)
            return {};
        // the ready units are returned, which the caller writes out under the module directory (§14.2)
        return outputs;
    }
} // namespace albor::manifest
