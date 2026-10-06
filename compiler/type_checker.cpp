#include "compiler/type_checker.h"

#include <algorithm>
#include <utility>

namespace albor::compiler
{
    namespace
    {
        // the wrappers that make a snapshot and a vector read as the author wrote them (§10.5, §13.2, §8.1)
        constexpr std::string_view SNAPSHOT_OPEN = "Snapshot<";
        constexpr std::string_view SNAPSHOT_CLOSE = ">";
        constexpr std::string_view VECTOR_OPEN = "vector<";
        constexpr std::string_view VECTOR_CLOSE = ">";

        // a parameter form is the wrapper, the element and the closing angle, so this is the shortest it can be (§11.1)
        constexpr std::size_t SHORTEST_PARAMETRIC = 10;

        // resolve a type name as written, against the catalog that holds the native types (§11.1, §11.2, AD-17)
        ValueType resolve_type_name(std::string_view written, const ElementCatalog& catalog) {
            // a scalar name is one the language spells without brackets (§11.1)
            if (written == "bool")
                return scalar(TypeKind::Bool);
            // an integer is spelled int (§11.1)
            if (written == "int")
                return scalar(TypeKind::Int);
            // a floating point number is spelled float (§11.1)
            if (written == "float")
                return scalar(TypeKind::Float);
            // a text value is spelled string (§11.1)
            if (written == "string")
                return scalar(TypeKind::String);
            // a colour is spelled color (§11.1)
            if (written == "color")
                return scalar(TypeKind::Color);
            // a length is spelled length, so a declared length and a literal length agree (§11.1, AD-27)
            if (written == "length")
                return scalar(TypeKind::Length);
            // a snapshot is written Snapshot<T>, whose element may itself nest (§10.5, §13.2)
            if (written.substr(0, SNAPSHOT_OPEN.size()) == SNAPSHOT_OPEN && written.size() >= SHORTEST_PARAMETRIC && written.substr(written.size() - SNAPSHOT_CLOSE.size()) == SNAPSHOT_CLOSE) {
                // the element is the part between the brackets, which resolves the same way (§10.5, §13.2)
                const std::string_view inner = written.substr(SNAPSHOT_OPEN.size(), written.size() - SNAPSHOT_OPEN.size() - SNAPSHOT_CLOSE.size());
                // the wrapper is rebuilt rather than trusted, so a malformed element still resolves (§10.5, §13.2)
                return ValueType::parameterized(TypeKind::Snapshot, resolve_type_name(inner, catalog));
            }
            // a vector is written vector<T>, whose element may itself nest (§8.1)
            if (written.substr(0, VECTOR_OPEN.size()) == VECTOR_OPEN && written.size() >= SHORTEST_PARAMETRIC && written.substr(written.size() - VECTOR_CLOSE.size()) == VECTOR_CLOSE) {
                // the element is the part between the brackets, which resolves the same way (§8.1)
                const std::string_view inner = written.substr(VECTOR_OPEN.size(), written.size() - VECTOR_OPEN.size() - VECTOR_CLOSE.size());
                // the wrapper is rebuilt rather than trusted, so a malformed element still resolves (§8.1)
                return ValueType::parameterized(TypeKind::Vector, resolve_type_name(inner, catalog));
            }
            // anything else is a native type, which the catalog holds only if a manifest declared it (§11.2, AD-17)
            if (catalog.find(written) != nullptr || catalog.holds_native(written))
                return ValueType::native(std::string(written));
            // an unknown name resolves to the error type, so nothing downstream raises a second complaint (§11.1)
            return error_type();
        }
    } // namespace

    std::optional<ValueType> TypeChecker::Scope::lookup(std::string_view name) const {
        // the repeat bindings are checked first, because they are the nearest scope (§11.1)
        if (repeat != nullptr) {
            // the item binding carries the element type, which a vector source unwraps for its body (§11.1)
            if (name == repeat->repeat_item_name)
                return repeat->repeat_source_type.is(TypeKind::Vector) ? repeat->repeat_source_type.element() : repeat->repeat_source_type;
            // the index binding is always an integer, because it counts (§11.1)
            if (name == repeat->repeat_index_name)
                return scalar(TypeKind::Int);
        }
        // the declarations are scanned, because every scope is built from a resolved component (§11.1)
        for (const CheckedProperty& property : *properties) {
            // the name decides, so a reference resolves to exactly one declaration (§11.1)
            if (property.name == name)
                return property.type;
        }
        // a name the scope does not hold was already reported, so nothing is returned (§11.1)
        return std::nullopt;
    }

    TypeChecker::TypeChecker(const ElementCatalog& catalog, DiagnosticList& diagnostics) :
        m_catalog(catalog), m_diagnostics(diagnostics) {}

