#include "albor/compiler/source_index.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "albor/compiler/lexer.h"

namespace albor::compiler
{
    namespace
    {
        // the scope a reference is indexed in, which mirrors the checker's resolution order (§11.1)
        struct IndexScope
        {
            // the component the expressions belong to, which holds its properties, commands and signals (§11.1)
            const CheckedComponent* component = nullptr;

            // the module-scoped values a component expression reads, which a function body never sees (§11.1)
            const std::vector<CheckedGlobal>* globals = nullptr;

            // the function whose parameters are in scope, which is the whole scope of a body (§11.1)
            const FunctionDeclaration* function = nullptr;

            // the signal a handler body is attached to, whose parameters the body reads by name (§11.1)
            const CheckedSignal* handler_signal = nullptr;

            // the declaration the handler signal resolves to, which a parameter reference points at (§12.2)
            std::optional<SourceTarget> handler_signal_target;

            // the repeat whose item and index bindings are in scope, which a body reads by name (§11.1)
            const TreeNode* repeat = nullptr;
        };

        // build a target for a declaration in this file, which is where a definition lands (§12.2)
        std::optional<SourceTarget> target_for(const std::filesystem::path& file, const SourceSpan& span) {
            // a declaration without a name has no target, because a jump would have nowhere to land (§12.2)
            if (span.empty())
                return std::nullopt;
            // the target names the file and the exact name, which the editor turns into a location (§12.2)
            return SourceTarget{.file = file, .span = span};
        }

        // find the component a file declares, which an element and a base both resolve against (§11.1)
        const CheckedComponent* component_named(const CheckedFile& checked, std::string_view name) {
            // the declarations are scanned, because a name resolves to exactly one component (§11.1)
            for (const CheckedComponent& component : checked.components) {
                // the name decides, so the first match is the declaration (§11.1)
                if (component.name == name)
                    return &component;
            }
            // a name the file does not declare is not its component (§11.1)
            return nullptr;
        }

        // find the component an imported file declares, which an element resolves after the file's own (§11.1)
        const ImportedComponent* imported_named(const CheckedFile& checked, std::string_view name) {
            // the imported interfaces are scanned, because each names one component (§11.1)
            for (const ImportedComponent& component : checked.imported_components) {
                // the name decides, so the first match is the imported declaration (§11.1)
                if (component.name == name)
                    return &component;
            }
            // a name no import declares is not an imported component (§11.1)
            return nullptr;
        }

        // find a property of a component or an imported component, which an assignment resolves (§11.1)
        const CheckedProperty* property_named(const std::vector<CheckedProperty>& properties, std::string_view name) {
            // the declarations are scanned, because a property name resolves to one declaration (§11.1)
            for (const CheckedProperty& property : properties) {
                // the name decides, so the first match is the declaration (§11.1)
                if (property.name == name)
                    return &property;
            }
            // a name the component does not declare is not one of its properties (§11.1)
            return nullptr;
        }

        // find a signal of a component or an imported component, which a handler resolves (§11.1)
        const CheckedSignal* signal_named(const std::vector<CheckedSignal>& signals, std::string_view name) {
            // the declarations are scanned, because a signal name resolves to one declaration (§11.1)
            for (const CheckedSignal& signal : signals) {
                // the name decides, so the first match is the declaration (§11.1)
                if (signal.name == name)
                    return &signal;
            }
            // a name the component does not declare is not one of its signals (§11.1)
            return nullptr;
        }

        // find a command of a component, which a binding resolves (§18)
        const CheckedCommand* command_named(const std::vector<CheckedCommand>& commands, std::string_view name) {
            // the declarations are scanned, because a command name resolves to one declaration (§18)
            for (const CheckedCommand& command : commands) {
                // the name decides, so the first match is the declaration (§18)
                if (command.name == name)
                    return &command;
            }
            // a name the component does not declare is not one of its commands (§18)
            return nullptr;
        }

        // find a function parameter, which a function body reads by name (§11.1)
        const FunctionParameter* parameter_named(const FunctionDeclaration& function, std::string_view name) {
            // the parameters are scanned, because the body resolves each name against them (§11.1)
            for (const FunctionParameter& parameter : function.parameters) {
                // the name decides, so the first match is the parameter (§11.1)
                if (parameter.name == name)
                    return &parameter;
            }
            // a name the function does not declare is not one of its parameters (§11.1)
            return nullptr;
        }

