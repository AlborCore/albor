#include "compiler/type_checker.h"

#include <algorithm>
#include <utility>

namespace albor::compiler
{
    std::optional<ValueType> TypeChecker::Scope::lookup(std::string_view name) const {
        // a draw block reads the canvas extent, which is the node it paints into (§6.4)
        if (in_draw) {
            // the arranged width is the only width a drawing can be correct against (§6.4)
            if (name == "canvas_width" || name == "canvas_height")
                return scalar(TypeKind::Float);
        }
        // the function parameters are checked first, because they are the nearest scope and shadow a property (§11.1)
        if (function_parameters != nullptr) {
            // each parameter is named by the declaration, so the body reads the argument by that name (§11.1)
            for (const std::pair<std::string, ValueType>& parameter : *function_parameters) {
                // the name decides, so a parameter resolves to exactly one type (§11.1)
                if (parameter.first == name)
                    return parameter.second;
            }
        }
        // the repeat bindings are checked first, because they are the nearest scope (§11.1)
        if (repeat != nullptr) {
            // the item binding carries the element type, which a vector source unwraps for its body (§11.1)
            if (name == repeat->repeat_item_name)
                return repeat->repeat_source_type.is(TypeKind::Vector) ? repeat->repeat_source_type.element() : repeat->repeat_source_type;
            // the index binding is always an integer, because it counts (§11.1)
            if (name == repeat->repeat_index_name)
                return scalar(TypeKind::Int);
        }
        // the signal parameters are read before the component, because a handler shadows a property (§11.1)
        if (handler_signal != nullptr) {
            // each parameter is named by the catalog, so the body writes what the widget carries (§11.1)
            for (std::size_t index = 0; index < handler_signal->parameter_names.size() && index < handler_signal->parameter_types.size(); index++) {
                // the name decides, so a parameter resolves to exactly one type (§11.1)
                if (handler_signal->parameter_names[index] == name)
                    return handler_signal->parameter_types[index];
            }
        }
        // the declarations are scanned, because every scope is built from a resolved component (§11.1)
        if (properties != nullptr)
            for (const CheckedProperty& property : *properties) {
                // the name decides, so a reference resolves to exactly one declaration (§11.1)
                if (property.name == name)
                    return property.type;
            }
        // the module-scoped values are consulted last, because a component's own declarations shadow them (§11.1)
        if (globals != nullptr)
            for (const CheckedGlobal& global : *globals) {
                // the name decides, so a reference resolves to exactly one declaration (§11.1)
                if (global.name == name)
                    return global.type;
            }
        // a name the scope does not hold was already reported, so nothing is returned (§11.1)
        return std::nullopt;
    }

    TypeChecker::TypeChecker(const ElementCatalog& catalog, DiagnosticList& diagnostics) :
        m_catalog(catalog), m_diagnostics(diagnostics) {}

    namespace
    {
        // copy a checked subtree, because a derived body must not disturb the base's own checked tree (§11.1)
        std::shared_ptr<CheckedNode> clone_node(const CheckedNode& source) {
            // the copy takes the shallow member set, because the expressions are shared rather than owned (§11.1)
            std::shared_ptr<CheckedNode> copy = std::make_shared<CheckedNode>(source);
            // the children are copied one by one, because each child is its own subtree (§11.1)
            copy->children.clear();
            for (const std::shared_ptr<CheckedNode>& child : source.children)
                copy->children.push_back(clone_node(*child));
            // a repeat body is its own subtree too, so it is copied the same way (§11.1)
            if (source.repeat_body != nullptr)
                copy->repeat_body = clone_node(*source.repeat_body);
            // a conditional body is copied for the same reason (§11.1)
            if (source.conditional_body != nullptr)
                copy->conditional_body = clone_node(*source.conditional_body);
            return copy;
        }

        // collect every children slot in a subtree, which is what a fill edits and a diagnostic points at (§11.1)
        void collect_slot_spans(const CheckedNode& node, std::vector<SourceSpan>& out) {
            // each child is scanned, because the marker sits among them or deeper (§11.1)
            for (const std::shared_ptr<CheckedNode>& child : node.children) {
                // a marker is recorded by its span, because a report points at it (§11.1)
                if (child->kind == TreeNodeKind::Slot)
                    out.push_back(child->span);
                else
                    collect_slot_spans(*child, out);
            }
            // a repeat body carries children of its own, because a marker may sit inside one (§11.1)
            if (node.repeat_body != nullptr)
                collect_slot_spans(*node.repeat_body, out);
            // a conditional body is the same, because its element is part of the tree (§11.1)
            if (node.conditional_body != nullptr)
                collect_slot_spans(*node.conditional_body, out);
        }

        // replace every children slot with the content a derived component wrote, reporting whether one was found (§11.1)
        bool fill_slots(CheckedNode& node, const std::vector<std::shared_ptr<CheckedNode>>& content) {
            bool filled = false;
            // the direct children are scanned first, because the marker sits among them (§11.1)
            for (std::size_t index = 0; index < node.children.size();) {
                // a marker is the content point, which the content replaces in place (§11.1)
                if (node.children[index]->kind == TreeNodeKind::Slot) {
                    // the marker is erased, because the content now stands where it was (§11.1)
                    node.children.erase(node.children.begin() + static_cast<std::ptrdiff_t>(index));
                    // the content is inserted at that position, because that is the order the base wrote (§11.1)
                    node.children.insert(node.children.begin() + static_cast<std::ptrdiff_t>(index), content.begin(), content.end());
                    filled = true;
                    // the scan continues after the inserted run, because nothing there is a marker (§11.1)
                    index += content.size();
                    continue;
                }
                // the next child is taken, because a marker is only where one is written (§11.1)
                index++;
            }
            // the children are walked as well, because a marker may sit deeper (§11.1)
            for (const std::shared_ptr<CheckedNode>& child : node.children)
                if (fill_slots(*child, content))
                    filled = true;
            // a repeat body is walked too, because its element may carry the marker (§11.1)
            if (node.repeat_body != nullptr && fill_slots(*node.repeat_body, content))
                filled = true;
            // a conditional body is walked for the same reason (§11.1)
            if (node.conditional_body != nullptr && fill_slots(*node.conditional_body, content))
                filled = true;
            return filled;
        }

        // drop every children marker, because a base used directly shows no content point (§11.1)
        void strip_slots(CheckedNode& node) {
            // the empty markers are erased, because nothing filled them (§11.1)
            std::erase_if(node.children, [](const std::shared_ptr<CheckedNode>& child) { return child->kind == TreeNodeKind::Slot; });
            // each remaining child is walked, because a marker may sit deeper (§11.1)
            for (const std::shared_ptr<CheckedNode>& child : node.children)
                strip_slots(*child);
            // a repeat body carries children of its own, so it is walked the same way (§11.1)
            if (node.repeat_body != nullptr)
                strip_slots(*node.repeat_body);
            // a conditional body is walked for the same reason (§11.1)
            if (node.conditional_body != nullptr)
                strip_slots(*node.conditional_body);
        }
    } // namespace

    CheckedFile TypeChecker::check(const SourceFile& file) {
        // the file is assembled as the components are checked, so one mistake costs one component (§11.1)
        CheckedFile checked;
        // the imports name the manifest modules, which is what a record header path is built from (§11.1, AD-17)
        for (const ImportDeclaration& imported : file.imports)
            checked.imported_modules.push_back(imported.module);
        // the function signatures are resolved first, so a call may appear before the declaration it names (§11.1)
        check_function_signatures(file, checked.functions);
        // the signatures are published, because a body resolves its calls against them (§11.1)
        m_functions = &checked.functions;
        // the function bodies are resolved next, because a body may call any signature the file declares (§11.1)
        for (std::size_t index = 0; index < file.functions.size() && index < checked.functions.size(); index++)
            check_function(file.functions[index], checked.functions[index]);
        // a function that calls itself through a cycle is rejected, because the C++ would recurse forever (§11.1)
        detect_function_cycles(checked.functions);
        // the module-scoped types resolve next, so an initializer may name any value the file declares (§11.1)
        check_global_signatures(file, checked.globals);
        // the initial values follow their signatures, because one may call a function the file declares (§11.1)
        for (std::size_t index = 0; index < file.globals.size() && index < checked.globals.size(); index++)
            check_global_value(file.globals[index], checked.globals[index]);
        // the module-scoped values are published, because a component expression resolves against them (§11.1)
        m_globals = &checked.globals;
        // every component name is indexed, because a duplicate reports once and a base resolves wherever written (§11.1)
        m_component_index.clear();
        for (std::size_t index = 0; index < file.components.size(); index++) {
            // the name decides the slot, because an element reference and a base both resolve by it (§11.1)
            const std::string& name = file.components[index].name;
            // a name the file declares twice is reported, because a reference to it would be ambiguous (§11.1)
            if (m_component_index.contains(name))
                m_diagnostics.add(Diagnostic::Severity::Error, file.components[index].span, "E0401", "'" + name + "' is declared more than once in this file");
            else
                m_component_index.emplace(name, index);
        }
        // the component declarations are resolved in declaration order, because the result preserves it (§11.1)
        for (const ComponentDeclaration& component : file.components) {
            // the component is assembled locally, because the pass state belongs to one declaration (§11.1)
            CheckedComponent out;
            // the name, the imports and the span come straight from the declaration (§11.1)
            out.name = component.name;
            out.imports = component.imports;
            out.span = component.span;
            // the declared names start empty, because they are collected as the declarations resolve (§11.1)
            m_declared.clear();
            // the first pass resolves the declarations, so a reference may appear before them (§11.1)
            resolve_declarations(component, out);
            // the component is kept, because the merge pass below runs over the whole file (§11.1)
            checked.components.push_back(std::move(out));
        }
        // a derived component gains its base's members, because the merged list is what its body resolves against (§11.1)
        std::vector<bool> inherited(file.components.size(), true);
        for (std::size_t index = 0; index < file.components.size(); index++)
            if (!file.components[index].base.empty())
                inherited[index] = merge_inherited(file, index, checked.components);
        // each component publishes its element type after the merge, so an instance sees the inherited members too (§11.1)
        publish_component_types(checked.components, inherited);
        // the trees are resolved bases first, because a derived body splices into the checked tree of its base (§11.1)
        resolve_component_trees(file, checked.components);
        // a graph of component elements that instantiates itself is rejected, because it would build forever (§11.1)
        detect_instantiation_cycles(checked.components);
        // an empty children slot is dropped from every tree, because a base used directly shows no content point (§11.1)
        for (CheckedComponent& component : checked.components)
            if (component.root != nullptr)
                strip_slots(*component.root);
        // the pass state is cleared, because the checker may be reused for another file (§11.1)
        m_properties = nullptr;
        // the signals are cleared with it, because they pointed into the component that just ended (§11.1)
        m_signals = nullptr;
        // the function signatures are cleared with it, because they pointed into the checked file (§11.1)
        m_functions = nullptr;
        // the module-scoped values are cleared with them, because they pointed into the checked file too (§11.1)
        m_globals = nullptr;
        // the component index is cleared with the rest, because it names positions in this file alone (§11.1)
        m_component_index.clear();
        // the published element types are cleared with it, because they were rebuilt for this file (§11.1)
        m_file_component_types.clear();
        return checked;
    }

