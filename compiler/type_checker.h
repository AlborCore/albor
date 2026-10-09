#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compiler/ast.h"
#include "compiler/diagnostic.h"
#include "compiler/parser.h"
#include "compiler/value_type.h"

namespace albor::compiler
{
    // the checked tree shares the expressions the parser built, because re-owning them would duplicate the whole ast (§11.1)
    using SharedExpression = std::shared_ptr<const Expression>;

    // one property a component declares, resolved to the type it was written with (§11.1)
    struct CheckedProperty
    {
        // the declared name, which a binding and a handler resolve against (§11.1)
        std::string name;

        // the type the author wrote, which an assigned value must match (§11.1)
        ValueType type;

        // the direction, which decides whether a handler may assign it (§11.1, §7)
        PropertyDirection direction = PropertyDirection::In;

        // whether the author gave a default, because a property may have none (§11.1)
        bool has_default = false;

        // the default value as typed, which the facade initialises with (§11.1)
        SharedExpression default_value;

        // the component properties the default reads, which the engine registers (§7.3, AD-04a)
        std::vector<std::string> default_dependencies;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one signal a component declares, resolved to its parameter types (§11.1)
    struct CheckedSignal
    {
        // the declared name, which a handler resolves against (§11.1)
        std::string name;

        // the parameter types in declaration order, which an invocation must match (§11.1)
        std::vector<ValueType> parameter_types;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one assignment on an element, resolved against the element type and typed (§11.1)
    struct CheckedAssignment
    {
        // the property name as written (§11.1)
        std::string name;

        // the assigned value, whose type was established (§11.1)
        SharedExpression value;

        // the type the value carries, which the property accepted (§11.1)
        ValueType type;

        // whether the property scales with the surface density, which the codegen applies (§11.1, AD-27)
        bool scaled_by_density = false;

        // the component properties the value reads, which the binding registers (§7.3, AD-04a)
        std::vector<std::string> dependencies;

        // where the assignment was written (§11.1)
        SourceSpan span;
    };

    // one statement of a checked handler body, which is either an assignment or an invocation (§11.1)
    struct CheckedStatement
    {
        // what kind of statement this is, which decides which fields below are set (§11.1)
        HandlerStatementKind kind = HandlerStatementKind::Assignment;

        // the property being assigned or the signal being invoked (§11.1)
        std::string target;

        // the assigned value, which is absent for an invocation (§11.1)
        SharedExpression value;

        // the invocation arguments, which are empty for an assignment (§11.1)
        std::vector<SharedExpression> arguments;

        // the component properties the statement reads, which the graph walk orders by (§7.3, AD-04a)
        std::vector<std::string> dependencies;

        // where the statement was written (§11.1)
        SourceSpan span;
    };

    // one handler on an element, resolved against the element type (§11.1)
    struct CheckedHandler
    {
        // the signal name the handler attached to (§11.1)
        std::string name;

        // the body statements, each already checked against its target (§11.1)
        std::vector<CheckedStatement> body;

        // the component properties the body assigns, which the invalidation index records (§7.3, AD-04a)
        std::vector<std::string> writes;

        // where the handler was written (§11.1)
        SourceSpan span;
    };

    // one checked tree node, which is either an element or a repeat (§11.1)
    struct CheckedNode
    {
        // what kind of node this is, which decides which fields below are set (§11.1)
        TreeNodeKind kind = TreeNodeKind::Element;

        // the element type name as written, which the catalog resolved (§11.1)
        std::string element_type;

        // the author assigned identity, which is empty when the author wrote none (§18.1, AD-11)
        std::string explicit_id;

        // the resolved assignments (§11.1)
        std::vector<CheckedAssignment> assignments;

        // the resolved handlers (§11.1)
        std::vector<CheckedHandler> handlers;

        // the checked children, which a container accepts (§11.1, §8)
        std::vector<std::shared_ptr<CheckedNode>> children;

        // the repeated source expression, whose type is the element type of the source (§11.1)
        SharedExpression repeat_source;

