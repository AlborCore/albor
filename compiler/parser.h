#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "compiler/ast.h"
#include "compiler/diagnostic.h"
#include "compiler/lexer.h"

namespace albor::compiler
{
    // one parsed source file, which holds its imports and its component declarations (§11.1)
    struct SourceFile
    {
        // the modules the file refers to, which are resolved against the manifest set (§11.1, AD-17)
        std::vector<ImportDeclaration> imports;

        // the structs the file declares at its top level, which the registration pass turns into records (§11.1)
        std::vector<StructDeclaration> structs;

        // the enums the file declares at its top level, which the registration pass turns into value sets (§11.1)
        std::vector<EnumDeclaration> enums;

        // the functions the file declares at its top level, which a binding may call (§11.1)
        std::vector<FunctionDeclaration> functions;

        // the module-scoped values the file declares at its top level, which several components read (§11.1)
        std::vector<GlobalDeclaration> globals;

        // the components the file declares, which is what the checker and the codegen take (§11.1)
        std::vector<ComponentDeclaration> components;
    };

    // the parser that turns a token stream into a source file, which reports rather than throws (§11.1)
    class Parser
    {
    public:
        // build a parser over a scanned token stream, which the caller keeps alive for the parse (§11.1)
        Parser(const std::vector<Token>& tokens, DiagnosticList& diagnostics);

        // parse the whole stream, recovering at every declaration boundary so one mistake costs one declaration (§11.1)
        [[nodiscard]] SourceFile parse();

    private:
        // hand out the unit at the given offset from the current one, which is how lookahead stays cheap (§11.1)
        [[nodiscard]] const Token& peek(std::size_t offset = 0) const;

        // hand out where the construct the reader just finished ends, which is what a span end needs (§11.1)
        [[nodiscard]] const SourceLocation& last_consumed_end() const { return m_last_consumed_end; }

        // report whether the current unit is the given keyword (§11.1)
        [[nodiscard]] bool at_keyword(std::string_view keyword) const;

        // report whether the unit at the given offset is the given punctuation (§11.1)
        [[nodiscard]] bool at_punctuation(std::string_view symbol, std::size_t offset = 0) const;

        // report whether the current unit is the given keyword, consuming it when it is (§11.1)
        [[nodiscard]] bool accept_keyword(std::string_view keyword);

        // report whether the current unit is the given punctuation, consuming it when it is (§11.1)
        [[nodiscard]] bool accept_punctuation(std::string_view symbol);

        // consume the unit at the given offset, which is how a construct takes its lookahead (§11.1)
        void advance(std::size_t offset = 0);

        // consume a keyword the grammar requires, reporting a problem and recovering when it is absent (§11.1)
        void expect_keyword(std::string_view keyword);

        // consume a punctuation the grammar requires, reporting a problem and recovering when it is absent (§11.1)
        void expect_punctuation(std::string_view symbol);

        // read the identifier the grammar requires, reporting a problem when it is absent (§11.1)
        [[nodiscard]] std::string expect_identifier();

        // read a type name, which is bracketed for a property and bare for a signal parameter (§11.1, §11.2)
        [[nodiscard]] std::string expect_type_name(bool bracketed);

        // read the units between the angle brackets of a generic type, which the closing angle ends (§11.1)
        [[nodiscard]] std::string read_type_angles();

        // read one declaration of the file, an import, struct, enum, function, global or component (§11.1)
        void parse_declaration(SourceFile& file);

        // read an import, which names a manifest module (§11.1, AD-17)
        void parse_import(SourceFile& file);

        // read a struct declaration with its fields, which a property or a signal may then carry (§11.1)
        void parse_struct(SourceFile& file);

        // read an enum declaration with its members, which a property may then hold one of (§11.1)
        void parse_enum(SourceFile& file);

        // read a function declaration with its parameters and body, which a binding may call (§11.1)
        void parse_function(SourceFile& file);

        // read a module-scoped value declaration, which several components then read (§11.1)
        void parse_global(SourceFile& file);

        // read a component declaration, including its members and its root element (§11.1)
        std::unique_ptr<ComponentDeclaration> parse_component();

        // read one member of a component, which is a property, a signal or the root element (§11.1)
        void parse_component_member(ComponentDeclaration& component);