        // resolve an element type name to the component it declares, which an element keyword jumps to (§12.2)
        std::optional<SourceTarget> component_target(const CheckedFile& checked, std::string_view name) {
            // the file's own components come first, because an element resolves against them before an import (§11.1)
            if (const CheckedComponent* component = component_named(checked, name); component != nullptr)
                return target_for({}, component->name_span);
            // an imported component follows, because its declaration lives in its own file (§11.1)
            if (const ImportedComponent* component = imported_named(checked, name); component != nullptr)
                return target_for(component->source_path, component->name_span);
            // a builtin widget is declared by the framework rather than a ui file, so no source location exists (§12.2)
            return std::nullopt;
        }

        // resolve a property name of an element type, which an assignment jumps to (§12.2)
        std::optional<SourceTarget> property_target(const CheckedFile& checked, std::string_view type, std::string_view name) {
            // the file's own components come first, because an element resolves against them before an import (§11.1)
            if (const CheckedComponent* component = component_named(checked, type); component != nullptr) {
                // the declared property is the target, because its name is where a jump lands (§12.2)
                if (const CheckedProperty* property = property_named(component->properties, name); property != nullptr)
                    return target_for({}, property->name_span);
            }
            // an imported component follows, because its declaration lives in its own file (§11.1)
            if (const ImportedComponent* component = imported_named(checked, type); component != nullptr) {
                // the imported property is the target, because its name span belongs to the imported file (§12.2)
                if (const CheckedProperty* property = property_named(component->properties, name); property != nullptr)
                    return target_for(component->source_path, property->name_span);
            }
            // a builtin property has no ui declaration, so no source location exists to jump to (§12.2)
            return std::nullopt;
        }

        // resolve a signal name of an element type, which a handler jumps to (§12.2)
        std::optional<SourceTarget> signal_target(const CheckedFile& checked, std::string_view type, std::string_view name) {
            // the file's own components come first, because an element resolves against them before an import (§11.1)
            if (const CheckedComponent* component = component_named(checked, type); component != nullptr) {
                // the declared signal is the target, because its name is where a jump lands (§12.2)
                if (const CheckedSignal* signal = signal_named(component->signals, name); signal != nullptr)
                    return target_for({}, signal->name_span);
            }
            // an imported component follows, because its declaration lives in its own file (§11.1)
            if (const ImportedComponent* component = imported_named(checked, type); component != nullptr) {
                // the imported signal is the target, because its name span belongs to the imported file (§12.2)
                if (const CheckedSignal* signal = signal_named(component->signals, name); signal != nullptr)
                    return target_for(component->source_path, signal->name_span);
            }
            // a builtin signal has no ui declaration, so no source location exists to jump to (§12.2)
            return std::nullopt;
        }

        // find the signal a handler attaches to, which its body reads parameters from (§11.1)
        const CheckedSignal* element_signal(const CheckedFile& checked, std::string_view type, std::string_view name) {
            // the file's own components come first, because an element resolves against them before an import (§11.1)
            if (const CheckedComponent* component = component_named(checked, type); component != nullptr)
                return signal_named(component->signals, name);
            // an imported component follows, because its declaration lives in its own file (§11.1)
            if (const ImportedComponent* component = imported_named(checked, type); component != nullptr)
                return signal_named(component->signals, name);
            // a builtin signal declares its parameters in the catalog, which the index does not carry (§12.2)
            return nullptr;
        }