        // the type of the repeated source, which the item binding carries (§11.1)
        ValueType repeat_source_type;

        // the name the repeated item is bound to (§11.1)
        std::string repeat_item_name;

        // the name the repeated index is bound to, which is empty when the author wrote none (§11.1)
        std::string repeat_index_name;

        // the checked body of a repeat, which is the element instantiated per item (§11.1)
        std::shared_ptr<CheckedNode> repeat_body;

        // the component properties the repeated source reads, which the graph walk orders by (§7.3, AD-04a)
        std::vector<std::string> repeat_dependencies;

        // where the node was written (§11.1)
        SourceSpan span;
    };

    // one component whose declarations and tree have been resolved and typed (§11.1)
    struct CheckedComponent
    {
        // the component name, which becomes the generated class name (§11.1, AD-03a)
        std::string name;

        // the modules the component imported (§11.1, AD-17)
        std::vector<ImportDeclaration> imports;

        // the resolved properties, which is what a binding resolves against (§11.1)
        std::vector<CheckedProperty> properties;

        // the resolved signals, which is what a handler resolves against (§11.1)
        std::vector<CheckedSignal> signals;

        // the checked root element (§11.1)
        std::shared_ptr<CheckedNode> root;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one fully checked source file, which is what the codegen takes (§11.1)
    struct CheckedFile
    {
        // the checked components in declaration order (§11.1)
        std::vector<CheckedComponent> components;
    };

    // the checker that resolves a parsed file into a typed tree, which reports rather than throws (§11.1)
    class TypeChecker
    {
    public:
        // build a checker over the element catalog it resolves types against (§11.1, §11.2, AD-17)
        TypeChecker(const ElementCatalog& catalog, DiagnosticList& diagnostics);

        // check the file in two passes, which is what makes a reference resolvable wherever it appears (§11.1)
        [[nodiscard]] CheckedFile check(const SourceFile& file);

    private:
        // the scope a reference resolves in, which is either a component or a repeat body (§11.1)
        struct Scope
        {
            // the component properties the scope exposes, which a reference resolves against (§11.1)
            const std::vector<CheckedProperty>* properties = nullptr;

            // the repeat the scope sits inside, which contributes the item and index bindings (§11.1)
            const CheckedNode* repeat = nullptr;

            // find the type of a name the scope exposes, which answers both what a reference asks (§11.1)
            [[nodiscard]] std::optional<ValueType> lookup(std::string_view name) const;
        };

        // resolve the declarations of one component, which is the first pass (§11.1)
        void resolve_declarations(const ComponentDeclaration& component, CheckedComponent& out);

        // report a name the component declares twice, because a reference would be ambiguous (§11.1)
        void report_duplicate(std::string_view name, SourceSpan span);

        // resolve the root element, which is the second pass (§11.1)
        void resolve_tree(const ComponentDeclaration& component, CheckedComponent& out);

        // resolve one tree node against the scope it appears in (§11.1)
        std::shared_ptr<CheckedNode> resolve_node(const TreeNode& node, const Scope& scope);

        // resolve the repeat form, whose body sees the item binding (§11.1)
        std::shared_ptr<CheckedNode> resolve_repeat(const TreeNode& node, const Scope& scope);

        // resolve the assignments of an element against its type (§11.1)
        void resolve_assignments(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out);

        // resolve one assignment against the property it names (§11.1)
        void resolve_assignment(const PropertyAssignment& assignment, const ElementType& type, const Scope& scope, CheckedNode& out);

        // resolve the handlers of an element against its type (§11.1)
        void resolve_handlers(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out);

        // resolve one handler body against the component it belongs to (§11.1)
        void resolve_handler(const HandlerAttachment& handler, const ElementType& type, const Scope& scope, CheckedNode& out);

        // resolve one handler statement against what it names (§11.1)
        void resolve_statement(const HandlerStatement& statement, const Scope& scope, CheckedHandler& out);

        // resolve the children of an element, which only a container accepts (§11.1, §8)
        void resolve_children(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out);