        // read a property declaration with its direction, type, name and optional default (§11.1)
        void parse_property_declaration(ComponentDeclaration& component);

        // read a signal declaration with its parameter types (§11.1)
        void parse_signal_declaration(ComponentDeclaration& component);

        // read an element or a repeat, which is one child of the tree (§11.1)
        std::unique_ptr<TreeNode> parse_tree_node();

        // read a repeat, whose body is instantiated once per item (§11.1)
        std::unique_ptr<TreeNode> parse_repeat();

        // read a conditional, whose body exists only while the condition holds (§11.1)
        std::unique_ptr<TreeNode> parse_conditional();

        // read the members of an element, which are assignments, handlers and children (§11.1)
        void parse_tree_body(TreeNode& node);

        // read one member of an element, which is an assignment, a handler or a child (§11.1)
        void parse_tree_member(TreeNode& node);

        // read a property assignment, which is an id when the name is the identity key (§11.1, AD-11)
        void parse_property_assignment(TreeNode& node);

        // read a handler attachment with its body (§11.1)
        void parse_handler(TreeNode& node);

        // read the drawing block of a canvas, which is a sequence of primitive calls (§6.4, §11.1)
        void parse_draw_block(TreeNode& node);

        // read a handler body, whose statements are assignments and signal invocations only (§11.1)
        void parse_handler_body(std::vector<HandlerStatement>& body);

        // read a comma separated argument list up to the closing parenthesis (§11.1)
        [[nodiscard]] std::vector<ExpressionPtr> parse_arguments();

        // read an expression at the lowest precedence, which is where a binding value starts (§11.1)
        [[nodiscard]] ExpressionPtr parse_expression();

        // read a disjunction, which is the loosest binding operator group (§11.1)
        [[nodiscard]] ExpressionPtr parse_disjunction();

        // read a conjunction, which binds tighter than a disjunction (§11.1)
        [[nodiscard]] ExpressionPtr parse_conjunction();

        // read an equality comparison, which binds tighter than a conjunction (§11.1)
        [[nodiscard]] ExpressionPtr parse_equality();

        // read an ordering comparison, which binds tighter than an equality (§11.1)
        [[nodiscard]] ExpressionPtr parse_comparison();

        // read an additive expression, which binds tighter than a comparison (§11.1)
        [[nodiscard]] ExpressionPtr parse_additive();

        // read a multiplicative expression, which binds tighter than an addition (§11.1)
        [[nodiscard]] ExpressionPtr parse_multiplicative();

        // read a prefix application, which binds tighter than any binary operator (§11.1)
        [[nodiscard]] ExpressionPtr parse_unary();

        // read a primary expression, which is a literal, a reference, a macro or a grouping (§11.1)
        [[nodiscard]] ExpressionPtr parse_primary();

        // read a bracketed collection literal, whose elements follow the full precedence (§8.1)
        [[nodiscard]] ExpressionPtr parse_array();

        // read a translatable string macro, which the catalog extractor finds in this one place (§11.1, §9.2)
        [[nodiscard]] ExpressionPtr parse_translation();

        // read a builtin conversion such as string(value), which the checker resolves by name (§11.1)
        [[nodiscard]] ExpressionPtr parse_conversion();

        // report that the current unit cannot appear here, which points a reader at the mistake (§11.1)
        void report_unexpected(std::string_view expected);

        // skip units until the next top level declaration, which is how one error costs one declaration (§11.1)
        void recover_to_declaration();

        // the units being parsed, which the caller keeps alive for the whole parse (§11.1)
        const std::vector<Token>& m_tokens;

        // the problems found while parsing, which the parser appends to (§11.1)
        DiagnosticList& m_diagnostics;

        // the offset of the unit the parser is on (§11.1)
        std::size_t m_index = 0;

        // whether an identifier may open a record construction, which a conditional condition suspends (§11.1)
        bool m_allow_construction = true;

        // the last unit of the stream, which every lookahead is clamped to so a read is never out of range (§11.1)
        Token m_end;

        // where the most recently consumed unit ended, which is what a closing span takes (§11.1)
        SourceLocation m_last_consumed_end;
    };
} // namespace albor::compiler