    CheckedFile TypeChecker::check(const SourceFile& file) {
        // the file is assembled as the components are checked, so one mistake costs one component (§11.1)
        CheckedFile checked;
        // the components are checked in declaration order, because the result preserves it (§11.1)
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
            // the declarations are published, because the second pass resolves handlers against them (§11.1)
            m_properties = &out.properties;
            // the signals are published, because the second pass resolves invocations against them (§11.1)
            m_signals = &out.signals;
            // the second pass resolves the tree against what the first pass found (§11.1)
            resolve_tree(component, out);
            // the static graph is walked once the tree is typed, because it is built from the bindings (§7.3, AD-04a)
            detect_cycles(out);
            // the component is kept, because a diagnostic already blocks codegen (§11.1)
            checked.components.push_back(std::move(out));
        }
        // the pass state is cleared, because the checker may be reused for another file (§11.1)
        m_properties = nullptr;
        // the signals are cleared with it, because they pointed into the component that just ended (§11.1)
        m_signals = nullptr;
        return checked;
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
            // the written type resolves against the catalog, which is where a native type comes from (§11.1, §11.2, AD-17)
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
        // the defaults are typed second, because a default may read a property declared after it (§11.1)
        for (std::size_t index = 0; index < component.properties.size(); index++) {
            // a property with no default has nothing to type (§11.1)
            if (!out.properties.at(index).has_default)
                continue;
            // the scope is the whole declaration list, because the default may read any of it (§11.1)
            const Scope scope{.properties = &out.properties, .repeat = nullptr};
            // the dependencies come from the same walk, because a default is a binding (§7.3, AD-04a)
            const ValueType value_type = type_expression(*out.properties.at(index).default_value, scope, out.properties.at(index).default_dependencies);
            // the value must match the property, because nothing widens implicitly (§11.1)
            if (!assignable(value_type, out.properties.at(index).type)) {
                // the mismatch points at the declaration, because that is where both types are written (§11.1)
                report_default_mismatch(component.properties.at(index), value_type, out.properties.at(index).type);
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
            for (const std::string& written : declaration.parameter_types) {
                // the type resolves against the catalog, which is where a native type comes from (§11.1, §11.2, AD-17)
                const ValueType resolved = resolve_type_name(written, m_catalog);
                // a type that did not resolve is reported here, because nothing downstream can (§11.1)
                if (resolved.is_error())
                    report_unknown_type(written, declaration.span);
                // the parameter is kept in declaration order, because the facade signature follows it (§11.1)
                signal.parameter_types.push_back(resolved);
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

    void TypeChecker::resolve_tree(const ComponentDeclaration& component, CheckedComponent& out) {
        // a component with no root has nothing to resolve, which is a declaration defect rather than a tree one (§11.1)
        if (component.root == nullptr) {
            // the missing root is reported, because a component must build something (§11.1)
            m_diagnostics.add(Diagnostic::Severity::Error, component.span, "E0402", "a component must declare a root element");
            return;
        }
        // the scope a reference resolves in is the component's own declarations (§11.1)
        const Scope scope{.properties = &out.properties, .repeat = nullptr};
        // the root is checked against that scope, which is the second pass (§11.1)
        out.root = resolve_node(*component.root, scope);
    }

    std::shared_ptr<CheckedNode> TypeChecker::resolve_node(const TreeNode& node, const Scope& scope) {
        // a repeat is checked by its own rule, because its body sees an extra binding (§11.1)
        if (node.kind == TreeNodeKind::Repeat)
            return resolve_repeat(node, scope);
        // the node is allocated up front, because the caller takes a shared pointer (§11.1)
        auto out = std::make_shared<CheckedNode>();
        // the kind, the name, the identity and the span come straight from the node (§11.1, AD-11)
        out->kind = node.kind;
        out->element_type = node.element_type;
        out->explicit_id = node.explicit_id;
        out->span = node.span;
        // an element resolves against the catalog, which is where its properties come from (§11.1, §11.2, AD-17)
        const ElementType* type = m_catalog.find(node.element_type);
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
            const Scope inner{.properties = scope.properties, .repeat = out.get()};
            // the body is checked against that scope, which is what makes the item resolvable (§11.1)
            out->repeat_body = resolve_node(*node.repeat_body, inner);
        }
        return out;
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
        // the density flag comes from the declaration, so the codegen knows where the scale belongs (§11.1, AD-27)
        out_assignment.scaled_by_density = property->scaled_by_density;
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
        if (type.find_signal(handler.name) == nullptr) {
            // the problem names both sides, because the author needs to know what the type does raise (§11.1)
            report_unknown_signal(handler.span, type.name, handler.name);
        }
        // the body resolves against the component, because a handler reads and writes the component (§11.1)
        for (const HandlerStatement& statement : handler.body)
            resolve_statement(statement, scope, out_handler);
        // the handler is kept, because the codegen wires a callback to it (§11.1)
        out.handlers.push_back(std::move(out_handler));
    }

    void TypeChecker::resolve_statement(const HandlerStatement& statement, const Scope& scope, CheckedHandler& out) {
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
                // the problem points at the statement, because that is where the target is written (§11.1)
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
        else {
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
        // the statement is kept, because the codegen emits it as checked (§11.1)
        out.body.push_back(std::move(out_statement));
    }

    void TypeChecker::resolve_children(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out) {
        // a leaf takes no children, so one written under it is reported rather than dropped (§11.1, §8)
        if (!node.children.empty() && !type.accepts_children) {
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
        // a reference resolves against the scope (§11.1)
        if (const auto* reference = dynamic_cast<const PropertyExpression*>(&expression))
            return type_property(*reference, scope, dependencies);
        // a conversion resolves against the closed rule set (§11.1)
        if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression))
            return type_conversion(*conversion, scope, dependencies);
        // a binary application types both operands and then applies the operator rule (§11.1)
        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
            return type_binary(*binary, scope, dependencies);
        // a unary application types its operand and then applies the operator rule (§11.1)
        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
            return type_unary(*unary, scope, dependencies);
        // a grouping contributes nothing, so its inner expression types the whole (§11.1)
        if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
            return type_expression(*grouping->inner, scope, dependencies);
        // a form with no rule is reported, because typing it as nothing would let it through unchecked (§11.1)
        m_diagnostics.add(Diagnostic::Severity::Error, expression.location(), "E0415", "this expression form has no type rule");
        // the error type stands in, so nothing downstream raises a second complaint (§11.1)
        return error_type();
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
        // the declarations are scanned, because only they are component properties (§7.3, AD-04a)
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
} // namespace albor::compiler