        // type one expression bottom up, which is the second half of the second pass (§11.1)
        ValueType type_expression(const Expression& expression, const Scope& scope, std::vector<std::string>& dependencies);

        // type a literal, which the kind of the literal decides (§11.1)
        ValueType type_literal(const LiteralExpression& literal);

        // type a bracketed list, whose elements must agree because a collection has one element type (§8.1)
        ValueType type_array(const ArrayExpression& array, const Scope& scope, std::vector<std::string>& dependencies);

        // type a reference, which resolves against the scope (§11.1)
        ValueType type_property(const PropertyExpression& reference, const Scope& scope, std::vector<std::string>& dependencies);

        // type a conversion, which the closed rule set decides (§11.1)
        ValueType type_conversion(const ConversionExpression& conversion, const Scope& scope, std::vector<std::string>& dependencies);

        // type a binary application, which the operator rule decides (§11.1)
        ValueType type_binary(const BinaryExpression& binary, const Scope& scope, std::vector<std::string>& dependencies);

        // type a unary application, which the operator rule decides (§11.1)
        ValueType type_unary(const UnaryExpression& unary, const Scope& scope, std::vector<std::string>& dependencies);

        // report that a written type does not resolve, which points at the declaration (§11.1, §11.2, AD-17)
        void report_unknown_type(std::string_view written, SourceSpan span);

        // report that a declared default does not match its own type, which points at the declaration (§11.1)
        void report_default_mismatch(const PropertyDeclaration& declaration, const ValueType& from, const ValueType& to);

        // report that a name is not in scope, which points at the reference (§11.1)
        void report_unknown_name(const Expression& expression, std::string_view name);

        // report that a value cannot fill a target type, which points at the value (§11.1)
        void report_type_mismatch(const Expression& expression, const ValueType& from, const ValueType& to);

        // report that a name does not exist on an element type (§11.1)
        void report_unknown_property(SourceSpan span, std::string_view element, std::string_view property_name);

        // report that a signal does not exist on an element type (§11.1)
        void report_unknown_signal(SourceSpan span, std::string_view element, std::string_view signal_name);

        // report that a handler may not assign a property, which points at the statement (§11.1, §7)
        void report_not_assignable(const HandlerStatement& statement, std::string_view name);

        // report a static dependency cycle, which the eager push graph could not order (§7.3, AD-04a)
        void report_cycle(std::string_view reader, const std::vector<std::string>& path);

        // collect the static dependency edges of the whole component, then reject any cycle (§7.3, AD-04a)
        void detect_cycles(const CheckedComponent& component);

        // collect the edges one checked node and its bindings create (§7.3, AD-04a)
        void collect_edges(const CheckedNode& node, std::vector<std::pair<std::string, std::vector<std::string>>>& edges) const;

        // record that a reader depends on a writer, which is what an edge means (§7.3, AD-04a)
        static void add_edge(std::vector<std::pair<std::string, std::vector<std::string>>>& edges, std::string_view reader, std::string_view writer);

        // walk the edges from a property, which is how a cycle is found (§7.3, AD-04a)
        void visit(const std::vector<std::pair<std::string, std::vector<std::string>>>& edges, const std::string& start, std::vector<std::string>& path, std::vector<std::string>& finished);

        // the element types a node may instantiate, which the catalog resolved (§11.1, §11.2, AD-17)
        const ElementCatalog& m_catalog;

        // the problems found while checking, which the checker appends to (§11.1)
        DiagnosticList& m_diagnostics;

        // the properties of the component being checked, which a handler resolves against (§11.1)
        const std::vector<CheckedProperty>* m_properties = nullptr;

        // the signals of the component being checked, which a handler resolves against (§11.1)
        const std::vector<CheckedSignal>* m_signals = nullptr;

        // the declared names of the component being checked, with the span each was written at (§11.1)
        std::vector<std::pair<std::string, SourceSpan>> m_declared;
    };
} // namespace albor::compiler