    bool TypeChecker::merge_inherited(const SourceFile& file, std::size_t index, std::vector<CheckedComponent>& out) {
        // the declaration is named, because the merge rewrites the derived component against it (§11.1)
        const ComponentDeclaration& component = file.components[index];
        // the base must be a component this file declares, because only one carries members to inherit (§11.1)
        const auto found = m_component_index.find(component.base);
        if (found == m_component_index.end()) {
            // the problem points at the base name, because that is what the author wrote (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, component.base_span, "E0425", "'" + component.base + "' is not a component declared in this file");
            return false;
        }
        // the base must derive from none, because a derived fills its base's slot and declares none of its own (§11.1)
        if (!file.components[found->second].base.empty()) {
            // the problem points at the base name, because a one level deep chain is the rule (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, component.base_span, "E0426", "'" + component.base + "' is itself derived, so it declares no children slot of its own");
            return false;
        }
        // both sides are named, because the merged set replaces the derived component's own (§11.1)
        const CheckedComponent& base = out.at(found->second);
        CheckedComponent& derived = out.at(index);
        // the inherited members come first, because a redeclaration overrides one but never reorders the set (§11.1)
        std::vector<CheckedProperty> properties = base.properties;
        std::vector<CheckedSignal> signals = base.signals;
        // each of the derived component's own properties is laid over the inherited set (§11.1)
        for (const CheckedProperty& own : derived.properties) {
            // the search finds an inherited property of the same name, which a redeclaration overrides (§11.1)
            CheckedProperty* inherited_property = nullptr;
            for (CheckedProperty& candidate : properties)
                if (candidate.name == own.name) {
                    inherited_property = &candidate;
                    break;
                }
            // a property the base does not declare is simply added, because nothing precedes it (§11.1)
            if (inherited_property == nullptr) {
                properties.push_back(own);
                continue;
            }
            // a redeclaration must agree on type and direction, because the inherited bindings read the same slot (§11.1)
            if (inherited_property->type.to_string() != own.type.to_string() || inherited_property->direction != own.direction)
                m_diagnostics.add(Diagnostic::Severity::Error, own.span, "E0424", "'" + own.name + "' redeclares an inherited property with a different type or direction");
            // the redeclaration takes the slot, because the derived default is what the facade initialises with (§11.1)
            *inherited_property = own;
        }
        // each of the derived component's own signals joins the inherited set, because a duplicate would split it (§11.1)
        for (const CheckedSignal& own : derived.signals) {
            // an inherited signal of the same name is a duplicate, because only one callback setter may exist (§11.1)
            const bool duplicate = std::any_of(signals.begin(), signals.end(), [&own](const CheckedSignal& signal) { return signal.name == own.name; });
            if (duplicate) {
                // the collision is reported against the later one, because the base one is what the author meant (§11.1)
                m_diagnostics.add(Diagnostic::Severity::Error, own.span, "E0401", "'" + own.name + "' is declared more than once in this component");
                continue;
            }
            // the signal is kept, because it is part of what the merged body raises (§11.1)
            signals.push_back(own);
        }
        // a derived property colliding with an inherited signal duplicates, because a name means one thing (§11.1)
        for (const CheckedProperty& own : derived.properties)
            for (const CheckedSignal& signal : base.signals)
                if (own.name == signal.name)
                    m_diagnostics.add(Diagnostic::Severity::Error, own.span, "E0401", "'" + own.name + "' is declared more than once in this component");
        // a derived signal that collides with an inherited property is the other half of the same rule (§11.1)
        for (const CheckedSignal& own : derived.signals)
            for (const CheckedProperty& property : base.properties)
                if (own.name == property.name)
                    m_diagnostics.add(Diagnostic::Severity::Error, own.span, "E0401", "'" + own.name + "' is declared more than once in this component");
        // the merged sets replace the derived component's own, because that is what its body resolves against (§11.1)
        derived.properties = std::move(properties);
        derived.signals = std::move(signals);
        // the derived defaults are typed against the merged list, because an inherited name may appear in one (§11.1)
        type_own_defaults(component, derived);
        return true;
    }

    void TypeChecker::type_own_defaults(const ComponentDeclaration& component, CheckedComponent& out) {
        // the scope is the merged list, because an inherited name may appear in a default (§11.1)
        const Scope scope{.properties = &out.properties, .repeat = nullptr, .globals = m_globals};
        for (const PropertyDeclaration& declaration : component.properties) {
            // the checked property is found by name, because the merge moved the list out of written order (§11.1)
            CheckedProperty* property = nullptr;
            for (CheckedProperty& candidate : out.properties)
                if (candidate.name == declaration.name) {
                    property = &candidate;
                    break;
                }
            // a property with no default has nothing to type, which the declaration already said (§11.1)
            if (property == nullptr || !property->has_default)
                continue;
            // the dependencies come from the same walk, because a default is a binding (§7.3, AD-04a)
            const ValueType value_type = type_expression(*property->default_value, scope, property->default_dependencies);
            // the value must match the property, because nothing widens implicitly (§11.1)
            if (!assignable(value_type, property->type))
                report_default_mismatch(declaration, value_type, property->type);
        }
    }

    void TypeChecker::publish_component_types(const std::vector<CheckedComponent>& checked, const std::vector<bool>& inherited) {
        // the set starts empty, because it is rebuilt for every file the checker sees (§11.1)
        m_file_component_types.clear();
        for (std::size_t index = 0; index < checked.size(); index++) {
            // a component whose merge failed publishes nothing, because its member set is incomplete (§11.1)
            if (!inherited.at(index))
                continue;
            // the name decides the slot, because an instance writes the element name (§11.1)
            const CheckedComponent& component = checked[index];
            // a duplicate name publishes once, because the index pass already reported the second (§11.1)
            if (m_file_component_types.contains(component.name))
                continue;
            // the type is assembled locally, because each component contributes its own members (§11.1)
            ElementType type;
            // the name is what an element writes, so an instance resolves against it (§11.1)
            type.name = component.name;
            // the declared properties come first, because one shadows a common property of the same name (§11.1)
            for (const CheckedProperty& property : component.properties)
                type.properties.push_back(ElementProperty{.name = property.name, .type = property.type, .assignable = true});
            // the common properties join, because a component arranges like any other node does (§8.2)
            for (const ElementProperty& common : common_element_properties())
                type.properties.push_back(common);
            // the signals carry their parameter names, because a handler body reads the raised value by them (§11.1)
            for (const CheckedSignal& signal : component.signals)
                type.signals.push_back(ElementSignal{.name = signal.name, .parameter_types = signal.parameter_types, .parameter_names = signal.parameter_names});
            // content reaches a base through a derived component, never through an instance (§11.1)
            type.accepts_children = false;
            // the type is published, because an element reference resolves before the catalog (§11.1)
            m_file_component_types.emplace(component.name, std::move(type));
        }
    }

    void TypeChecker::resolve_component_trees(const SourceFile& file, std::vector<CheckedComponent>& out) {
        // two rounds run, bases first, because a derived body reads the checked tree its base produced (§11.1)
        for (std::size_t round = 0; round < 2; round++) {
            for (std::size_t index = 0; index < file.components.size(); index++) {
                // the round skips by derivation, because a base is checked before any component that fills it (§11.1)
                const bool derived = !file.components[index].base.empty();
                if (derived != (round == 1))
                    continue;
                // the base is null unless it resolved non derived, because the merge already reported the rest (§11.1)
                const CheckedComponent* base = nullptr;
                if (derived) {
                    const auto found = m_component_index.find(file.components[index].base);
                    if (found != m_component_index.end() && file.components[found->second].base.empty())
                        base = &out.at(found->second);
                }
                // the declared names are rebuilt, because a cycle report points at a declaration (§7.3, AD-04a)
                m_declared.clear();
                for (const CheckedProperty& property : out[index].properties)
                    m_declared.emplace_back(property.name, property.span);
                for (const CheckedSignal& signal : out[index].signals)
                    m_declared.emplace_back(signal.name, signal.span);
                // the declarations are published, because the second pass resolves handlers against them (§11.1)
                m_properties = &out[index].properties;
                // the signals are published, because the second pass resolves invocations against them (§11.1)
                m_signals = &out[index].signals;
                // the second pass resolves the tree against what the first pass found (§11.1)
                resolve_tree(file.components[index], out[index], base);
                // the static graph is walked once the tree is typed, because it is built from the bindings (§7.3, AD-04a)
                detect_cycles(out[index]);
            }
        }
        // the pass state is cleared, because the checker may be reused for another file (§11.1)
        m_properties = nullptr;
        // the signals are cleared with it, because they pointed into the checked file (§11.1)
        m_signals = nullptr;
    }

    void TypeChecker::detect_instantiation_cycles(const std::vector<CheckedComponent>& checked) {
        // the adjacency list maps one component to the component elements its tree instantiates (§11.1)
        std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>> edges;
        for (const CheckedComponent& component : checked) {
            // every component gets an entry, because the walk reaches each of them (§11.1)
            std::vector<std::pair<std::string, SourceSpan>>& targets = edges[component.name];
            // the tree is walked only when it exists, because a broken declaration built none (§11.1)
            if (component.root != nullptr)
                collect_instantiation_edges(*component.root, targets);
        }
        // the colour map orders the walk, because a gray edge back to the open path is the cycle (§11.1)
        std::map<std::string, int> colour;
        for (const CheckedComponent& component : checked) {
            // a finished component is skipped, because every path from it was already walked (§11.1)
            if (colour[component.name] == 2)
                continue;
            visit_instantiation(component.name, edges, colour);
        }
    }

    void TypeChecker::visit_instantiation(const std::string& name, const std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>>& edges, std::map<std::string, int>& colour) {
        // the component opens, because a path from it is now being walked (§11.1)
        colour[name] = 1;
        // each edge decides, because a cycle is reachable from any member of it (§11.1)
        const auto found = edges.find(name);
        if (found != edges.end()) {
            for (const std::pair<std::string, SourceSpan>& target : found->second) {
                // a gray target closes the cycle, because the path already opened it (§11.1)
                if (colour[target.first] == 1) {
                    // the problem points at the element, because that is where the instantiation was written (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, target.second, "E0429", "'" + name + "' instantiates itself through a cycle of component elements");
                    continue;
                }
                // an unvisited target is walked, because its own edges may return (§11.1)
                if (colour[target.first] == 0)
                    visit_instantiation(target.first, edges, colour);
            }
        }
        // the component closes, because every path from it has been walked (§11.1)
        colour[name] = 2;
    }

    void TypeChecker::collect_instantiation_edges(const CheckedNode& node, std::vector<std::pair<std::string, SourceSpan>>& targets) const {
        // an element that names a file component is an edge, because the instance builds that component (§11.1)
        if (node.kind == TreeNodeKind::Element && m_component_index.contains(node.element_type))
            targets.emplace_back(node.element_type, node.span);
        // each child is walked, because the edges sit at any depth (§11.1)
        for (const std::shared_ptr<CheckedNode>& child : node.children)
            collect_instantiation_edges(*child, targets);
        // a repeat body instantiates its element too, so it carries the same edges (§11.1)
        if (node.repeat_body != nullptr)
            collect_instantiation_edges(*node.repeat_body, targets);
        // a conditional body is the same, because it exists at build time (§11.1)
        if (node.conditional_body != nullptr)
            collect_instantiation_edges(*node.conditional_body, targets);
    }

    void TypeChecker::check_function_signatures(const SourceFile& file, std::vector<CheckedFunction>& out) {
        // every signature is resolved before any body, so a call may appear before the declaration it names (§11.1)
        for (const FunctionDeclaration& declaration : file.functions) {
            // the declaration is resolved locally, because one mistake must not hide the rest (§11.1)
            CheckedFunction function;
            // the name and the span come straight from the declaration (§11.1)
            function.name = declaration.name;
            function.span = declaration.span;
            // the parameters are resolved in written order, because the arguments must match them (§11.1)
            for (const FunctionParameter& parameter : declaration.parameters) {
                // the written type resolves against the catalog, which is where a native type comes from (§11.2, AD-17)
                const ValueType resolved = resolve_type_name(parameter.type, m_catalog);
                // a type that did not resolve is reported here, because nothing downstream can (§11.1)
                if (resolved.is_error())
                    report_unknown_type(parameter.type, parameter.span);
                // the parameter is kept, because a body reads the argument by that name (§11.1)
                function.parameters.emplace_back(parameter.name, resolved);
            }
            // the return type resolves against the catalog, which is what a call carries as its own (§11.2, AD-17)
            const ValueType return_type = resolve_type_name(declaration.return_type, m_catalog);
            // a return type that did not resolve is reported here, because nothing downstream can (§11.1)
            if (return_type.is_error())
                report_unknown_type(declaration.return_type, declaration.span);
            // the return type is stored, because a call takes it as the call's type (§11.1)
            function.return_type = return_type;
            // a name the file declares twice is reported, because a call would be ambiguous (§11.1)
            for (const CheckedFunction& prior : out)
                if (prior.name == function.name)
                    m_diagnostics.add(Diagnostic::Severity::Error, function.span, "E0401", "'" + function.name + "' is declared more than once in this file");
            // the signature is kept, because a body resolves its calls against it (§11.1)
            out.push_back(std::move(function));
        }
    }

    void TypeChecker::check_function(const FunctionDeclaration& declaration, CheckedFunction& out) {
        // the parameters are the whole of the scope, because a function reads only its own arguments (§11.1)
        const Scope scope{.properties = nullptr, .repeat = nullptr, .handler_signal = nullptr, .function_parameters = &out.parameters, .globals = nullptr, .in_draw = false};
        // a body with no return carries no expression, so there is nothing to type (§11.1)
        if (declaration.return_value == nullptr)
            return;
        // the dependencies are dropped, because a function reads only its parameters and so has none (§11.1)
        std::vector<std::string> dependencies;
        // the returned expression is typed against the parameters, which is the whole of the check (§11.1)
        const ValueType value_type = type_expression(*declaration.return_value, scope, dependencies);
        // the value must match the declared return type, because nothing widens implicitly (§11.1)
        if (!assignable(value_type, out.return_type))
            report_type_mismatch(*declaration.return_value, value_type, out.return_type);
        // the returned expression is shared rather than re-owned, because the tree owns the expression (§11.1)
        out.return_value = std::shared_ptr<const Expression>(declaration.return_value.get(), [](const Expression*) {});
    }