        // resolve a binding name against the scope, which is how an expression jumps to its declaration (§12.2)
        std::optional<SourceTarget> binding_target(const IndexScope& scope, std::string_view name) {
            // the function parameters are checked first, because they are the nearest scope and shadow a property (§11.1)
            if (scope.function != nullptr) {
                // the declaration names the parameter, which is where a jump lands (§12.2)
                if (const FunctionParameter* parameter = parameter_named(*scope.function, name); parameter != nullptr)
                    return target_for({}, parameter->name_span);
            }
            // the signal parameters are read next, because a handler binds them by name (§11.1)
            if (scope.handler_signal != nullptr && scope.handler_signal_target.has_value()) {
                // each parameter is named by the declaration, so the body reads what the signal carries (§11.1)
                for (std::size_t index = 0; index < scope.handler_signal->parameter_names.size(); index++) {
                    // a different name is not this parameter, so the scan continues (§11.1)
                    if (scope.handler_signal->parameter_names.at(index) != name)
                        continue;
                    // a kept parameter span is the exact declaration a jump lands on (§12.2)
                    if (index < scope.handler_signal->parameter_name_spans.size() && !scope.handler_signal->parameter_name_spans.at(index).empty())
                        return SourceTarget{.file = scope.handler_signal_target->file, .span = scope.handler_signal->parameter_name_spans.at(index)};
                    // a parameter with no span falls back to the signal declaration (§12.2)
                    return scope.handler_signal_target;
                }
            }
            // the repeat bindings follow, because a body outside a handler reads the item and the index (§11.1)
            if (scope.repeat != nullptr) {
                // the item binding is the declaration the reference resolves to (§12.2)
                if (name == scope.repeat->repeat_item_name)
                    return target_for({}, scope.repeat->repeat_item_span);
                // the index binding is the declaration a reference to it resolves to (§12.2)
                if (!scope.repeat->repeat_index_name.empty() && name == scope.repeat->repeat_index_name)
                    return target_for({}, scope.repeat->repeat_index_span);
            }
            // the component's own declarations are scanned, because a function scope holds none (§11.1)
            if (scope.component != nullptr) {
                // the properties come first, because a binding reads one by name (§11.1)
                if (const CheckedProperty* property = property_named(scope.component->properties, name); property != nullptr)
                    return target_for({}, property->name_span);
                // a command is a component declaration too, which a child resolves by name (§18)
                if (const CheckedCommand* command = command_named(scope.component->commands, name); command != nullptr)
                    return target_for({}, command->name_span);
            }
            // the module-scoped values are consulted last, because a component's own declarations shadow them (§11.1)
            if (scope.globals != nullptr) {
                // the global declarations are scanned, because a reference resolves one by name (§11.1)
                for (const CheckedGlobal& global : *scope.globals) {
                    // the name decides, so a reference jumps to the declaration (§12.2)
                    if (global.name == name)
                        return target_for({}, global.name_span);
                }
            }
            // a name the scope does not hold resolves to no declaration, which the checker already reported (§11.1)
            return std::nullopt;
        }

        // add one written name and its target, which definition and semantic tokens both read (§12.2)
        void add_use(SourceIndex& index, std::string name, SourceUseKind kind, const SourceSpan& span, std::optional<SourceTarget> target) {
            // a use without a name span cannot be classified, because an editor needs a region (§12.2)
            if (span.empty())
                return;
            // the use joins the index in written order, which keeps a test and a debugger readable (§12.2)
            index.uses.push_back(SourceUse{.name = std::move(name), .kind = kind, .span = span, .target = std::move(target)});
        }

