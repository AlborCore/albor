#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
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
    // the checked tree shares the parser's expressions, because re-owning them would duplicate the whole ast (§11.1)
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

        // the parameter names in declaration order, which a handler body reads the raised value by (§11.1)
        std::vector<std::string> parameter_names;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one resolved function, which a binding may call as a named pure computation (§11.1)
    struct CheckedFunction
    {
        // the declared name, which a call resolves against (§11.1)
        std::string name;

        // the parameter names and their resolved types, which the arguments must match (§11.1)
        std::vector<std::pair<std::string, ValueType>> parameters;

        // the resolved return type, which a call carries as its own type (§11.1)
        ValueType return_type;

        // the returned expression, whose dependencies are empty because a function reads only its parameters (§11.1)
        SharedExpression return_value;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one resolved module-scoped value, which several components read as a named constant (§11.1)
    struct CheckedGlobal
    {
        // the declared name, which a reference resolves against (§11.1)
        std::string name;

        // the resolved declared type, which every read carries as its own (§11.1)
        ValueType type;

        // the initial value, whose dependencies are empty because a global reads only its own scope (§11.1)
        SharedExpression value;

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
        ValueType type{};

        // the component properties the value reads, which the binding registers (§7.3, AD-04a)
        std::vector<std::string> dependencies{};

        // where the assignment was written (§11.1)
        SourceSpan span{};
    };

    // one statement of a checked handler body, which is either an assignment or an invocation (§11.1)
    struct CheckedStatement
    {
        // what kind of statement this is, which decides which fields below are set (§11.1)
        HandlerStatementKind kind = HandlerStatementKind::Assignment;

        // the property being assigned, the signal invoked or the collection the method runs on (§11.1)
        std::string target;

        // the built-in method name, which is empty for an assignment, a signal invocation and a field write (§11.1)
        std::string method;

        // the field of a record the write reaches, which is empty for every other form (§11.1)
        std::string field;

        // the assigned value, which is absent for an invocation and a method call (§11.1)
        SharedExpression value;

        // the invocation or method arguments, which are empty for an assignment (§11.1)
        std::vector<SharedExpression> arguments;

        // the element type of a collection method, which is what the mutation works with (§11.1)
        ValueType element;

        // the component properties the statement reads, which the graph walk orders by (§7.3, AD-04a)
        std::vector<std::string> dependencies;

        // where the statement was written (§11.1)
        SourceSpan span;
    };

    // one drawing call inside a canvas, resolved against the primitive vocabulary (§6.4)
    struct CheckedDrawCall
    {
        // the primitive the author named, which the vocabulary decided (§6.4)
        std::string primitive;

        // the checked arguments in written order, which the primitive arity fixes (§6.4)
        std::vector<SharedExpression> arguments;

        // the component properties the call reads, which the draw body re-evaluates on (§13.3)
        std::vector<std::string> dependencies;

        // where the call was written (§11.1)
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

        // the resolved draw calls, which only a canvas carries (§6.4)
        std::vector<CheckedDrawCall> draw_calls;

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

        // the condition expression of a conditional, which decides whether the body exists (§11.1)
        SharedExpression condition;

        // the component properties the condition reads, which the graph walk orders by (§7.3, AD-04a)
        std::vector<std::string> condition_dependencies;

        // the checked body of a conditional, which is the element that exists while the condition holds (§11.1)
        std::shared_ptr<CheckedNode> conditional_body;

        // where the node was written (§11.1)
        SourceSpan span;
    };

    // one command a component declares, which an actionable child binds by name (§18, AD-15)
    struct CheckedCommand
    {
        // the name a `.ui` child writes on its `command:` property (§18)
        std::string name;

        // the one label every consumer shows, which is empty when the author wrote none (§18)
        std::string label;

        // where the declaration was written (§18)
        SourceSpan span;
    };

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

        // the resolved commands, which an actionable child binds by name (§18, AD-15)
        std::vector<CheckedCommand> commands;

        // the checked root element (§11.1)
        std::shared_ptr<CheckedNode> root;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one component an imported ui file declares, which the importing file may instantiate (§11.1)
    struct ImportedComponent
    {
        // the component name, which becomes the generated class name (§11.1, AD-03a)
        std::string name;

        // the generated header the declaration lives in, which is the file the importer includes (§11.1)
        std::string header;

        // the resolved properties, which an assignment resolves against (§11.1)
        std::vector<CheckedProperty> properties;

        // the resolved signals, which a handler resolves against (§11.1)
        std::vector<CheckedSignal> signals;

        // the imported component's checked tree, which a design time path expands into its importer (§12.1)
        std::shared_ptr<CheckedNode> root;

        // the components the imported file itself imports, which a recursive expansion follows (§11.1)
        std::vector<ImportedComponent> imported_components;

        // the file the component was declared in, which resolves a design time asset beside it (§6.4, §12.1)
        std::filesystem::path source_path;
    };

    // one fully checked source file, which is what the codegen takes (§11.1)
    struct CheckedFile
    {
        // the checked components in declaration order (§11.1)
        std::vector<CheckedComponent> components;

        // the checked functions in declaration order, which a component binding may call (§11.1)
        std::vector<CheckedFunction> functions;

        // the checked module-scoped values in declaration order, which a component expression may read (§11.1)
        std::vector<CheckedGlobal> globals;

        // the manifest modules the file imported, which name where a record header lives (§11.1, §11.2, AD-17)
        std::vector<std::string> imported_modules;

        // the components the file imported from other ui files, which an element reference resolves after its own (§11.1)
        std::vector<ImportedComponent> imported_components;
    };

    // the checker that resolves a parsed file into a typed tree, which reports rather than throws (§11.1)
    class TypeChecker
    {
    public:
        // build a checker over the element catalog it resolves types against (§11.1, §11.2, AD-17)
        TypeChecker(const ElementCatalog& catalog, DiagnosticList& diagnostics);

        // check the file in two passes, which is what makes a reference resolvable wherever it appears (§11.1)
        [[nodiscard]] CheckedFile check(const SourceFile& file);

        // publish the components imported ui files declare, which an element resolves after the file's own (§11.1)
        void set_imported_components(std::vector<ImportedComponent> components);

    private:
        // the scope a reference resolves in, which is either a component or a repeat body (§11.1)
        struct Scope
        {
            // the component properties the scope exposes, which a reference resolves against (§11.1)
            const std::vector<CheckedProperty>* properties = nullptr;

            // the repeat the scope sits inside, which contributes the item and index bindings (§11.1)
            const CheckedNode* repeat = nullptr;

            // the signal the handler body sits inside, whose parameters the body reads by name (§11.1)
            const ElementSignal* handler_signal = nullptr;

            // the function parameters the body sits inside, which shadow every property of the same name (§11.1)
            const std::vector<std::pair<std::string, ValueType>>* function_parameters = nullptr;

            // the module-scoped values an expression reads, which a component scope carries and no function sees (§11.1)
            const std::vector<CheckedGlobal>* globals = nullptr;

            // the commands the component declares, which an actionable child binds by name (§18, AD-15)
            const std::vector<CheckedCommand>* commands = nullptr;

            // whether the scope sits inside a draw block, which is what exposes the canvas extent (§6.4)
            bool in_draw = false;

            // find the type of a name the scope exposes, which answers both what a reference asks (§11.1)
            [[nodiscard]] std::optional<ValueType> lookup(std::string_view name) const;
        };

        // resolve the draw calls of a canvas against the primitive vocabulary (§6.4)
        void resolve_draw_calls(const TreeNode& node, const ElementType& type, const Scope& scope, CheckedNode& out);

        // resolve the declarations of one component, which is the first pass (§11.1)
        void resolve_declarations(const ComponentDeclaration& component, CheckedComponent& out);

        // merge the members a base declares under a derived component's own, which the scope resolves against (§11.1)
        [[nodiscard]] bool merge_inherited(const SourceFile& file, std::size_t index, std::vector<CheckedComponent>& out);

        // type the defaults a component's own declarations carry against the merged list they resolve in (§11.1)
        void type_own_defaults(const ComponentDeclaration& component, CheckedComponent& out);

        // publish the element type each file component exposes, which an instance resolves before the catalog (§11.1)
        void publish_component_types(const std::vector<CheckedComponent>& checked, const std::vector<bool>& inherited);

        // resolve every component tree with its base's checked tree in hand, which a derived body splices into (§11.1)
        void resolve_component_trees(const SourceFile& file, std::vector<CheckedComponent>& out);

        // reject a graph of component elements that instantiates itself, which would build forever (§11.1)
        void detect_instantiation_cycles(const std::vector<CheckedComponent>& checked);

        // walk one component's instantiation edges, reporting an edge that returns to the open path (§11.1)
        void visit_instantiation(const std::string& name, const std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>>& edges, std::map<std::string, int>& colour);

        // collect the component elements one checked tree instantiates, which is the edge set a cycle walk reads (§11.1)
        void collect_instantiation_edges(const CheckedNode& node, std::vector<std::pair<std::string, SourceSpan>>& targets) const;

        // resolve the signatures of every function first, so a call may appear before its declaration (§11.1)
        void check_function_signatures(const SourceFile& file, std::vector<CheckedFunction>& out);

        // resolve the body of one function against its own parameters, which is the second pass (§11.1)
        void check_function(const FunctionDeclaration& declaration, CheckedFunction& out);

        // reject a declared function that calls itself through a cycle, because the C++ would recurse (§11.1)
        void detect_function_cycles(const std::vector<CheckedFunction>& functions);

        // collect the declared functions one expression calls, which is the edge set a cycle walk reads (§11.1)
        void collect_function_calls(const Expression& expression, std::vector<std::string>& calls) const;

        // walk one function's call edges, reporting an edge that returns to the open path (§11.1)
        void visit_function(const std::string& name, const std::map<std::string, std::vector<std::pair<std::string, SourceSpan>>>& edges, std::map<std::string, int>& colour);

        // resolve the declared type of every module-scoped value first, so an initializer may name any of them (§11.1)
        void check_global_signatures(const SourceFile& file, std::vector<CheckedGlobal>& out);

        // resolve the initial value of one module-scoped value against its declared type, which is the second pass (§11.1)
        void check_global_value(const GlobalDeclaration& declaration, CheckedGlobal& out);

        // report whether the name is a module-scoped value, which is what a write to it is reported against (§11.1)
        [[nodiscard]] bool is_global(std::string_view name) const;

        // report a write to a module-scoped value, because one is read-only wherever a write appears (§11.1)
        void report_read_only(std::string_view name, SourceSpan span);

        // report a name the component declares twice, because a reference would be ambiguous (§11.1)
        void report_duplicate(std::string_view name, SourceSpan span);

        // resolve the root element, which is the second pass (§11.1)
        void resolve_tree(const ComponentDeclaration& component, CheckedComponent& out, const CheckedComponent* base);

        // resolve one tree node against the scope it appears in (§11.1)
        std::shared_ptr<CheckedNode> resolve_node(const TreeNode& node, const Scope& scope);

        // resolve the repeat form, whose body sees the item binding (§11.1)
        std::shared_ptr<CheckedNode> resolve_repeat(const TreeNode& node, const Scope& scope);

        // resolve the conditional form, whose body exists only while the condition holds (§11.1)
        std::shared_ptr<CheckedNode> resolve_conditional(const TreeNode& node, const Scope& scope);

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

        // resolve a record construction against the record the catalog declares, which is how a value is written (§11.1)
        ValueType type_record_construction(const RecordConstructionExpression& construction, const Scope& scope, std::vector<std::string>& dependencies);

        // type a field read, which resolves the object first and then the field the manifest declared (§11.2, AD-17)
        ValueType type_field_access(const FieldAccessExpression& access, const Scope& scope, std::vector<std::string>& dependencies);

        // type a reference, which resolves against the scope (§11.1)
        ValueType type_property(const PropertyExpression& reference, const Scope& scope, std::vector<std::string>& dependencies);

        // type a conversion, which the closed rule set decides (§11.1)
        ValueType type_conversion(const ConversionExpression& conversion, const Scope& scope, std::vector<std::string>& dependencies);

        // type a colour operation, which is a closed set of calls on a colour and a number (§6.1)
        ValueType type_call(const CallExpression& call, const Scope& scope, std::vector<std::string>& dependencies);

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

        // report that a written value is not a member of a declared set, which lists the members it is (§11.1)
        void report_unknown_value_member(const Expression& expression, const NamedValueSet& set);

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

        // the resolved function signatures of the file, which a call resolves against wherever it appears (§11.1)
        const std::vector<CheckedFunction>* m_functions = nullptr;

        // the module-scoped values of the file, which a component expression reads and no function body sees (§11.1)
        const std::vector<CheckedGlobal>* m_globals = nullptr;

        // the declared names of the component being checked, with the span each was written at (§11.1)
        std::vector<std::pair<std::string, SourceSpan>> m_declared;

        // the command names a checked binding used, which decides whether a declaration is unused (§18)
        std::vector<std::string> m_used_commands;

        // the index each file component sits at by name, which a base and an element both resolve against (§11.1)
        std::map<std::string, std::size_t> m_component_index;

        // the element types the file's own components expose to an instance, which resolve before the catalog (§11.1)
        std::map<std::string, ElementType> m_file_component_types;

        // the components imported from other ui files, which the checked file carries for codegen (§11.1)
        std::vector<ImportedComponent> m_imported_components;

        // the element types those imports expose, which resolve after the file's own and before the catalog (§11.1)
        std::map<std::string, ElementType> m_imported_component_types;
    };

    // register the structs a file declares, turning each into a record the catalog resolves before the check (§11.1)
    void register_ui_structs(const SourceFile& file, ElementCatalog& catalog, DiagnosticList& diagnostics);

    // register the enums a file declares, turning each into a value set the catalog resolves before the check (§11.1)
    void register_ui_enums(const SourceFile& file, ElementCatalog& catalog, DiagnosticList& diagnostics);
} // namespace albor::compiler