    void TypeChecker::collect_function_calls(const Expression& expression, std::vector<std::string>& calls) const {
        // a call names a function, which is an edge whatever it resolves to (§11.1)
        if (const auto* call = dynamic_cast<const CallExpression*>(&expression)) {
            // the name is recorded, because a declared function makes it an edge (§11.1)
            calls.push_back(call->function);
            // every argument is walked, because a call may nest another (§11.1)
            for (const ExpressionPtr& argument : call->arguments)
                collect_function_calls(*argument, calls);
        }
        // a conversion wraps a value, which is walked behind it (§11.1)
        if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression))
            collect_function_calls(*conversion->value, calls);
        // a binary application has two operands, which are both walked (§11.1)
        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
            collect_function_calls(*binary->left, calls);
            collect_function_calls(*binary->right, calls);
        }
        // a unary application has one operand, which is walked behind it (§11.1)
        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
            collect_function_calls(*unary->value, calls);
        // a grouping has an inner expression, which is walked behind it (§11.1)
        if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
            collect_function_calls(*grouping->inner, calls);
        // a bracketed list has elements, each of which is walked in order (§11.1)
        if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression))
            for (const ExpressionPtr& element : array->elements)
                collect_function_calls(*element, calls);
        // a construction has field values, each of which is walked in order (§11.1)
        if (const auto* construction = dynamic_cast<const RecordConstructionExpression*>(&expression))
            for (const ConstructionField& field : construction->fields)
                collect_function_calls(*field.value, calls);
        // a field read has an object, which is walked behind it (§11.1)
        if (const auto* access = dynamic_cast<const FieldAccessExpression*>(&expression))
            collect_function_calls(*access->object, calls);
    }

    void TypeChecker::detect_function_cycles(const std::vector<CheckedFunction>& functions) {
        // the declared names decide which a call resolves to, because a builtin is never a node (§11.1)
        std::map<std::string, SourceSpan> declared;
        for (const CheckedFunction& function : functions)
            declared.emplace(function.name, function.span);
        // the adjacency list maps one function to the functions its body calls (§11.1)
        std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>> edges;
        for (const CheckedFunction& function : functions) {
            // every function gets an entry, because the walk reaches each of them (§11.1)
            std::vector<std::pair<std::string, SourceSpan>>& targets = edges[function.name];
            // a body with no return calls nothing, because there is no expression to walk (§11.1)
            if (function.return_value == nullptr)
                continue;
            // the called names are collected in written order, which the walk then follows (§11.1)
            std::vector<std::string> calls;
            collect_function_calls(*function.return_value, calls);
            // each call that names a declared function becomes an edge (§11.1)
            for (const std::string& called : calls) {
                // the declaration decides, because a builtin name is not a node (§11.1)
                const auto found = declared.find(called);
                if (found != declared.end())
                    targets.emplace_back(called, found->second);
            }
        }
        // the colour map orders the walk, because a gray edge back to the open path is the cycle (§11.1)
        std::map<std::string, int> colour;
        for (const CheckedFunction& function : functions) {
            // a finished function is skipped, because every path from it was already walked (§11.1)
            if (colour[function.name] == 2)
                continue;
            visit_function(function.name, edges, colour);
        }
    }

    void TypeChecker::visit_function(const std::string& name, const std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>>& edges, std::map<std::string, int>& colour) {
        // the function opens, because a path from it is now being walked (§11.1)
        colour[name] = 1;
        // each edge decides, because a cycle is reachable from any member of it (§11.1)
        const auto found = edges.find(name);
        if (found != edges.end()) {
            for (const std::pair<std::string, SourceSpan>& target : found->second) {
                // a gray target closes the cycle, because the path already opened it (§11.1)
                if (colour[target.first] == 1) {
                    // the problem points at the call, because that is where the recursion was written (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, target.second, "E0431", "'" + name + "' calls itself through a cycle of functions");
                    continue;
                }
                // an unvisited target is walked, because its own edges may return (§11.1)
                if (colour[target.first] == 0)
                    visit_function(target.first, edges, colour);
            }
        }
        // the function closes, because every path from it has been walked (§11.1)
        colour[name] = 2;
    }

    void TypeChecker::check_global_signatures(const SourceFile& file, std::vector<CheckedGlobal>& out) {
        // every type is resolved before any value, so one initializer may name another file-level name (§11.1)
        for (const GlobalDeclaration& declaration : file.globals) {
            // the declaration is resolved locally, because one mistake must not hide the rest (§11.1)
            CheckedGlobal global;
            // the name and the span come straight from the declaration (§11.1)
            global.name = declaration.name;
            global.span = declaration.span;
            // the declared type resolves against the catalog, which is where a native type comes from (§11.2, AD-17)
            const ValueType resolved = resolve_type_name(declaration.type, m_catalog);
            // a type that did not resolve is reported here, because nothing downstream can (§11.1)
            if (resolved.is_error())
                report_unknown_type(declaration.type, declaration.span);
            // the resolved type is stored, because every read carries it as its own (§11.1)
            global.type = resolved;
            // a name the file declares twice is reported, because a reference would be ambiguous (§11.1)
            for (const CheckedGlobal& prior : out)
                if (prior.name == global.name)
                    m_diagnostics.add(Diagnostic::Severity::Error, global.span, "E0401", "'" + global.name + "' is declared more than once in this file");
            // the signature is kept, because the value pass resolves its initializer against it (§11.1)
            out.push_back(std::move(global));
        }
    }

    void TypeChecker::check_global_value(const GlobalDeclaration& declaration, CheckedGlobal& out) {
        // the value with no initializer has nothing to type, because the parser already reported the gap (§11.1)
        if (declaration.value == nullptr)
            return;
        // the scope is empty on purpose, because a value reads neither a property nor another global (§11.1)
        const Scope scope{.properties = nullptr, .repeat = nullptr};
        // the dependencies are dropped, because a global never changes and so is never one (§7.3, AD-04a)
        std::vector<std::string> dependencies;
        // the initializer is typed against the empty scope, which is the whole of the check (§11.1)
        const ValueType value_type = type_expression(*declaration.value, scope, dependencies);
        // the value must match the declared type, because nothing widens implicitly (§11.1)
        if (!value_type.is_error() && !assignable(value_type, out.type))
            report_type_mismatch(*declaration.value, value_type, out.type);
        // the initializer is shared rather than re-owned, because the tree owns the expression (§11.1)
        out.value = std::shared_ptr<const Expression>(declaration.value.get(), [](const Expression*) {});
    }

    bool TypeChecker::is_global(std::string_view name) const {
        // the file may declare none, which keeps the scan a null check away (§11.1)
        if (m_globals == nullptr)
            return false;
        // the name decides, so exactly one declaration answers for it (§11.1)
        for (const CheckedGlobal& global : *m_globals)
            if (global.name == name)
                return true;
        return false;
    }

    void TypeChecker::report_read_only(std::string_view name, SourceSpan span) {
        // the problem points at the write, because that is what the author must change (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, span, "E0422", "'" + std::string(name) + "' is a module-scoped value, which is read-only");
    }

    void TypeChecker::resolve_declarations(const ComponentDeclaration& component, CheckedComponent& out) {
        // the properties are resolved first, because a default may reference any of them (§11.1)
        for (const PropertyDeclaration& declaration : component.properties) {
            // the declaration is resolved locally, because one mistake must not hide the rest (§11.1)
            CheckedProperty property;
            // the name, the direction and the span come straight from the declaration (§11.1)
            property.name = declaration.name;
            property.direction = declaration.direction;
            property.span = declaration.span;
            // the written type resolves against the catalog, which is where a native type comes from (§11.2, AD-17)
            property.type = resolve_type_name(declaration.type, m_catalog);
            // a type that did not resolve is reported here, because nothing downstream can (§11.1)
            if (property.type.is_error())
                report_unknown_type(declaration.type, declaration.span);
            // the default is shared rather than re-owned, because the tree owns the expression (§11.1)
            if (declaration.default_value != nullptr) {
                // the default exists, so the facade will initialise from it (§11.1)
                property.has_default = true;
                // the parsed expression is aliased, because the checked tree outlives nothing but the parse (§11.1)
                property.default_value = std::shared_ptr<const Expression>(declaration.default_value.get(), [](const Expression*) {});
            }
            // the name and the span are recorded, because a cycle report points at a declaration (§7.3, AD-04a)
            m_declared.emplace_back(property.name, property.span);
            // the property is kept, because a binding resolves against it (§11.1)
            out.properties.push_back(std::move(property));
        }
        // defaults type second since one may read a later property, and a derived body waits for inherited names (§11.1)
        if (component.base.empty()) {
            for (std::size_t index = 0; index < component.properties.size(); index++) {
                // a property with no default has nothing to type (§11.1)
                if (!out.properties.at(index).has_default)
                    continue;
                // the scope is the whole declaration list, because the default may read any of it (§11.1)
                const Scope scope{.properties = &out.properties, .repeat = nullptr, .globals = m_globals};
                // the dependencies come from the same walk, because a default is a binding (§7.3, AD-04a)
                const ValueType value_type = type_expression(*out.properties.at(index).default_value, scope, out.properties.at(index).default_dependencies);
                // the value must match the property, because nothing widens implicitly (§11.1)
                if (!assignable(value_type, out.properties.at(index).type)) {
                    // the mismatch points at the declaration, because that is where both types are written (§11.1)
                    report_default_mismatch(component.properties.at(index), value_type, out.properties.at(index).type);
                }
            }
        }
        // the signals are resolved, because a handler resolves against them (§11.1)
        for (const SignalDeclaration& declaration : component.signals) {
            // the declaration is resolved locally, because one mistake must not hide the rest (§11.1)
            CheckedSignal signal;
            // the name and the span come straight from the declaration (§11.1)
            signal.name = declaration.name;
            signal.span = declaration.span;
            // each parameter type resolves the same way a property type does (§11.1)
            for (std::size_t index = 0; index < declaration.parameter_types.size(); index++) {
                // the type resolves against the catalog, which is where a native type comes from (§11.1, §11.2, AD-17)
                const ValueType resolved = resolve_type_name(declaration.parameter_types[index], m_catalog);
                // a type that did not resolve is reported here, because nothing downstream can (§11.1)
                if (resolved.is_error())
                    report_unknown_type(declaration.parameter_types[index], declaration.span);
                // the parameter is kept in declaration order, because the facade signature follows it (§11.1)
                signal.parameter_types.push_back(resolved);
                // the name is kept beside the type, because a handler body reads the raised value by it (§11.1)
                signal.parameter_names.push_back(index < declaration.parameter_names.size() ? declaration.parameter_names[index] : std::string{});
            }
            // a signal is kept, because a handler resolves against it (§11.1)
            out.signals.push_back(std::move(signal));
        }
        // the duplicate check runs over everything the component declares, because a name means one thing (§11.1)
        for (std::size_t index = 0; index < out.properties.size(); index++) {
            // the earlier declarations are scanned, because only they can collide (§11.1)
            for (std::size_t other = 0; other < index; other++) {
                // a repeated name is reported once, against the later declaration (§11.1)
                if (out.properties.at(other).name == out.properties.at(index).name)
                    report_duplicate(out.properties.at(index).name, out.properties.at(index).span);
            }
        }
        // every signal is recorded with its span, because a cycle report points at a declaration (§7.3, AD-04a)
        for (const CheckedSignal& signal : out.signals) {
            // the name and the span are recorded, so a report can point at the declaration (§7.3, AD-04a)
            m_declared.emplace_back(signal.name, signal.span);
        }
        // a signal collides with a property of the same name, because a handler could not tell them apart (§11.1)
        for (const CheckedSignal& signal : out.signals) {
            // the properties are scanned, because a collision is between the two sets (§11.1)
            for (const CheckedProperty& property : out.properties) {
                // a collision is an error, because a name means one thing in a component (§11.1)
                if (property.name == signal.name)
                    report_duplicate(signal.name, signal.span);
            }
        }
        // a second signal with the same name collides for the same reason (§11.1)
        for (std::size_t index = 0; index < out.signals.size(); index++) {
            // the earlier signals are scanned, because only they can collide (§11.1)
            for (std::size_t other = 0; other < index; other++) {
                // a repeated name is reported once, against the later declaration (§11.1)
                if (out.signals.at(other).name == out.signals.at(index).name) {
                    // the collision is an error, because the facade could raise only one of them (§11.1)
                    report_duplicate(out.signals.at(index).name, out.signals.at(index).span);
                    break;
                }
            }
        }
    }

    void TypeChecker::report_duplicate(std::string_view name, SourceSpan span) {
        // the problem points at the later declaration, because the first one is what the author meant (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, span, "E0401", "'" + std::string(name) + "' is declared more than once in this component");
        // the name is recorded, so a third declaration reports against the same set (§11.1)
        m_declared.emplace_back(std::string(name), span);
    }

    void TypeChecker::report_unknown_type(std::string_view written, SourceSpan span) {
        // the problem names what was written, because that is what the author must change (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, span, "E0400", "'" + std::string(written) + "' is not a type this framework knows");
    }

    void TypeChecker::report_default_mismatch(const PropertyDeclaration& declaration, const ValueType& from, const ValueType& to) {
        // the problem points at the declaration, because that is where both types are written (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, declaration.span, "E0410", "the default of '" + declaration.name + "' is a " + from.to_string() + " but the property is a " + to.to_string());
    }

    void TypeChecker::resolve_tree(const ComponentDeclaration& component, CheckedComponent& out, const CheckedComponent* base) {
        // the scope a reference resolves in is the component's own declarations (§11.1)
        const Scope scope{.properties = &out.properties, .repeat = nullptr, .globals = m_globals};
        // a derived component supplies content instead of a root, which fills the base's children slot (§11.1)
        if (!component.base.empty()) {
            // a base that failed its merge has no tree to fill, because the merge pass already reported it (§11.1)
            if (base == nullptr || base->root == nullptr)
                return;
            // the content elements are checked one by one, because several may fill the one slot (§11.1)
            std::vector<std::shared_ptr<CheckedNode>> content;
            for (const std::unique_ptr<TreeNode>& element : component.content)
                content.push_back(resolve_node(*element, scope));
            // each content element is scanned for its own marker, because only a base declares the content point (§11.1)
            for (const std::shared_ptr<CheckedNode>& element : content) {
                // the markers are collected, because one is enough to report (§11.1)
                std::vector<SourceSpan> slots;
                collect_slot_spans(*element, slots);
                // a marker inside the content is reported and dropped, because the base carries the only one (§11.1)
                if (!slots.empty()) {
                    m_diagnostics.add(Diagnostic::Severity::Error, slots.front(), "E0427", "@children marks the content point of a base component, so a content element may not declare one");
                    strip_slots(*element);
                }
            }
            // the base's tree is cloned, because the fill must not disturb the base's own checked body (§11.1)
            out.root = clone_node(*base->root);
            // the fill walks the clone, so a marker anywhere in the base's tree accepts the content (§11.1)
            const bool filled = fill_slots(*out.root, content);
            // content with nowhere to land is reported, because the base declared no children slot (§11.1)
            if (!content.empty() && !filled)
                m_diagnostics.add(Diagnostic::Severity::Error, component.base_span, "E0428", "'" + component.base + "' declares no @children for '" + component.name + "' to fill");
            return;
        }
        // a component with no root has nothing to resolve, which is a declaration defect rather than a tree one (§11.1)
        if (component.root == nullptr) {
            // the missing root is reported, because a component must build something (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, component.span, "E0402", "a component must declare a root element");
            return;
        }
        // the root is checked against that scope, which is the second pass (§11.1)
        out.root = resolve_node(*component.root, scope);
        // a base declares one children slot, because two could never both be filled (§11.1)
        std::vector<SourceSpan> slots;
        collect_slot_spans(*out.root, slots);
        for (std::size_t index = 1; index < slots.size(); index++)
            m_diagnostics.add(Diagnostic::Severity::Error, slots[index], "E0427", "a component may declare one @children slot");
    }

    std::shared_ptr<CheckedNode> TypeChecker::resolve_node(const TreeNode& node, const Scope& scope) {
        // the children slot resolves by itself, because it carries no type and never reaches codegen (§11.1)
        if (node.kind == TreeNodeKind::Slot) {
            // the node is allocated with its kind and span, because a fill or a diagnostic reads them (§11.1)
            auto out = std::make_shared<CheckedNode>();
            // the kind marks the content point, which a derived body replaces before codegen sees it (§11.1)
            out->kind = TreeNodeKind::Slot;
            // the span covers the marker, because a report about the slot points at it (§11.1)
            out->span = node.span;
            return out;
        }
        // a repeat is checked by its own rule, because its body sees an extra binding (§11.1)
        if (node.kind == TreeNodeKind::Repeat)
            return resolve_repeat(node, scope);
        // a conditional is checked by its own rule, because its body exists only while the condition holds (§11.1)
        if (node.kind == TreeNodeKind::Conditional)
            return resolve_conditional(node, scope);
        // the node is allocated up front, because the caller takes a shared pointer (§11.1)
        auto out = std::make_shared<CheckedNode>();
        // the kind, the name, the identity and the span come straight from the node (§11.1, AD-11)
        out->kind = node.kind;
        out->element_type = node.element_type;
        out->explicit_id = node.explicit_id;
        out->span = node.span;
        // an element resolves against the file's components first, because a declared name shadows a native one (§11.1)
        const auto own_type = m_file_component_types.find(node.element_type);
        // the catalog decides only when no component has that name, which is the same rule a type follows (§11.1)
        const ElementType* type = own_type != m_file_component_types.end() ? &own_type->second : m_catalog.find(node.element_type);
        // an element the catalog does not hold is reported, because nothing else can check it (§11.1)
        if (type == nullptr) {
            // the problem points at the type name, because that is what the author wrote (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, node.span, "E0403", "'" + node.element_type + "' is not a widget or a declared native component");
            // an empty type stands in, so the rest of the node still gets walked (§11.1)
            const ElementType unknown;
            // the assignments are still walked, because a nested mistake is worth reporting (§11.1)
            resolve_assignments(node, unknown, scope, *out);
            // the handlers are still walked, because a nested mistake is worth reporting (§11.1)
            resolve_handlers(node, unknown, scope, *out);
            // the children are still walked, because a nested mistake is worth reporting (§11.1)
            resolve_children(node, unknown, scope, *out);
            return out;
        }
        // the assignments resolve against the element's properties (§11.1)
        resolve_assignments(node, *type, scope, *out);
        // the handlers resolve against the element's signals (§11.1)
        resolve_handlers(node, *type, scope, *out);
        // the draw calls resolve against the primitive vocabulary, which only a canvas writes (§6.4)
        resolve_draw_calls(node, *type, scope, *out);
        // the children resolve against whether the element is a container (§11.1, §8)
        resolve_children(node, *type, scope, *out);
        return out;
    }

    std::shared_ptr<CheckedNode> TypeChecker::resolve_repeat(const TreeNode& node, const Scope& scope) {
        // the node is allocated up front, because the caller takes a shared pointer (§11.1)
        auto out = std::make_shared<CheckedNode>();
        // the kind, the name and the span come straight from the node (§11.1)
        out->kind = TreeNodeKind::Repeat;
        out->element_type = node.element_type;
        out->explicit_id = node.explicit_id;
        out->span = node.span;
        // the bindings the author wrote are kept, because the body resolves against them (§11.1)
        out->repeat_item_name = node.repeat_item_name;
        out->repeat_index_name = node.repeat_index_name;
        // the source is shared rather than re-owned, because the tree owns the expression (§11.1)
        out->repeat_source = std::shared_ptr<const Expression>(node.repeat_source.get(), [](const Expression*) {});
        // the source is typed in the enclosing scope, because it does not see the item (§11.1)
        out->repeat_source_type = type_expression(*out->repeat_source, scope, out->repeat_dependencies);
        // the source must be a vector or a native type, because a repeat walks something (§11.1)
        if (!out->repeat_source_type.is_error() && !out->repeat_source_type.is(TypeKind::Vector) && !out->repeat_source_type.is(TypeKind::Native)) {
            // the problem points at the source, because that is what the author must change (§11.1)
            report_type_mismatch(*out->repeat_source, out->repeat_source_type, ValueType::parameterized(TypeKind::Vector, out->repeat_source_type));
        }
        // a repeat with no body was already reported by the parser (§11.1)
        if (node.repeat_body != nullptr) {
            // the body sees the component declarations and its own item binding (§11.1)
            const Scope inner{.properties = scope.properties, .repeat = out.get(), .globals = scope.globals};
            // the body is checked against that scope, which is what makes the item resolvable (§11.1)
            out->repeat_body = resolve_node(*node.repeat_body, inner);
        }
        return out;
    }

    std::shared_ptr<CheckedNode> TypeChecker::resolve_conditional(const TreeNode& node, const Scope& scope) {
        // the node is allocated up front, because the caller takes a shared pointer (§11.1)
        auto out = std::make_shared<CheckedNode>();
        // the kind and the span come straight from the node (§11.1)
        out->kind = TreeNodeKind::Conditional;
        out->explicit_id = node.explicit_id;
        out->span = node.span;
        // the condition is shared rather than re-owned, because the tree owns the expression (§11.1)
        out->condition = std::shared_ptr<const Expression>(node.condition.get(), [](const Expression*) {});
        // the condition is typed in the enclosing scope, because the body does not yet exist (§11.1)
        const ValueType condition_type = type_expression(*out->condition, scope, out->condition_dependencies);
        // a condition must be a question, because a conditional either shows the body or does not (§11.1)
        if (!condition_type.is_error() && !condition_type.is(TypeKind::Bool)) {
            // the problem points at the condition, because that is what the author must change (§11.1)
            report_type_mismatch(*out->condition, condition_type, scalar(TypeKind::Bool));
        }
        // a conditional with no body was already reported by the parser (§11.1)
        if (node.conditional_body != nullptr) {
            // the body sees the same scope, because a conditional does not introduce a binding (§11.1)
            out->conditional_body = resolve_node(*node.conditional_body, scope);
        }
        return out;
    }

    void TypeChecker::resolve_draw_calls(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // only a canvas draws, because every other widget paints itself (§6.4)
        if (!node.has_draw)
            return;
        if (type.name != "Canvas") {
            // the problem names the element, because the author must know which types draw (§6.4)
            m_diagnostics.add(Diagnostic::Severity::Error, node.span, "E0601", "'" + type.name + "' does not draw; only a canvas writes a draw block");
            return;
        }
        // the canvas extent is readable inside the block, because a drawing is laid out in it (§6.4)
        const Scope draw_scope{.properties = scope.properties, .repeat = scope.repeat, .in_draw = true};
        // each call resolves on its own, because one mistake must not hide the rest (§6.4)
        for (const DrawCall& call : node.draw_calls) {
            // the call is assembled locally, because the primitive decides its arity (§6.4)
            CheckedDrawCall out_call;
            // the name and the span come straight from the written call (§6.4)
            out_call.primitive = call.primitive;
            out_call.span = call.span;
            // the vocabulary decides how many arguments the primitive takes (§6.4)
            std::size_t expected = 0;
            bool known = true;
            // the arity is checked here, because a primitive with the wrong count cannot be emitted (§6.4)
            if (call.primitive == "fill_rect" || call.primitive == "fill_rounded_rect")
                expected = call.primitive == "fill_rect" ? 5u : 6u;
            else
                known = false;
            if (!known) {
                // the problem names the primitive, because the vocabulary is closed (§6.4)
                m_diagnostics.add(Diagnostic::Severity::Error, call.span, "E0602", "'" + call.primitive + "' is not a drawing primitive; the vocabulary is fill_rect and fill_rounded_rect");
                continue;
            }
            if (call.arguments.size() != expected) {
                // the problem points at the call, because that is what the author must fix (§6.4)
                m_diagnostics.add(Diagnostic::Severity::Error, call.span, "E0603", "'" + call.primitive + "' takes " + std::to_string(expected) + " arguments");
                continue;
            }
            // each argument is typed in the draw scope, which reads the canvas extent (§6.4)
            std::size_t index = 0;
            bool arguments_ok = true;
            for (const ExpressionPtr& argument : call.arguments) {
                // the argument shares the parsed expression, because the tree owns it (§11.1)
                SharedExpression shared(argument.get(), [](const Expression*) {});
                // the colour argument is the only one that is not a number (§6.4)
                const bool colour = index + 1 == expected;
                // the type is checked against what the primitive expects, because nothing converts implicitly (§11.1)
                const ValueType actual = type_expression(*shared, draw_scope, out_call.dependencies);
                // a whole number is a coordinate, because a drawing is measured in pixels and no scale applies (§6.4)
                const bool measure_ok = actual.is(TypeKind::Int) && !colour;
                if (!actual.is_error() && !measure_ok && !assignable(actual, colour ? scalar(TypeKind::Color) : scalar(TypeKind::Float)))
                    arguments_ok = false;
                // the argument is kept, because the codegen emits it into the body (§6.4)
                out_call.arguments.push_back(shared);
                index++;
            }
            if (!arguments_ok) {
                // the problem points at the call, because the mismatch is in one of its arguments (§6.4)
                m_diagnostics.add(Diagnostic::Severity::Error, call.span, "E0604", "'" + call.primitive + "' takes numbers and a final colour");
                continue;
            }
            // the call is kept, because the codegen emits it into the draw body (§6.4)
            out.draw_calls.push_back(std::move(out_call));
        }
    }

    void TypeChecker::resolve_assignments(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // each assignment resolves on its own, because one mistake must not hide the rest (§11.1)
        for (const PropertyAssignment& assignment : node.assignments)
            resolve_assignment(assignment, type, scope, out);
    }

    void TypeChecker::resolve_assignment(const PropertyAssignment& assignment, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // the assignment is assembled locally, because one mistake must not hide the rest (§11.1)
        CheckedAssignment out_assignment;
        // the name and the span come straight from the declaration (§11.1)
        out_assignment.name = assignment.name;
        out_assignment.span = assignment.span;
        // the value shares the parsed expression, because the tree owns it (§11.1)
        out_assignment.value = std::shared_ptr<const Expression>(assignment.value.get(), [](const Expression*) {});
        // the value is typed before the property is consulted, because a value may itself be broken (§11.1)
        out_assignment.type = type_expression(*out_assignment.value, scope, out_assignment.dependencies);
        // the property must exist on the element type, because the codegen emits a setter for it (§11.1)
        const ElementProperty* property = type.find_property(assignment.name);
        // a name the type does not expose is reported, because there is no setter to emit (§11.1)
        if (property == nullptr) {
            // the problem names both sides, because the author needs to know what the type does expose (§11.1)
            report_unknown_property(assignment.span, type.name, assignment.name);
            // the assignment is still kept, so the codegen never sees a half built node (§11.1)
            out.assignments.push_back(std::move(out_assignment));
            return;
        }
        // the value must match the property, because nothing widens implicitly (§11.1)
        if (!assignable(out_assignment.type, property->type)) {
            // the mismatch points at the value, because that is what the author must change (§11.1)
            report_type_mismatch(*out_assignment.value, out_assignment.type, property->type);
        }
        // the assignment is kept, because the codegen emits it as checked (§11.1)
        out.assignments.push_back(std::move(out_assignment));
    }

    void TypeChecker::resolve_handlers(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // each handler resolves on its own, because one mistake must not hide the rest (§11.1)
        for (const HandlerAttachment& handler : node.handlers)
            resolve_handler(handler, type, scope, out);
    }

    void TypeChecker::resolve_handler(const HandlerAttachment& handler, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // the handler is assembled locally, because one mistake must not hide the rest (§11.1)
        CheckedHandler out_handler;
        // the name and the span come straight from the attachment (§11.1)
        out_handler.name = handler.name;
        out_handler.span = handler.span;
        // the signal must exist on the element, because the codegen wires its callback to it (§11.1)
        const ElementSignal* signal = type.find_signal(handler.name);
        if (signal == nullptr) {
            // the problem names both sides, because the author needs to know what the type does raise (§11.1)
            report_unknown_signal(handler.span, type.name, handler.name);
        }
        // the body resolves against the component plus the signal parameters, which the body reads by name (§11.1)
        Scope body_scope = scope;
        body_scope.handler_signal = signal;
        for (const HandlerStatement& statement : handler.body)
            resolve_statement(statement, body_scope, out_handler);
        // the handler is kept, because the codegen wires a callback to it (§11.1)
        out.handlers.push_back(std::move(out_handler));
    }

    void TypeChecker::resolve_statement(const HandlerStatement& statement, const Scope& scope, CheckedHandler& out) {
        // a statement the parser could not read carries no value, and the parser already reported it (§11.1)
        if ((statement.kind == HandlerStatementKind::Assignment || statement.kind == HandlerStatementKind::FieldAssignment) && statement.value == nullptr)
            return;
        // the statement is assembled locally, because one mistake must not hide the rest (§11.1)
        CheckedStatement out_statement;
        // the kind, the target and the span come straight from the declaration (§11.1)
        out_statement.kind = statement.kind;
        out_statement.target = statement.target;
        out_statement.span = statement.span;
        // an assignment is checked against the property it fills (§11.1)
        if (statement.kind == HandlerStatementKind::Assignment) {
            // the assigned value shares the parsed expression, because the tree owns it (§11.1)
            out_statement.value = std::shared_ptr<const Expression>(statement.value.get(), [](const Expression*) {});
            // the scope is the component's own, because a handler may reach any declared property (§11.1)
            const ValueType value_type = type_expression(*out_statement.value, scope, out_statement.dependencies);
            // the target must be a declared property of the component (§11.1)
            const CheckedProperty* target = nullptr;
            // the declarations are scanned, because only they can be the target (§11.1)
            for (const CheckedProperty& property : *scope.properties) {
                // the name decides, so the target resolves to exactly one declaration (§11.1)
                if (property.name == statement.target)
                    target = &property;
            }
            // a target that is not a declared property is reported, because there is nothing to assign (§11.1)
            if (target == nullptr) {
                // a module-scoped value answers the name, which is what a write to one reports as (§11.1)
                if (is_global(statement.target))
                    report_read_only(statement.target, statement.span);
                // the problem points at the statement, because that is where the target is written (§11.1)
                else
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0404", "'" + statement.target + "' is not a property of this component");
            }
            else {
                // an incoming property may not be assigned from a handler, because a binding owns it (§11.1, §7)
                if (target->direction != PropertyDirection::InOut)
                    report_not_assignable(statement, statement.target);
                // the value must match the property, because nothing widens implicitly (§11.1)
                if (!assignable(value_type, target->type)) {
                    // the mismatch points at the value, because that is what the author must change (§11.1)
                    report_type_mismatch(*out_statement.value, value_type, target->type);
                }
                // the write is recorded, because the graph needs it to order invalidation (§7.3, AD-04a)
                out.writes.push_back(statement.target);
            }
        }
        else if (statement.kind == HandlerStatementKind::SignalInvocation) {
            // the invoked signal must be one the component declares (§11.1)
            const CheckedSignal* declared = nullptr;
            // the signals of the component are scanned, because only they can be invoked (§11.1)
            for (const CheckedSignal& candidate : *m_signals) {
                // the name decides, so the invocation resolves to exactly one declaration (§11.1)
                if (candidate.name == statement.target)
                    declared = &candidate;
            }
            // an invocation of something the component does not declare is reported (§11.1)
            if (declared == nullptr) {
                // the problem points at the statement, because that is where the target is written (§11.1)
                m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0405", "'" + statement.target + "' is not a signal of this component");
            }
            else {
                // the argument count must match, because the facade emits one signature (§11.1)
                if (statement.arguments.size() != declared->parameter_types.size()) {
                    // the mismatch is reported against the statement, because that is where the count is written (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0406", "'" + statement.target + "' takes " + std::to_string(declared->parameter_types.size()) + " argument(s)");
                }
                else {
                    // each argument shares the parsed expression, because the tree owns it (§11.1)
                    for (const ExpressionPtr& argument : statement.arguments)
                        out_statement.arguments.push_back(std::shared_ptr<const Expression>(argument.get(), [](const Expression*) {}));
                    // each argument must match its parameter, which is checked in order (§11.1)
                    for (std::size_t index = 0; index < out_statement.arguments.size(); index++) {
                        // the argument is typed, so a nested mistake is reported too (§11.1)
                        const ValueType argument_type = type_expression(*out_statement.arguments.at(index), scope, out_statement.dependencies);
                        // the argument must match its parameter, because nothing widens implicitly (§11.1)
                        if (!assignable(argument_type, declared->parameter_types.at(index))) {
                            // the mismatch points at the argument, because that is what the author must change (§11.1)
                            report_type_mismatch(*out_statement.arguments.at(index), argument_type, declared->parameter_types.at(index));
                        }
                    }
                }
            }
        }
        else if (statement.kind == HandlerStatementKind::FieldAssignment) {
            // the assigned value shares the parsed expression, because the tree owns it (§11.1)
            out_statement.value = std::shared_ptr<const Expression>(statement.value.get(), [](const Expression*) {});
            // the scope is the component's own, because a handler may reach any declared property (§11.1)
            const ValueType value_type = type_expression(*out_statement.value, scope, out_statement.dependencies);
            // the target must be a declared property of the component (§11.1)
            const CheckedProperty* target = nullptr;
            // the declarations are scanned, because only they can be the target (§11.1)
            for (const CheckedProperty& property : *scope.properties) {
                // the name decides, so the target resolves to exactly one declaration (§11.1)
                if (property.name == statement.target)
                    target = &property;
            }
            // a target that is not a declared property is reported, because there is nothing to write into (§11.1)
            if (target == nullptr) {
                // a module-scoped value answers the name, which is what a write to one reports as (§11.1)
                if (is_global(statement.target))
                    report_read_only(statement.target, statement.span);
                // the problem points at the statement, because that is where the target is written (§11.1)
                else
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0404", "'" + statement.target + "' is not a property of this component");
            }
            // an incoming property may not be written from a handler, because a binding owns it (§11.1, §7)
            else if (target->direction != PropertyDirection::InOut)
                report_not_assignable(statement, statement.target);
            // only a record carries fields, so a scalar target has none to write (§11.1)
            else if (!target->type.is(TypeKind::Native)) {
                // the problem names the type, because a scalar has no fields (§11.1)
                m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0417", "'" + statement.target + "' is a " + target->type.to_string() + ", so it has no fields");
            }
            else {
                // the record decides the fields, so it is resolved against the catalog (§11.2, AD-17)
                const RecordType* record = m_catalog.find_record(target->type.native_name());
                // a native type that is not a record carries no fields, which is reported (§11.2, AD-17)
                if (record == nullptr) {
                    // the problem points at the statement, because that is where the field is written (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0417", "'" + statement.target + "' is not a record, so it has no fields");
                }
                else {
                    // the field name resolves against the declaration, which the record wrote (§11.2, AD-17)
                    const RecordFieldType* field = record->find_field(statement.field);
                    // a field the record does not declare cannot be written, which is reported rather than guessed (§11.2)
                    if (field == nullptr) {
                        // the problem points at the field name, because that is the part the author must change (§11.2)
                        m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0416", "'" + record->name + "' has no field named '" + statement.field + "'");
                    }
                    // the value must match the field, because nothing widens implicitly (§11.1)
                    else if (!assignable(value_type, field->type)) {
                        // the mismatch points at the value, because that is what the author must change (§11.1)
                        report_type_mismatch(*out_statement.value, value_type, field->type);
                    }
                    // the field is kept, because the codegen emits the write through it (§11.1)
                    out_statement.field = statement.field;
                    // the write is recorded, because the graph needs it to order invalidation (§7.3, AD-04a)
                    out.writes.push_back(statement.target);
                }
            }
        }
        else {
            // a property method runs a built-in on a declared property, so the target must be one (§11.1)
            const CheckedProperty* target = nullptr;
            // the declarations are scanned, because only they can be the target (§11.1)
            for (const CheckedProperty& property : *scope.properties) {
                // the name decides, so the target resolves to exactly one declaration (§11.1)
                if (property.name == statement.target)
                    target = &property;
            }
            // a target that is not a declared property is reported, because there is nothing to mutate (§11.1)
            if (target == nullptr) {
                // a module-scoped value answers the name, which is what a mutation of one reports as (§11.1)
                if (is_global(statement.target))
                    report_read_only(statement.target, statement.span);
                // the problem points at the statement, because that is where the target is written (§11.1)
                else
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0404", "'" + statement.target + "' is not a property of this component");
            }
            // a command property takes the one built-in that fires it, and firing writes nothing (§18.1, AD-15)
            else if (target->type.is(TypeKind::Command)) {
                // the method is copied, because the codegen emits it as checked (§11.1)
                out_statement.method = statement.method;
                // the closed set of one is matched, because any other name has no invocation to emit (§11.1, §18.1)
                if (statement.method != "invoke") {
                    // the problem names the method, because that is the part the author must change (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0418", "'" + statement.method + "' is not a command method; the closed set is invoke");
                }
                // a handler carries no argument to a command yet, so the bare form is the whole surface (§18.3)
                else if (!statement.arguments.empty()) {
                    // the problem points at the statement, because that is where the argument is written (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0406", "'invoke' takes 0 argument(s)");
                }
                // no write is recorded, because firing a command changes no property of this component (§7.3, §18.1)
            }
            // only a vector property carries the collection methods, because that is the closed set (§11.1)
            else if (!target->type.is(TypeKind::Vector)) {
                // the problem names the type, because a scalar has no collection methods (§11.1)
                m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0419", "'" + statement.target + "' is a " + target->type.to_string() + ", so it has no collection methods");
            }
            // an incoming property may not be mutated from a handler, because a binding owns it (§11.1, §7)
            else if (target->direction != PropertyDirection::InOut) {
                report_not_assignable(statement, statement.target);
            }
            else {
                // the element type is what every argument is checked against, so it is kept (§11.1)
                out_statement.element = target->type.element();
                // the method name decides the arity and the argument types, which are a closed set (§11.1)
                const std::string& method = statement.method;
                // each argument shares the parsed expression, because the tree owns it (§11.1)
                for (const ExpressionPtr& argument : statement.arguments)
                    out_statement.arguments.push_back(std::shared_ptr<const Expression>(argument.get(), [](const Expression*) {}));
                // the write is recorded, because the graph needs it to order invalidation (§7.3, AD-04a)
                out.writes.push_back(statement.target);
                // the method is copied, because the codegen emits it as checked (§11.1)
                out_statement.method = method;
                // the closed set of methods is matched, because an unknown one has no mutation (§11.1)
                if (method == "append") {
                    // append takes exactly one element, which is the value pushed (§11.1)
                    if (out_statement.arguments.size() != 1) {
                        m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0406", "'append' takes 1 argument(s)");
                    }
                    else {
                        // the argument must be the element type, because nothing widens implicitly (§11.1)
                        const ValueType argument_type = type_expression(*out_statement.arguments.at(0), scope, out_statement.dependencies);
                        if (!assignable(argument_type, out_statement.element))
                            report_type_mismatch(*out_statement.arguments.at(0), argument_type, out_statement.element);
                    }
                }
                else if (method == "remove_at") {
                    // remove_at takes exactly one index, which is the position erased (§11.1)
                    if (out_statement.arguments.size() != 1) {
                        m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0406", "'remove_at' takes 1 argument(s)");
                    }
                    else {
                        // the argument must be an integer, because a position is a count (§11.1)
                        const ValueType argument_type = type_expression(*out_statement.arguments.at(0), scope, out_statement.dependencies);
                        if (!assignable(argument_type, scalar(TypeKind::Int)))
                            report_type_mismatch(*out_statement.arguments.at(0), argument_type, scalar(TypeKind::Int));
                    }
                }
                else if (method == "clear") {
                    // clear takes no argument, because it drops every element (§11.1)
                    if (!out_statement.arguments.empty()) {
                        m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0406", "'clear' takes 0 argument(s)");
                    }
                }
                else if (method == "set") {
                    // set takes an index and a value, because it replaces one element (§11.1)
                    if (out_statement.arguments.size() != 2) {
                        m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0406", "'set' takes 2 argument(s)");
                    }
                    else {
                        // the index must be an integer, because a position is a count (§11.1)
                        const ValueType index_type = type_expression(*out_statement.arguments.at(0), scope, out_statement.dependencies);
                        if (!assignable(index_type, scalar(TypeKind::Int)))
                            report_type_mismatch(*out_statement.arguments.at(0), index_type, scalar(TypeKind::Int));
                        // the value must be the element type, because nothing widens implicitly (§11.1)
                        const ValueType value_type = type_expression(*out_statement.arguments.at(1), scope, out_statement.dependencies);
                        if (!assignable(value_type, out_statement.element))
                            report_type_mismatch(*out_statement.arguments.at(1), value_type, out_statement.element);
                    }
                }
                else {
                    // a method the language does not define has no mutation, so it is reported (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0418", "'" + method + "' is not a collection method; the closed set is append, remove_at, clear and set");
                }
            }
        }
        // the statement is kept, because the codegen emits it as checked (§11.1)
        out.body.push_back(std::move(out_statement));
    }

    void TypeChecker::resolve_children(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // a leaf takes no children, so one written under it is reported rather than dropped (§11.1, §8)
        if (!node.children.empty() && !type.accepts_children) {
            // an instance of a file component takes content only through a derived one, where the slot is filled (§11.1)
            if (m_file_component_types.contains(type.name))
                m_diagnostics.add(Diagnostic::Severity::Error, node.children.at(0)->span, "E0430", "'" + type.name + "' takes content only through a derived component, not through an instance");
            else
                // the problem points at the first child, because that is what cannot go here (§11.1)
                m_diagnostics.add(Diagnostic::Severity::Error, node.children.at(0)->span, "E0407", "'" + type.name + "' cannot contain child elements");
        }
        // the children are checked either way, because a nested mistake is worth reporting (§11.1)
        for (const std::unique_ptr<TreeNode>& child : node.children) {
            // the child sees the same scope, because a container does not introduce a binding (§11.1)
            out.children.push_back(resolve_node(*child, scope));
        }
    }

    ValueType TypeChecker::type_expression(const Expression& expression, const Scope& scope, std::vector<std::string>& dependencies) {
        // a literal carries its own type, because its kind says what it is (§11.1)
        if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression))
            return type_literal(*literal);
        // a translation macro always produces a string, because that is what the catalog holds (§9.2)
        if (dynamic_cast<const TranslationExpression*>(&expression) != nullptr)
            return scalar(TypeKind::String);
        // an embedded image produces an immutable handle, which is what the render path takes (§6.4)
        if (dynamic_cast<const ImageExpression*>(&expression) != nullptr)
            return scalar(TypeKind::Image);
        // a reference resolves against the scope (§11.1)
        if (const auto* reference = dynamic_cast<const PropertyExpression*>(&expression))
            return type_property(*reference, scope, dependencies);
        // a conversion resolves against the closed rule set (§11.1)
        if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression))
            return type_conversion(*conversion, scope, dependencies);
        // a colour operation resolves against the same closed set, because the language defines no more (§6.1)
        if (const auto* call = dynamic_cast<const CallExpression*>(&expression))
            return type_call(*call, scope, dependencies);
        // a binary application types both operands and then applies the operator rule (§11.1)
        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
            return type_binary(*binary, scope, dependencies);
        // a unary application types its operand and then applies the operator rule (§11.1)
        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
            return type_unary(*unary, scope, dependencies);
        // a grouping contributes nothing, so its inner expression types the whole (§11.1)
        if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
            return type_expression(*grouping->inner, scope, dependencies);
        // a bracketed list types as the collection its elements agree on (§8.1)
        if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression))
            return type_array(*array, scope, dependencies);
        // a construction types against the record its written name declares (§11.1)
        if (const auto* construction = dynamic_cast<const RecordConstructionExpression*>(&expression))
            return type_record_construction(*construction, scope, dependencies);
        // a field read resolves the object first, because the field is only known once the record is (§11.2)
        if (const auto* access = dynamic_cast<const FieldAccessExpression*>(&expression))
            return type_field_access(*access, scope, dependencies);
        // a form with no rule is reported, because typing it as nothing would let it through unchecked (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, expression.location(), "E0415", "this expression form has no type rule");
        // the error type stands in, so nothing downstream raises a second complaint (§11.1)
        return error_type();
    }

    ValueType TypeChecker::type_field_access(const FieldAccessExpression& access, const Scope& scope, std::vector<std::string>& dependencies) {
        // a value set is written as a bare name, so it is resolved before the object is typed as a reference (§11.1)
        if (const auto* set_name = dynamic_cast<const PropertyExpression*>(access.object.get()); set_name != nullptr) {
            // the name decides, so a written value resolves to exactly one set, declared or builtin (§11.1)
            if (const NamedValueSet* set = m_catalog.find_named_value_set(set_name->name)) {
                // a member the set does not carry is reported against the ones it does, which the author needs (§11.1)
                if (set->find_member(access.field) == nullptr)
                    report_unknown_value_member(access, *set);
                // a set the file declared produces the type its name resolves to, which a property of it holds (§11.1)
                if (set->user)
                    return ValueType::native(set->name);
                // a builtin set decides the kind, and a member that resolved types as that kind (§11.1)
                return scalar(set->kind);
            }
        }
        // the object is typed first, because the field is only known once the record type is (§11.2)
        const ValueType object = type_expression(*access.object, scope, dependencies);
        // an object whose type failed names no record, so the field cannot resolve (§11.2)
        if (object.is_error())
            return error_type();
        // only a native type carries fields, because a scalar has no members (§11.2, AD-17)
        if (!object.is(TypeKind::Native)) {
            // the problem names both sides, because the author needs to know a value has no fields (§11.2)
            report_type_mismatch(access, object, ValueType::native("*record*"));
            return error_type();
        }
        // a field read reaches a record only when the manifest declared one by that name (§11.2, AD-17)
        const RecordType* record = m_catalog.find_record(object.native_name());
        // a native type that is not a record may be a value set the file declared, whose members resolve by name (§11.1)
        if (record == nullptr) {
            // the set is looked up by the object's name, because a property of the enum holds one member of it (§11.1)
            if (const NamedValueSet* set = m_catalog.find_named_value_set(object.native_name()); set != nullptr && set->user) {
                // a member the set does not carry is reported against the ones it does, which the author needs (§11.1)
                if (set->find_member(access.field) == nullptr)
                    report_unknown_value_member(access, *set);
                // the set decides the type, which is what a property of it holds (§11.1)
                return ValueType::native(set->name);
            }
        }
        // a native type that is not a record carries no fields, which the manifest declared as data (§11.2, AD-17)
        if (record == nullptr) {
            // the problem points at the access, because that is what the author must change (§11.2, AD-17)
            m_diagnostics.add(Diagnostic::Severity::Error, access.span, "E0417", "'" + object.native_name() + "' is not a record, so it has no fields");
            return error_type();
        }
        // the field name resolves against the declaration, which the manifest wrote (§11.2, AD-17)
        const RecordFieldType* field = record->find_field(access.field);
        // a field the record does not declare cannot be read, which is reported rather than guessed (§11.2, AD-17)
        if (field == nullptr) {
            // the problem points at the field name, because that is the part the author must change (§11.2)
            m_diagnostics.add(Diagnostic::Severity::Error, access.span, "E0416", "'" + record->name + "' has no field named '" + access.field + "'");
            return error_type();
        }
        // the declared field type is the whole of the answer, because there is no inference (§11.2, AD-17)
        return field->type;
    }

    ValueType TypeChecker::type_array(const ArrayExpression& array, const Scope& scope, std::vector<std::string>& dependencies) {
        // an empty list has no element to read a type from, so the element is left unknown for the target to fill (§8.1)
        if (array.elements.empty())
            return ValueType::parameterized(TypeKind::Vector, error_type());
        // the first element decides the collection, because every other element must agree with it (§8.1)
        ValueType element = type_expression(*array.elements.front(), scope, dependencies);
        // a later element is compared against the first, and each disagreement is reported once (§8.1)
        for (std::size_t index = 1; index < array.elements.size(); index++) {
            // the element is typed in the same scope, and its sources are dependencies of the whole list (§11.1)
            const ValueType other = type_expression(*array.elements.at(index), scope, dependencies);
            // an element of unknown type decides nothing, because the error already raised is the real complaint (§8.1)
            if (other.is_error())
                element = error_type();
            // a disagreement makes the collection untyped, so no second complaint follows from the default (§8.1)
            else if (!element.is_error() && !element.equals(other)) {
                // the problem points at the element that disagreed, because that is what the author must change (§8.1)
                m_diagnostics.add(Diagnostic::Severity::Error, array.elements.at(index)->location(), "E0408", "a collection holds one element type, but '" + other.to_string() + "' does not match '" + element.to_string() + "'");
                // the element type is left unknown, so the assignment check raises nothing further (§8.1)
                element = error_type();
            }
        }
        // the agreed element type is the collection, and an unknown one keeps the collection unknown (§8.1)
        return ValueType::parameterized(TypeKind::Vector, element);
    }

    ValueType TypeChecker::type_record_construction(const RecordConstructionExpression& construction, const Scope& scope, std::vector<std::string>& dependencies) {
        // the written name resolves first, because the fields must be read from somewhere (§11.1)
        const ValueType type = resolve_type_name(construction.type_name, m_catalog);
        // a name that resolves to nothing reports once here, because no field can resolve against it (§11.1)
        if (type.is_error()) {
            report_unknown_type(construction.type_name, construction.span);
            return error_type();
        }
        // only a record carries fields, because a scalar construction has none to fill (§11.2, AD-17)
        const RecordType* record = type.is(TypeKind::Native) ? m_catalog.find_record(type.native_name()) : nullptr;
        if (record == nullptr) {
            // the problem points at the construction, because that is what the author must change (§11.2, AD-17)
            m_diagnostics.add(Diagnostic::Severity::Error, construction.span, "E0417", "'" + construction.type_name + "' is not a record, so it has no fields");
            return error_type();
        }
        // each field is read in written order, because each pairs one declared field with a value (§11.1)
        for (std::size_t index = 0; index < construction.fields.size(); index++) {
            const ConstructionField& field = construction.fields.at(index);
            // a field the record does not declare is reported against the ones it does (§11.2, AD-17)
            const RecordFieldType* declared = record->find_field(field.name);
            if (declared == nullptr) {
                m_diagnostics.add(Diagnostic::Severity::Error, field.value->location(), "E0416", "'" + record->name + "' has no field named '" + field.name + "'");
                continue;
            }
            // a field named twice is reported, because the construction would bind it once (§11.1)
            for (std::size_t prior = 0; prior < index; prior++)
                if (construction.fields.at(prior).name == field.name) {
                    m_diagnostics.add(Diagnostic::Severity::Error, field.value->location(), "E0423", "'" + record->name + "' names field '" + field.name + "' twice");
                    break;
                }
            // the value is typed next, because it must match the field it fills (§11.1)
            const ValueType value_type = type_expression(*field.value, scope, dependencies);
            // the value must match the declared field, because nothing widens implicitly (§11.1)
            if (!value_type.is_error() && !assignable(value_type, declared->type))
                report_type_mismatch(*field.value, value_type, declared->type);
        }
        // the construction produces exactly the record, because there is no inference (§11.1)
        return type;
    }

    ValueType TypeChecker::type_literal(const LiteralExpression& literal) {
        // the kind of the literal decides its type, because the language has no other evidence (§11.1)
        switch (literal.kind) {
        // an integer literal is an integer, and nothing widens it (§11.1)
        case LiteralExpression::Kind::Integer:
            return scalar(TypeKind::Int);
        // a floating point literal is a float (§11.1)
        case LiteralExpression::Kind::Float:
            return scalar(TypeKind::Float);
        // a string literal is text (§11.1)
        case LiteralExpression::Kind::String:
            return scalar(TypeKind::String);
        // a colour literal is a colour (§11.1)
        case LiteralExpression::Kind::Color:
            return scalar(TypeKind::Color);
        // a length literal carries its unit, which the codegen applies (§11.1, AD-27)
        case LiteralExpression::Kind::Length:
            return scalar(TypeKind::Length);
        // a truth literal is a bool (§11.1)
        case LiteralExpression::Kind::Boolean:
            return scalar(TypeKind::Bool);
        }
        // a kind outside the declared set is reported, because typing it as nothing would let it through (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, literal.span, "E0415", "this literal kind has no type rule");
        // the error type stands in, so nothing downstream raises a second complaint (§11.1)
        return error_type();
    }

    ValueType TypeChecker::type_property(const PropertyExpression& reference, const Scope& scope, std::vector<std::string>& dependencies) {
        // the scope answers what the name means, because a name means one thing per scope (§11.1)
        const std::optional<ValueType> found = scope.lookup(reference.name);
        // a name the scope does not hold is reported, because there is nothing to read (§11.1)
        if (!found.has_value()) {
            // the problem points at the reference, because that is what the author must change (§11.1)
            report_unknown_name(reference, reference.name);
            return error_type();
        }
        // a repeat binding is not a component property, so it registers no dependency (§7.3, AD-04a)
        bool is_component_property = false;
        // the declarations are scanned, because only they are a dependency, and a function scope holds none (§11.1)
        if (scope.properties != nullptr)
            for (const CheckedProperty& property : *scope.properties) {
                // the name decides, so the dependency belongs to exactly one property (§7.3, AD-04a)
                if (property.name == reference.name)
                    is_component_property = true;
            }
        // a component property becomes a dependency, because the binding invalidates when it changes (§7.3, AD-04a)
        if (is_component_property) {
            // a repeated read registers the dependency once, because the engine keeps a set (§7.3, AD-04a)
            if (std::find(dependencies.begin(), dependencies.end(), reference.name) == dependencies.end())
                dependencies.push_back(reference.name);
        }
        // the declared type is what a reference reads, because there is no inference (§11.1)
        return *found;
    }

    ValueType TypeChecker::type_conversion(const ConversionExpression& conversion, const Scope& scope, std::vector<std::string>& dependencies) {
        // the operand is typed first, because a conversion reads a value (§11.1)
        const ValueType from = type_expression(*conversion.value, scope, dependencies);
        // the target must be one of the four builtin conversions (§11.1)
        TypeKind target = TypeKind::Error;
        // a name that is not a conversion was already reported by the parser, so nothing is reported twice (§11.1)
        if (!builtin_conversion_target(conversion.target_type, target))
            return error_type();
        // the rule set decides, because the conversions are closed (§11.1)
        if (!conversion_allowed(from.kind(), target)) {
            // the problem names both sides, because the author needs to know what was refused (§11.1)
            report_type_mismatch(conversion, from, scalar(target));
            return error_type();
        }
        // the conversion produces exactly the target, because there is no inference (§11.1)
        return scalar(target);
    }

    ValueType TypeChecker::type_call(const CallExpression& call, const Scope& scope, std::vector<std::string>& dependencies) {
        // a declared function is resolved first, because it shadows a builtin of the same name (§11.1)
        if (m_functions != nullptr)
            for (const CheckedFunction& function : *m_functions) {
                // the name decides, so a call resolves to exactly one declaration (§11.1)
                if (function.name != call.function)
                    continue;
                // the arity must match, because a call that passes the wrong count cannot resolve (§11.1)
                if (call.arguments.size() != function.parameters.size()) {
                    // the report names the name and the count, because both are what the author has to change (§11.1)
                    m_diagnostics.add(Diagnostic::Severity::Error, call.location(), "E0417", "'" + call.function + "' takes " + std::to_string(function.parameters.size()) + " arguments");
                    return error_type();
                }
                // each argument is typed and checked against its parameter, because nothing widens implicitly (§11.1)
                for (std::size_t index = 0; index < call.arguments.size(); index++) {
                    // the argument is typed first, so the dependency list follows the written order (§7.3, AD-04a)
                    const ValueType argument = type_expression(*call.arguments.at(index), scope, dependencies);
                    // an argument that failed suppresses the rule, because its type is not known (§11.1)
                    if (argument.is_error())
                        return error_type();
                    // the parameter it fills is the type the argument must match (§11.1)
                    const ValueType& parameter = function.parameters.at(index).second;
                    // the argument must fill the parameter, because nothing widens implicitly (§11.1)
                    if (!assignable(argument, parameter)) {
                        // the mismatch points at the argument, because that is what the author wrote (§11.1)
                        report_type_mismatch(*call.arguments.at(index), argument, parameter);
                        return error_type();
                    }
                }
                // a call to a declared function carries the declared return type (§11.1)
                return function.return_type;
            }
        // a call the language does not define has no type, which the report names (§11.1)
        if (call.function != "transparentize" && call.function != "alpha" && call.function != "alpha_set") {
            // the report names the name, because that is what the author has to change (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, call.location(), "E0416", "there is no function named '" + call.function + "'");
            return error_type();
        }
        // every operation takes a colour and an amount, so a tree that does not is refused rather than indexed (§11.1)
        if (call.arguments.size() != 2) {
            // the report names the name and the count, because both are what the author has to change (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, call.location(), "E0417", "'" + call.function + "' takes exactly two arguments");
            return error_type();
        }
        // the colour is typed first, because every operation works on one (§6.1)
        const ValueType colour = type_expression(*call.arguments.at(0), scope, dependencies);
        // the amount is typed second, so the dependency list follows the written order (§7.3, AD-04a)
        const ValueType amount = type_expression(*call.arguments.at(1), scope, dependencies);
        // an argument that failed suppresses the rule, because its type is not known (§11.1)
        if (colour.is_error() || amount.is_error())
            return error_type();
        // the first argument must be a colour, because the operation has nothing else to work on (§6.1)
        if (!colour.is(TypeKind::Color)) {
            // the report names what was written, because the author needs to know what to fix (§11.1)
            report_type_mismatch(call, colour, scalar(TypeKind::Color));
            return error_type();
        }
        // the amount must be a number, because it is a fraction or an alpha (§6.1)
        if (!amount.is_numeric()) {
            // the report names what was written, because the author needs to know what to fix (§11.1)
            report_type_mismatch(call, amount, scalar(TypeKind::Float));
            return error_type();
        }
        // every colour operation produces a colour, because it changes only the alpha (§6.1)
        return scalar(TypeKind::Color);
    }

    ValueType TypeChecker::type_binary(const BinaryExpression& binary, const Scope& scope, std::vector<std::string>& dependencies) {
        // both operands are typed before the rule is applied, because the rule needs both (§11.1)
        const ValueType left = type_expression(*binary.left, scope, dependencies);
        // the right operand is typed second, so the dependency list follows the written order (§7.3, AD-04a)
        const ValueType right = type_expression(*binary.right, scope, dependencies);
        // an operand that failed suppresses the rule, because its type is not known (§11.1)
        if (left.is_error() || right.is_error())
            return error_type();
        // concatenation is the one operator that produces a string (§11.1)
        if (binary.op == BinaryOperator::Add) {
            // a text on either side makes the whole a concatenation (§11.1)
            if (left.is(TypeKind::String) || right.is(TypeKind::String)) {
                // a number may join text only through a conversion, so both sides must be text (§11.1)
                if (left.is(TypeKind::String) && right.is(TypeKind::String))
                    return scalar(TypeKind::String);
                // a mixed addition is reported, because nothing converts implicitly (§11.1)
                report_type_mismatch(binary, left, scalar(TypeKind::String));
                return error_type();
            }
            // two numbers add, and the wider of the two decides without any promotion (§11.1)
            if (left.is_numeric() && right.is_numeric()) {
                // a float on either side makes the whole a float (§11.1)
                if (left.is(TypeKind::Float) || right.is(TypeKind::Float))
                    return scalar(TypeKind::Float);
                // two integers stay an integer, because nothing widens implicitly (§11.1)
                return scalar(TypeKind::Int);
            }
            // a colour, a bool or a length has no addition, which is reported (§11.1)
            report_type_mismatch(binary, left, scalar(TypeKind::Float));
            return error_type();
        }
        // subtraction, multiplication and division take numbers only (§11.1)
        if (binary.op == BinaryOperator::Subtract || binary.op == BinaryOperator::Multiply || binary.op == BinaryOperator::Divide) {
            // both sides must be numbers, because the operator has no other meaning (§11.1)
            if (left.is_numeric() && right.is_numeric()) {
                // a float on either side makes the whole a float (§11.1)
                if (left.is(TypeKind::Float) || right.is(TypeKind::Float))
                    return scalar(TypeKind::Float);
                // two integers stay an integer, because nothing widens implicitly (§11.1)
                return scalar(TypeKind::Int);
            }
            // a non numeric operand is reported, because the operator cannot apply (§11.1)
            report_type_mismatch(binary, left, scalar(TypeKind::Float));
            return error_type();
        }
        // the remainder takes integers only, because a float remainder has no defined rounding (§11.1)
        if (binary.op == BinaryOperator::Modulo) {
            // both sides must be integers, because a float has no remainder (§11.1)
            if (left.is(TypeKind::Int) && right.is(TypeKind::Int))
                return scalar(TypeKind::Int);
            // anything else is reported, because the operator cannot apply (§11.1)
            report_type_mismatch(binary, left, scalar(TypeKind::Int));
            return error_type();
        }
        // equality compares numbers or text, because those are the values a comparison means (§11.1)
        if (binary.op == BinaryOperator::Equal || binary.op == BinaryOperator::NotEqual) {
            // the two sides must be comparable, because comparing across types is not meaningful (§11.1)
            const bool both_numbers = left.is_numeric() && right.is_numeric();
            // two texts are comparable, because equality on text is meaningful (§11.1)
            const bool both_text = left.is(TypeKind::String) && right.is(TypeKind::String);
            // a comparable pair produces a bool, because a comparison is a question (§11.1)
            if (both_numbers || both_text)
                return scalar(TypeKind::Bool);
            // a pair that cannot be compared is reported (§11.1)
            report_type_mismatch(binary, left, right);
            return error_type();
        }
        // ordering compares numbers only, because text has no order (§11.1)
        if (binary.op == BinaryOperator::Less || binary.op == BinaryOperator::LessEqual || binary.op == BinaryOperator::Greater || binary.op == BinaryOperator::GreaterEqual) {
            // both sides must be numbers, because the operator has no other meaning (§11.1)
            if (left.is_numeric() && right.is_numeric())
                return scalar(TypeKind::Bool);
            // anything else is reported, because the operator cannot apply (§11.1)
            report_type_mismatch(binary, left, scalar(TypeKind::Float));
            return error_type();
        }
        // conjunction and disjunction take bools, because they combine questions (§11.1)
        if (binary.op == BinaryOperator::And || binary.op == BinaryOperator::Or) {
            // both sides must be bools, because the operator has no other meaning (§11.1)
            if (left.is(TypeKind::Bool) && right.is(TypeKind::Bool))
                return scalar(TypeKind::Bool);
            // anything else is reported, because the operator cannot apply (§11.1)
            report_type_mismatch(binary, left, scalar(TypeKind::Bool));
            return error_type();
        }
        // an operator outside the declared set is reported, because typing it as nothing would let it through (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, binary.span, "E0415", "this operator has no type rule");
        // the error type stands in, so nothing downstream raises a second complaint (§11.1)
        return error_type();
    }

    ValueType TypeChecker::type_unary(const UnaryExpression& unary, const Scope& scope, std::vector<std::string>& dependencies) {
        // the operand is typed first, because the operator applies to it (§11.1)
        const ValueType operand = type_expression(*unary.value, scope, dependencies);
        // an operand that failed suppresses the rule, because its type is not known (§11.1)
        if (operand.is_error())
            return error_type();
        // a negation takes a bool, because it is a question (§11.1)
        if (unary.op == UnaryOperator::Not) {
            // a bool operand produces a bool (§11.1)
            if (operand.is(TypeKind::Bool))
                return scalar(TypeKind::Bool);
            // anything else is reported, because the operator cannot apply (§11.1)
            report_type_mismatch(unary, operand, scalar(TypeKind::Bool));
            return error_type();
        }
        // a sign takes a number, because it is arithmetic (§11.1)
        if (operand.is_numeric())
            return operand;
        // anything else is reported, which includes an operator outside the declared set (§11.1)
        report_type_mismatch(unary, operand, scalar(TypeKind::Float));
        return error_type();
    }

    void TypeChecker::report_unknown_name(const Expression& expression, std::string_view name) {
        // the problem points at the reference, because that is what the author must change (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, expression.location(), "E0409", "'" + std::string(name) + "' is not a property of this component");
    }

    void TypeChecker::report_type_mismatch(const Expression& expression, const ValueType& from, const ValueType& to) {
        // the problem names both sides, because the author needs to know what was refused (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, expression.location(), "E0410", "a value of type '" + from.to_string() + "' cannot be used where '" + to.to_string() + "' is required");
    }

    void TypeChecker::report_unknown_property(SourceSpan span, std::string_view element, std::string_view property_name) {
        // the problem names the element, because the property belongs to it (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, span, "E0411", "'" + std::string(element) + "' has no property named '" + std::string(property_name) + "'");
    }

    void TypeChecker::report_unknown_value_member(const Expression& expression, const NamedValueSet& set) {
        // the members are joined into one list, because the author needs every name they may write (§11.1)
        std::string names;
        for (const NamedValueMember& member : set.members) {
            // the separator goes between the names, so the list never starts or ends with one (§11.1)
            if (!names.empty())
                names += ", ";
            // the written form is listed, because that is what the author has to write (§11.1)
            names += set.name + "." + member.written;
        }
        // the problem lists them, because a named value set is closed and the author cannot guess a member (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, expression.location(), "E0416", "this is not a member of " + set.name + ", which carries " + names);
    }

    void TypeChecker::report_unknown_signal(SourceSpan span, std::string_view element, std::string_view signal_name) {
        // the problem names the element, because the signal belongs to it (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, span, "E0412", "'" + std::string(element) + "' raises no signal named '" + std::string(signal_name) + "'");
    }

    void TypeChecker::report_not_assignable(const HandlerStatement& statement, std::string_view name) {
        // the problem points at the statement, because that is where the assignment is written (§11.1, §7)
        m_diagnostics.add(Diagnostic::Severity::Error, statement.span, "E0413", "'" + std::string(name) + "' is an incoming property and a handler may not assign it");
    }

    void TypeChecker::add_edge(std::vector<std::pair<std::string, std::vector<std::string>>>& edges, std::string_view reader, std::string_view writer) {
        // the reader is looked for, because the edge list is built one writer at a time (§7.3, AD-04a)
        auto existing = std::find_if(edges.begin(), edges.end(), [reader](const auto& edge) { return edge.first == reader; });
        // a reader with no writers yet gets a fresh entry, because the edge set is built once (§7.3, AD-04a)
        if (existing == edges.end()) {
            // the entry is appended, so the walk will visit the reader (§7.3, AD-04a)
            edges.emplace_back(std::string(reader), std::vector<std::string>{});
            // the fresh entry is the one the writer joins (§7.3, AD-04a)
            existing = edges.end() - 1;
        }
        // the writer joins directly, because dependencies are deduped while they are collected (§7.3, AD-04a)
        existing->second.emplace_back(writer);
    }

    void TypeChecker::collect_edges(const CheckedNode& node, std::vector<std::pair<std::string, std::vector<std::string>>>& edges) const {
        // a handler write breaks its binding and a repeat rebuilds the tree, so neither feeds this graph (AD-24)
        static_cast<void>(node);
        static_cast<void>(edges);
    }

    void TypeChecker::visit(const std::vector<std::pair<std::string, std::vector<std::string>>>& edges, const std::string& start, std::vector<std::string>& path, std::vector<std::string>& finished) {
        // a property already finished cannot close a cycle again, because its subgraph was walked (§7.3, AD-04a)
        if (std::find(finished.begin(), finished.end(), start) != finished.end())
            return;
        // a property already on the path is the one a cycle closes on (§7.3, AD-04a)
        const auto on_path = std::find(path.begin(), path.end(), start);
        // the repeated property closes the cycle, so the path from it is the cycle (§7.3, AD-04a)
        if (on_path != path.end()) {
            // the cycle is the tail of the path from the repeated property (§7.3, AD-04a)
            std::vector<std::string> cycle(path.begin() + (on_path - path.begin()), path.end());
            // the repeated property closes the loop, so the rendered chain reads as the cycle it is (§7.3, AD-04a)
            cycle.push_back(start);
            // the cycle is reported against the repeated property, because that is where the author starts (§7.3, AD-04a)
            report_cycle(start, cycle);
            return;
        }
        // the property joins the path, because the walk is a depth first search (§7.3, AD-04a)
        path.push_back(start);
        // the edges of the property are followed, because that is where a cycle can form (§7.3, AD-04a)
        for (const auto& edge : edges) {
            // only the edges of this property matter, because the others were walked from their own node (§7.3, AD-04a)
            if (edge.first == start) {
                // each writer is walked, because a cycle is a chain of them (§7.3, AD-04a)
                for (const std::string& writer : edge.second)
                    visit(edges, writer, path, finished);
            }
        }
        // the property leaves the path, because the walk returns from it (§7.3, AD-04a)
        path.pop_back();
        // the property is finished, because its subgraph was walked (§7.3, AD-04a)
        finished.push_back(start);
    }

    // the walk marks finished properties, so a loop is reported from only one of its members (§7.3, AD-04a)
    void TypeChecker::report_cycle(std::string_view reader, const std::vector<std::string>& path) {
        // the path is rendered as a chain, because a cycle has no single point to blame (§7.3, AD-04a)
        std::string chain;
        // every property on the path contributes its name, in the order the walk found them (§7.3, AD-04a)
        for (const std::string& name : path) {
            // the separator is placed before each name but the first (§7.3, AD-04a)
            if (!chain.empty())
                chain += " -> ";
            chain += name;
        }
        // the span is the declaration of the property the cycle closes on (§7.3, AD-04a)
        SourceSpan span;
        // the declared names are scanned, because only they carry a span (§7.3, AD-04a)
        for (const auto& declared : m_declared) {
            // the name decides, so the span belongs to exactly one declaration (§7.3, AD-04a)
            if (declared.first == reader) {
                span = declared.second;
                break;
            }
        }
        // the message names the whole loop, because a cycle is a property of the graph (§7.3, AD-04a)
        m_diagnostics.add(Diagnostic::Severity::Error, span, "E0414", "static dependency cycle: " + chain);
    }

    void TypeChecker::detect_cycles(const CheckedComponent& component) {
        // only defaults become edges, because a handler write is imperative and a repeat rebuilds the tree (§7.3, AD-04a)
        std::vector<std::pair<std::string, std::vector<std::string>>> edges;
        // every declared property is read, because a cycle is reachable from one of them (§7.3, AD-04a)
        for (const CheckedProperty& property : component.properties) {
            // a property with no default has no outgoing edge, because nothing feeds it (§11.1)
            if (!property.has_default)
                continue;
            // each dependency becomes an edge out of the property, because that is what its default reads (§7.3, AD-04a)
            for (const std::string& dependency : property.default_dependencies)
                add_edge(edges, property.name, dependency);
        }
        // the tree is still walked, so a malformed repeat or handler is reached by the same pass (§11.1)
        if (component.root != nullptr)
            collect_edges(*component.root, edges);
        // the path and the finished set are per component, because the graph is rebuilt each time (§7.3, AD-04a)
        std::vector<std::string> path;
        // the finished set starts empty, because nothing has been walked (§7.3, AD-04a)
        std::vector<std::string> finished;
        // every declared property is a starting point, because a cycle is reachable from one (§7.3, AD-04a)
        for (const CheckedProperty& property : component.properties)
            visit(edges, property.name, path, finished);
    }

    namespace
    {
        // report whether a field type holds another record, which the first release does not emit nested (§11.1)
        bool holds_record(const ValueType& type, const ElementCatalog& catalog) {
            // a native name holds a record only when the catalog declares one by that name (§11.1)
            if (type.is(TypeKind::Native))
                return catalog.find_record(type.native_name()) != nullptr;
            // a parameter form nests its element, which is where a held record hides (§11.1)
            if (type.kind() == TypeKind::Vector || type.kind() == TypeKind::Snapshot)
                return holds_record(type.element(), catalog);
            // a scalar carries no record, which is the common case (§11.1)
            return false;
        }
    } // namespace

    void register_ui_structs(const SourceFile& file, ElementCatalog& catalog, DiagnosticList& diagnostics) {
        // the structs are walked in declaration order, which is the order a field may name an earlier one (§11.1)
        for (const StructDeclaration& declaration : file.structs) {
            // the record starts with its name and collects the fields as they are resolved (§11.1)
            RecordType record{.name = declaration.name, .fields = {}, .from_ui = true};
            // each field is walked, because a field read resolves by name against the declaration (§11.1)
            for (const StructField& field : declaration.fields) {
                // the written type becomes the type a field read produces, resolved against the catalog (§11.1)
                const ValueType resolved = resolve_type_name(field.type, catalog);
                // a field that names no type is reported, because a field read would produce nothing (§11.1)
                if (resolved.is(TypeKind::Error))
                    diagnostics.add(Diagnostic::Severity::Error, field.span, "E0420", "a struct field must name a known type");
                // a field that holds another record is reported, because the emitted struct does not nest one yet (§11.1)
                else if (holds_record(resolved, catalog))
                    diagnostics.add(Diagnostic::Severity::Error, field.span, "E0421", "a struct field may not hold another record yet");
                // the field joins the record, which is the shape a field read walks (§11.1)
                record.fields.push_back(RecordFieldType{.name = field.name, .type = resolved, .is_key = false});
            }
            // the record joins the catalog, which is what makes a property of the struct name resolve (§11.1)
            catalog.add_record(record);
        }
    }

    void register_ui_enums(const SourceFile& file, ElementCatalog& catalog) {
        // the enums are walked in declaration order, which is the order a member may be read (§11.1)
        for (const EnumDeclaration& declaration : file.enums) {
            // the set starts with its name, marked as file declared so it resolves as a property type (§11.1)
            NamedValueSet set{.name = declaration.name, .kind = TypeKind::Error, .user = true, .cpp_name = declaration.name, .members = {}};
            // each member is walked, because a member read resolves by written name against the declaration (§11.1)
            for (const std::string& member : declaration.members) {
                // a file declared enum spells its member the same in ui and in C++, so both forms match (§11.1, §11.3)
                set.members.push_back(NamedValueMember{.written = member, .code = member});
            }
            // the set joins the catalog, which is what makes a property of the enum name resolve (§11.1)
            catalog.add_named_value_set(set);
        }
    }
} // namespace albor::compiler