        // walk one expression, adding the binding references it contains (§11.1)
        void collect_expression(const Expression& expression, const IndexScope& scope, SourceIndex& index) {
            // a property reference is the only expression that names a declaration in source (§11.1)
            if (const auto* reference = dynamic_cast<const PropertyExpression*>(&expression)) {
                // the reference resolves against the scope, which is what a jump follows (§12.2)
                add_use(index, reference->name, SourceUseKind::Binding, reference->span, binding_target(scope, reference->name));
                return;
            }
            // a conversion wraps one value, which may contain a reference (§11.1)
            if (const auto* conversion = dynamic_cast<const ConversionExpression*>(&expression)) {
                // the value is walked, because a conversion may wrap any expression (§11.1)
                collect_expression(*conversion->value, scope, index);
                return;
            }
            // a call carries arguments, which may contain references (§11.1)
            if (const auto* call = dynamic_cast<const CallExpression*>(&expression)) {
                // every argument is walked, because a call reads each of them (§11.1)
                for (const ExpressionPtr& argument : call->arguments)
                    collect_expression(*argument, scope, index);
                return;
            }
            // a binary application has two operands, which are both walked (§11.1)
            if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression)) {
                // the left operand is walked first, because it is written first (§11.1)
                collect_expression(*binary->left, scope, index);
                // the right operand follows, because it is written second (§11.1)
                collect_expression(*binary->right, scope, index);
                return;
            }
            // a unary application has one operand, which is walked (§11.1)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression)) {
                // the operand is walked, because it may contain a reference (§11.1)
                collect_expression(*unary->value, scope, index);
                return;
            }
            // a grouping preserves one inner expression, which is walked (§11.1)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression)) {
                // the inner expression is walked, because parentheses change no binding (§11.1)
                collect_expression(*grouping->inner, scope, index);
                return;
            }
            // a collection literal carries its elements, which are all walked (§8.1)
            if (const auto* array = dynamic_cast<const ArrayExpression*>(&expression)) {
                // every element is walked, because each may read a binding (§8.1)
                for (const ExpressionPtr& element : array->elements)
                    collect_expression(*element, scope, index);
                return;
            }
            // a record construction carries field values, which are all walked (§11.1)
            if (const auto* construction = dynamic_cast<const RecordConstructionExpression*>(&expression)) {
                // every field value is walked, because each may read a binding (§11.1)
                for (const ConstructionField& field : construction->fields)
                    collect_expression(*field.value, scope, index);
                return;
            }
            // a field access reads through an object, which is walked (§11.2)
            if (const auto* access = dynamic_cast<const FieldAccessExpression*>(&expression)) {
                // the object is walked, because it may read a binding (§11.2)
                collect_expression(*access->object, scope, index);
                return;
            }
            // a literal, a translation and an image name no declaration, so they add nothing (§11.1)
        }

        // walk one handler statement, adding the names it writes or invokes (§11.1)
        void collect_statement(const HandlerStatement& statement, const IndexScope& scope, SourceIndex& index) {
            // an assignment writes one component property, which a jump resolves (§11.1)
            if (statement.kind == HandlerStatementKind::Assignment) {
                // the target is a binding reference, because it names a declared property (§11.1)
                add_use(index, statement.target, SourceUseKind::Binding, statement.target_span, binding_target(scope, statement.target));
                // the assigned value is walked when the parser produced one (§11.1)
                if (statement.value != nullptr)
                    collect_expression(*statement.value, scope, index);
                return;
            }
            // a signal invocation names one component signal, which a jump resolves (§11.1)
            if (statement.kind == HandlerStatementKind::SignalInvocation) {
                // the signal is the current component's declaration, because a handler may only raise its own (§11.1)
                const CheckedSignal* signal = scope.component == nullptr ? nullptr : signal_named(scope.component->signals, statement.target);
                // the target is the declaration name, which a jump lands on (§12.2)
                const std::optional<SourceTarget> target = signal == nullptr ? std::nullopt : target_for({}, signal->name_span);
                // the invocation joins the index, because a semantic token marks it as an event (§12.2)
                add_use(index, statement.target, SourceUseKind::Signal, statement.target_span, target);
                // every argument is walked, because each may read a binding (§11.1)
                for (const ExpressionPtr& argument : statement.arguments)
                    collect_expression(*argument, scope, index);
                return;
            }
            // a collection method writes one component property, which a jump resolves (§11.1)
            if (statement.kind == HandlerStatementKind::PropertyMethod) {
                // the target is a binding reference, because the collection is a declared property (§11.1)
                add_use(index, statement.target, SourceUseKind::Binding, statement.target_span, binding_target(scope, statement.target));
                // every argument is walked, because each may read a binding (§11.1)
                for (const ExpressionPtr& argument : statement.arguments)
                    collect_expression(*argument, scope, index);
                return;
            }
            // a field write names one component record property, which a jump resolves (§11.1)
            add_use(index, statement.target, SourceUseKind::Binding, statement.target_span, binding_target(scope, statement.target));
            // the assigned value is walked when the parser produced one (§11.1)
            if (statement.value != nullptr)
                collect_expression(*statement.value, scope, index);
        }

        // walk one tree node, adding the element names, assignments, handlers and references it contains (§11.1)
        void collect_tree(const TreeNode& node, const IndexScope& scope, const CheckedFile& checked, SourceIndex& index, std::vector<SourceToken>& extra_tokens) {
            // a repeat names its source and binds an item and an index, which its body reads (§11.1)
            if (node.kind == TreeNodeKind::Repeat) {
                // the source is walked, because it may read a component property (§11.1)
                if (node.repeat_source != nullptr)
                    collect_expression(*node.repeat_source, scope, index);
                // the item binding is a declaration, which a semantic token marks as a variable (§12.2)
                extra_tokens.push_back(SourceToken{.kind = SourceTokenKind::Variable, .span = node.repeat_item_span});
                // the index binding is a declaration too, when the author wrote one (§12.2)
                if (!node.repeat_index_span.empty())
                    extra_tokens.push_back(SourceToken{.kind = SourceTokenKind::Variable, .span = node.repeat_index_span});
                // the body sees the repeat bindings, because that is what it reads them by (§11.1)
                IndexScope inner = scope;
                inner.repeat = &node;
                // the body is walked, because it is instantiated once per item (§11.1)
                if (node.repeat_body != nullptr)
                    collect_tree(*node.repeat_body, inner, checked, index, extra_tokens);
                return;
            }
            // a conditional names its condition and wraps one body, which exists while it holds (§11.1)
            if (node.kind == TreeNodeKind::Conditional) {
                // the condition is walked, because it may read a component property (§11.1)
                if (node.condition != nullptr)
                    collect_expression(*node.condition, scope, index);
                // the body is walked in the same scope, because a conditional binds no name (§11.1)
                if (node.conditional_body != nullptr)
                    collect_tree(*node.conditional_body, scope, checked, index, extra_tokens);
                return;
            }
            // a children slot marks a content point, which names no declaration (§11.1)
            if (node.kind == TreeNodeKind::Slot)
                return;
            // the element type is a written name, which a jump resolves to its component declaration (§12.2)
            add_use(index, node.element_type, SourceUseKind::Element, node.element_type_span, component_target(checked, node.element_type));
            // each assignment names a property of the element type, which a jump resolves (§12.2)
            for (const PropertyAssignment& assignment : node.assignments) {
                // the property name is the use, and its target is the declared property the type carries (§12.2)
                add_use(index, assignment.name, SourceUseKind::Property, assignment.name_span, property_target(checked, node.element_type, assignment.name));
                // the assigned expression is walked when the parser produced one (§11.1)
                if (assignment.value != nullptr)
                    collect_expression(*assignment.value, scope, index);
            }
            // each handler names a signal of the element type, which a jump resolves (§12.2)
            for (const HandlerAttachment& handler : node.handlers) {
                // the written handler carries the signal name after its prefix, which the signal target resolves (§12.2)
                add_use(index, "on_" + handler.name, SourceUseKind::Handler, handler.name_span, signal_target(checked, node.element_type, handler.name));
                // the signal contributes its parameters to the body scope, which the handler reads by name (§11.1)
                const CheckedSignal* signal = element_signal(checked, node.element_type, handler.name);
                // the body scope carries the handler signal and the declaration a parameter reference jumps to (§12.2)
                IndexScope body = scope;
                body.handler_signal = signal;
                body.handler_signal_target = signal_target(checked, node.element_type, handler.name);
                // every statement is walked, because each names something the body resolves (§11.1)
                for (const HandlerStatement& statement : handler.body)
                    collect_statement(statement, body, index);
            }
            // each draw argument is walked, because a canvas body reads properties like any binding (§6.4)
            for (const DrawCall& call : node.draw_calls) {
                // every argument may contain a binding, which an editor colours and a jump follows (§6.4, §12.2)
                for (const ExpressionPtr& argument : call.arguments)
                    collect_expression(*argument, scope, index);
            }
            // each child is walked, because an element may nest any tree form (§11.1)
            for (const std::unique_ptr<TreeNode>& child : node.children)
                collect_tree(*child, scope, checked, index, extra_tokens);
        }

        // append the element symbols a tree node carries, which the document outline shows (§12.2)
        void append_tree_symbols(const TreeNode& node, std::vector<SourceSymbol>& out) {
            // an element is one symbol, which its nested elements become children of (§12.2)
            if (node.kind == TreeNodeKind::Element) {
                // the symbol carries the element type, the written region and the name region (§12.2)
                SourceSymbol symbol;
                symbol.name = node.element_type;
                symbol.kind = SourceSymbolKind::Element;
                symbol.detail = "element";
                symbol.span = node.span;
                symbol.name_span = node.element_type_span;
                // each child is added below the element, because a container nests its children (§12.2)
                for (const std::unique_ptr<TreeNode>& child : node.children)
                    append_tree_symbols(*child, symbol.children);
                // the symbol joins the outline at the parent's position (§12.2)
                out.push_back(std::move(symbol));
                return;
            }
            // a repeat shows only the element it repeats, because the binding has no outline kind (§12.2)
            if (node.kind == TreeNodeKind::Repeat) {
                // the body is appended in the repeat's place, because that is the element the author sees (§12.2)
                if (node.repeat_body != nullptr)
                    append_tree_symbols(*node.repeat_body, out);
                return;
            }
            // a conditional shows only the element it guards, because the condition names no declaration (§12.2)
            if (node.kind == TreeNodeKind::Conditional && node.conditional_body != nullptr)
                append_tree_symbols(*node.conditional_body, out);
        }

        // classify a declaration as the semantic token type the legend advertises (§12.2)
        SourceTokenKind symbol_token_kind(SourceSymbolKind kind) {
            // the declaration kind decides, because an editor colours by what the name is (§12.2)
            switch (kind) {
            case SourceSymbolKind::Component:
            case SourceSymbolKind::Element:
            case SourceSymbolKind::Struct:
            case SourceSymbolKind::Enum:
                return SourceTokenKind::Type;
            case SourceSymbolKind::Property:
                return SourceTokenKind::Property;
            case SourceSymbolKind::Signal:
                return SourceTokenKind::Event;
            case SourceSymbolKind::Command:
            case SourceSymbolKind::Function:
                return SourceTokenKind::Function;
            case SourceSymbolKind::Global:
                return SourceTokenKind::Variable;
            }
            // a kind outside the declared set is a type, because every declaration names something (§12.2)
            return SourceTokenKind::Type;
        }

        // classify a use as the semantic token type the legend advertises (§12.2)
        SourceTokenKind use_token_kind(SourceUseKind kind) {
            // the use kind decides, because an editor colours by what the name refers to (§12.2)
            switch (kind) {
            case SourceUseKind::Element:
                return SourceTokenKind::Type;
            case SourceUseKind::Property:
                return SourceTokenKind::Property;
            case SourceUseKind::Signal:
                return SourceTokenKind::Event;
            case SourceUseKind::Handler:
                return SourceTokenKind::Function;
            case SourceUseKind::Binding:
                return SourceTokenKind::Variable;
            }
            // a kind outside the declared set binds a value, because that is what an unresolved name is (§12.2)
            return SourceTokenKind::Variable;
        }

        // collect the declaration names a symbol tree carries, which the token stream classifies (§12.2)
        void collect_symbol_tokens(const SourceSymbol& symbol, std::vector<SourceToken>& tokens) {
            // the declaration name is one token, because an editor colours the name and not the whole body (§12.2)
            tokens.push_back(SourceToken{.kind = symbol_token_kind(symbol.kind), .span = symbol.name_span});
            // each child declaration is a token too, because a member name is written in source (§12.2)
            for (const SourceSymbol& child : symbol.children)
                collect_symbol_tokens(child, tokens);
        }
    } // namespace

    SourceIndex index_source(std::string_view text, const SourceFile& file, const CheckedFile& checked) {
        // the index accumulates symbols, uses and tokens as the declarations are walked (§12.2)
        SourceIndex index;
        // the extra tokens carry names a declaration walk does not produce, such as repeat bindings (§12.2)
        std::vector<SourceToken> extra_tokens;
        // every component is indexed first, because it is the unit a file compiles and an editor outlines (§11.1)
        for (const ComponentDeclaration& declaration : file.components) {
            // the symbol describes the component, which the outline shows at the top level (§12.2)
            SourceSymbol component;
            component.name = declaration.name;
            component.kind = SourceSymbolKind::Component;
            component.detail = "component";
            component.span = declaration.span;
            component.name_span = declaration.name_span;
            // each property is a child symbol, because a component declares its surface (§12.2)
            for (const PropertyDeclaration& property : declaration.properties) {
                // the symbol names the property and shows its declared type as the detail (§12.2)
                SourceSymbol symbol;
                symbol.name = property.name;
                symbol.kind = SourceSymbolKind::Property;
                symbol.detail = property.type;
                symbol.span = property.span;
                symbol.name_span = property.name_span;
                // the property joins the component's outline, because a member belongs to its type (§12.2)
                component.children.push_back(std::move(symbol));
            }
            // each signal is a child symbol, because a handler resolves it on the component (§12.2)
            for (const SignalDeclaration& signal : declaration.signals) {
                // the symbol names the signal and says what it is, because its type is not a written property (§12.2)
                SourceSymbol symbol;
                symbol.name = signal.name;
                symbol.kind = SourceSymbolKind::Signal;
                symbol.detail = "signal";
                symbol.span = signal.span;
                symbol.name_span = signal.name_span;
                // the signal joins the component's outline, because a member belongs to its type (§12.2)
                component.children.push_back(std::move(symbol));
                // each parameter is a declaration name, which a handler body reads as a variable (§12.2)
                for (const SourceSpan& span : signal.parameter_name_spans) {
                    // a parameter with no span cannot be painted, so it carries no token (§12.2)
                    if (!span.empty())
                        extra_tokens.push_back(SourceToken{.kind = SourceTokenKind::Variable, .span = span});
                }
            }
            // each command is a child symbol, because an actionable child binds it by name (§18)
            for (const CommandDeclaration& command : declaration.commands) {
                // the symbol names the command and shows its label, which is what every consumer displays (§12.2, §18)
                SourceSymbol symbol;
                symbol.name = command.name;
                symbol.kind = SourceSymbolKind::Command;
                symbol.detail = command.label;
                symbol.span = command.span;
                symbol.name_span = command.name_span;
                // the command joins the component's outline, because a member belongs to its type (§12.2)
                component.children.push_back(std::move(symbol));
            }
            // the root element is the first tree symbol, because it is the component's body (§11.1)
            if (declaration.root != nullptr)
                append_tree_symbols(*declaration.root, component.children);
            // the content elements follow, because a derived component writes them in the same body (§11.1)
            for (const std::unique_ptr<TreeNode>& content : declaration.content)
                append_tree_symbols(*content, component.children);
            // the component joins the outline before its references, because both read one declaration (§12.2)
            index.symbols.push_back(std::move(component));
            // the checked component supplies the resolved declarations a reference resolves against (§11.1)
            const CheckedComponent* resolved = component_named(checked, declaration.name);
            // the component scope carries its own members and the file's globals, which a default may read (§11.1)
            IndexScope scope;
            scope.component = resolved;
            scope.globals = &checked.globals;
            // a property default is a binding, so its references are walked with the component scope (§11.1)
            for (const PropertyDeclaration& property : declaration.properties) {
                // a property may declare no default, which leaves nothing to walk (§11.1)
                if (property.default_value != nullptr)
                    collect_expression(*property.default_value, scope, index);
            }
            // the root is walked, because its elements and assignments name declarations (§11.1)
            if (declaration.root != nullptr)
                collect_tree(*declaration.root, scope, checked, index, extra_tokens);
            // the content is walked in the same scope, because a derived body sees its component's members (§11.1)
            for (const std::unique_ptr<TreeNode>& content : declaration.content)
                collect_tree(*content, scope, checked, index, extra_tokens);
        }
        // every function is indexed next, because a binding may call it and its body reads parameters (§11.1)
        for (const FunctionDeclaration& declaration : file.functions) {
            // the symbol names the function and shows its return type as the detail (§12.2)
            SourceSymbol symbol;
            symbol.name = declaration.name;
            symbol.kind = SourceSymbolKind::Function;
            symbol.detail = declaration.return_type;
            symbol.span = declaration.span;
            symbol.name_span = declaration.name_span;
            // the function joins the top level, because it belongs to the file rather than a component (§12.2)
            index.symbols.push_back(std::move(symbol));
            // each parameter is a declaration name, which a semantic token marks as a variable (§12.2)
            for (const FunctionParameter& parameter : declaration.parameters)
                extra_tokens.push_back(SourceToken{.kind = SourceTokenKind::Variable, .span = parameter.name_span});
            // the body scope is the parameters alone, because a function reads only its arguments (§11.1)
            IndexScope scope;
            scope.function = &declaration;
            // the returned expression is walked, because it may read a parameter by name (§11.1)
            if (declaration.return_value != nullptr)
                collect_expression(*declaration.return_value, scope, index);
        }
        // every module-scoped value is indexed, because a component expression may read it (§11.1)
        for (const GlobalDeclaration& declaration : file.globals) {
            // the symbol names the global and shows its declared type as the detail (§12.2)
            SourceSymbol symbol;
            symbol.name = declaration.name;
            symbol.kind = SourceSymbolKind::Global;
            symbol.detail = declaration.type;
            symbol.span = declaration.span;
            symbol.name_span = declaration.name_span;
            // the global joins the top level, because it belongs to the file rather than a component (§12.2)
            index.symbols.push_back(std::move(symbol));
            // the initializer scope is empty, because a global reads neither a property nor another global (§11.1)
            IndexScope scope;
            // the initializer is walked, because it may name a declaration the checker reports (§11.1)
            if (declaration.value != nullptr)
                collect_expression(*declaration.value, scope, index);
        }
        // every struct is indexed, because a property or a signal may carry it (§11.1)
        for (const StructDeclaration& declaration : file.structs) {
            // the symbol names the struct, which is what a record type is written as (§12.2)
            SourceSymbol symbol;
            symbol.name = declaration.name;
            symbol.kind = SourceSymbolKind::Struct;
            symbol.span = declaration.span;
            symbol.name_span = declaration.name_span;
            // the struct joins the top level, because it belongs to the file rather than a component (§12.2)
            index.symbols.push_back(std::move(symbol));
        }
        // every enum is indexed, because a property may hold one of its members (§11.1)
        for (const EnumDeclaration& declaration : file.enums) {
            // the symbol names the enum, which is what a value set is written as (§12.2)
            SourceSymbol symbol;
            symbol.name = declaration.name;
            symbol.kind = SourceSymbolKind::Enum;
            symbol.span = declaration.span;
            symbol.name_span = declaration.name_span;
            // the enum joins the top level, because it belongs to the file rather than a component (§12.2)
            index.symbols.push_back(std::move(symbol));
        }
        // the top level outline follows source order, because a reader expects the file as it is written (§12.2)
        std::sort(index.symbols.begin(), index.symbols.end(), [](const SourceSymbol& lhs, const SourceSymbol& rhs) {
            // the line decides first, because the outline is read top to bottom (§12.2)
            if (lhs.span.start.line != rhs.span.start.line)
                return lhs.span.start.line < rhs.span.start.line;
            // the column follows, because two declarations on one line keep their written order (§12.2)
            return lhs.span.start.column < rhs.span.start.column;
        });
        // the tokens start with every declaration name, because an editor colours what is declared (§12.2)
        for (const SourceSymbol& symbol : index.symbols)
            collect_symbol_tokens(symbol, index.tokens);
        // every use is a token too, because a reference is written in source (§12.2)
        for (const SourceUse& use : index.uses)
            index.tokens.push_back(SourceToken{.kind = use_token_kind(use.kind), .span = use.span});
        // the extra names follow, because a repeat or parameter declaration is a token without a symbol (§12.2)
        index.tokens.insert(index.tokens.end(), extra_tokens.begin(), extra_tokens.end());
        // the keywords are scanned from the text, because the parser consumes their units without keeping them (§11.1)
        DiagnosticList diagnostics;
        // the scanner runs over the same text, because a keyword unit is spelled like an identifier (§11.1)
        for (const Token& token : Lexer(text, diagnostics).tokenize()) {
            // only an identifier can spell a keyword, because a literal never matches (§11.1)
            if (token.kind != Token::Kind::Identifier)
                continue;
            // a language keyword is one token, because the legend marks it separately from a name (§12.2)
            if (is_language_keyword(token.text))
                index.tokens.push_back(SourceToken{.kind = SourceTokenKind::Keyword, .span = token.span});
        }
        // the tokens are sorted, because the protocol encodes a token as a delta from the previous one (§12.2)
        std::sort(index.tokens.begin(), index.tokens.end(), [](const SourceToken& lhs, const SourceToken& rhs) {
            // the line decides first, because a token stream is ordered top to bottom (§12.2)
            if (lhs.span.start.line != rhs.span.start.line)
                return lhs.span.start.line < rhs.span.start.line;
            // the column follows, because a token stream is ordered left to right (§12.2)
            if (lhs.span.start.column != rhs.span.start.column)
                return lhs.span.start.column < rhs.span.start.column;
            // the end breaks a tie, because a nested name starts where its container does (§12.2)
            return lhs.span.end.column < rhs.span.end.column;
        });
        // the duplicate and overlapping tokens are dropped, because an editor cannot paint one region twice (§12.2)
        std::vector<SourceToken> unique;
        for (const SourceToken& token : index.tokens) {
            // a token that spans lines or covers nothing cannot be encoded, because the protocol counts one line (§12.2)
            if (token.span.empty() || token.span.start.line != token.span.end.line)
                continue;
            // an exact duplicate is the same region, which one token already covers (§12.2)
            if (!unique.empty() && unique.back().span.start == token.span.start && unique.back().span.end == token.span.end)
                continue;
            // an overlapping token would paint over its predecessor, so the first classification wins (§12.2)
            if (!unique.empty() && unique.back().span.start.line == token.span.start.line && token.span.start.column < unique.back().span.end.column)
                continue;
            // the token joins the stream, because it covers a region no earlier token covers (§12.2)
            unique.push_back(token);
        }
        // the sorted stream replaces the collected one, which is what the reply encodes (§12.2)
        index.tokens = std::move(unique);
        return index;
    }
} // namespace albor::compiler
