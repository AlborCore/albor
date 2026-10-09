#pragma once

#include <memory>
#include <string>
#include <vector>

#include "compiler/diagnostic.h"

namespace albor::compiler
{
    // the base of every expression node, which lets the tree be walked without knowing its shape (§11.1)
    class Expression
    {
    public:
        // the expression is destroyed through this base, so the tree owns every node (§11.1)
        virtual ~Expression() = default;

        // the expression is neither copied nor moved, because a node is owned by exactly one tree (§11.1)
        Expression(const Expression&) = delete;

        // the expression is neither copied nor moved, because a node is owned by exactly one tree (§11.1)
        Expression& operator=(const Expression&) = delete;

        // hand out where the expression was written, which is what a diagnostic and a codegen span both need (§11.1)
        [[nodiscard]] virtual const SourceSpan& location() const = 0;

    protected:
        // only the tree constructs a node, because a node outside a tree has no owner (§11.1)
        Expression() = default;
    };

    // one owning pointer to an expression, because the tree is built before it is checked (§11.1)
    using ExpressionPtr = std::unique_ptr<Expression>;

    // a literal written as it appears in the source, which the checker maps onto a declared type (§11.1)
    class LiteralExpression : public Expression
    {
    public:
        // what kind of literal this is, which decides how the checker types it (§11.1)
        enum class Kind
        {
            // a signed integer written as digits (§11.1)
            Integer,

            // a floating point literal (§11.1)
            Float,

            // a quoted string, which is the only translatable literal form (§11.1, §9.2)
            String,

            // a colour written as hexadecimal channels (§11.1)
            Color,

            // a length written with its unit, which the checker resolves against the declared type (§11.1, AD-27)
            Length,

            // a truth literal (§11.1)
            Boolean
        };

        // what kind of literal this is (§11.1)
        Kind kind = Kind::Integer;

        // the spelling as written, which the checker parses and the codegen re-emits (§11.1)
        std::string text;

        // where the literal was written (§11.1)
        SourceSpan span;

        // hand out where the literal was written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a literal of the given kind, which the parser names after the unit it scanned (§11.1)
        static ExpressionPtr make(Kind kind, std::string text, SourceSpan span);
    };

    // a reference to a declared property, which becomes a typed read at the point of use (§11.1)
    class PropertyExpression : public Expression
    {
    public:
        // the declared name as written, which the checker resolves against the component (§11.1)
        std::string name;

        // where the reference was written (§11.1)
        SourceSpan span;

        // hand out where the reference was written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a property reference, which the parser makes from an identifier in expression position (§11.1)
        static ExpressionPtr make(std::string name, SourceSpan span);
    };

    // a translatable string, which is a distinct form because the catalog extractor has to find every one (§11.1, §9.2)
    class TranslationExpression : public Expression
    {
    public:
        // the message key or source text, which is what the catalog records (§11.1, §9.2)
        std::string text;

        // where the macro was written (§11.1, §9.2)
        SourceSpan span;

        // hand out where the macro was written (§11.1, §9.2)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a translation macro, which the parser makes from a @tr unit (§11.1, §9.2)
        static ExpressionPtr make(std::string text, SourceSpan span);
    };

    // a conversion such as string(value), which the checker resolves against the declared type of the target (§11.1)
    class ConversionExpression : public Expression
    {
    public:
        // the type the conversion produces, which is the builtin name as written (§11.1)
        std::string target_type;

        // the value being converted, which the checker types bottom up (§11.1)
        ExpressionPtr value;

        // where the conversion was written (§11.1)
        SourceSpan span;

        // hand out where the conversion was written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a conversion, which the parser makes from a builtin name in call position (§11.1)
        static ExpressionPtr make(std::string target_type, ExpressionPtr value, SourceSpan span);
    };

    // the binary operators the language defines, which is a closed set so the grammar needs no rules table (§11.1)
    enum class BinaryOperator
    {
        // string concatenation, the only operator that produces a string (§11.1)
        Add,

        // subtraction on the numeric types (§11.1)
        Subtract,

        // multiplication on the numeric types (§11.1)
        Multiply,

        // division on the numeric types (§11.1)
        Divide,

        // remainder on the integer types (§11.1)
        Modulo,

        // numeric or string comparison, which produces a bool (§11.1)
        Equal,

        // numeric or string comparison, which produces a bool (§11.1)
        NotEqual,

        // numeric comparison, which produces a bool (§11.1)
        Less,

        // numeric comparison, which produces a bool (§11.1)
        LessEqual,

        // numeric comparison, which produces a bool (§11.1)
        Greater,

        // numeric comparison, which produces a bool (§11.1)
        GreaterEqual,

        // boolean conjunction, which is short circuiting (§11.1)
        And,

        // boolean disjunction, which is short circuiting (§11.1)
        Or
    };

    // a binary application, which the checker types from its two operands (§11.1)
    class BinaryExpression : public Expression
    {
    public:
        // which operator was written (§11.1)
        BinaryOperator op = BinaryOperator::Add;

        // the left operand, which the checker types first (§11.1)
        ExpressionPtr left;

        // the right operand, which the checker types second (§11.1)
        ExpressionPtr right;

        // where the application was written (§11.1)
        SourceSpan span;

        // hand out where the application was written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a binary application, which the parser makes once it has both operands (§11.1)
        static ExpressionPtr make(BinaryOperator op, ExpressionPtr left, ExpressionPtr right, SourceSpan span);
    };

    // the unary operators the language defines, which is a closed set (§11.1)
    enum class UnaryOperator
    {
        // logical negation, which requires a bool (§11.1)
        Not,

        // numeric negation, which requires a number (§11.1)
        Negate
    };

    // a unary application, which the checker types from its single operand (§11.1)
    class UnaryExpression : public Expression
    {
    public:
        // which operator was written (§11.1)
        UnaryOperator op = UnaryOperator::Not;

        // the operand, which the checker types first (§11.1)
        ExpressionPtr value;

        // where the application was written (§11.1)
        SourceSpan span;

        // hand out where the application was written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a unary application, which the parser makes once it has its operand (§11.1)
        static ExpressionPtr make(UnaryOperator op, ExpressionPtr value, SourceSpan span);
    };

    // a parenthesised expression, which is kept so the codegen can preserve the written grouping (§11.1)
    class GroupingExpression : public Expression
    {
    public:
        // the grouped expression, which the checker types as if the parentheses were absent (§11.1)
        ExpressionPtr inner;

        // where the parentheses were written (§11.1)
        SourceSpan span;

        // hand out where the parentheses were written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a grouping, which the parser makes when it sees an opening parenthesis (§11.1)
        static ExpressionPtr make(ExpressionPtr inner, SourceSpan span);
    };

    // a bracketed list of expressions, which is the only way an author writes a collection value (§8.1, §11.1)
    class ArrayExpression : public Expression
    {
    public:
        // the elements in written order, which is the order the collection preserves (§8.1)
        std::vector<ExpressionPtr> elements;

        // where the brackets were written (§11.1)
        SourceSpan span;

        // hand out where the brackets were written (§11.1)
        [[nodiscard]] const SourceSpan& location() const override { return span; }

        // build a bracketed list, which the parser makes once it has read the elements (§8.1)
        static ExpressionPtr make(std::vector<ExpressionPtr> elements, SourceSpan span);
    };

    // the statements a handler body may contain, which is a closed set of two forms (§11.1)
    enum class HandlerStatementKind
    {
        // an assignment to a declared property (§11.1)
        Assignment,

        // an invocation of a declared signal (§11.1)
        SignalInvocation
    };

    // one statement inside a handler body, which is either an assignment or a signal invocation (§11.1)
    class HandlerStatement
    {
    public:
        // what kind of statement this is, which decides which fields below are set (§11.1)
        HandlerStatementKind kind = HandlerStatementKind::Assignment;

        // the property being assigned or the signal being invoked, which is a declared name (§11.1)
        std::string target;

        // the assigned value, which is empty for a signal invocation (§11.1)
        ExpressionPtr value;

        // the signal arguments, which are empty for an assignment (§11.1)
        std::vector<ExpressionPtr> arguments;

        // where the statement was written (§11.1)
        SourceSpan span;
    };

    // a signal declared by a component, which names what a handler may invoke (§11.1)
    struct SignalDeclaration
    {
        // the declared name, which the checker resolves and the facade exposes (§11.1)
        std::string name;

        // the parameter types as written, which the manifest and the facade agree on (§11.1)
        std::vector<std::string> parameter_types;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // the direction a property may move in, which decides whether a handler may assign it (§11.1, §7)
    enum class PropertyDirection
    {
        // assigned only from outside, which is the default for a bound property (§11.1)
        In,

        // assigned from a handler as well as from outside (§11.1)
        InOut
    };

    // a property declared by a component, which names what a binding or a handler may refer to (§11.1)
    struct PropertyDeclaration
    {
        // the declared name, which the checker resolves and the facade exposes (§11.1)
        std::string name;

        // the type as written between the angle brackets (§11.1)
        std::string type;

        // the direction, which decides whether a handler may assign it (§11.1)
        PropertyDirection direction = PropertyDirection::In;

        // the declared default, which is empty when the property has none (§11.1)
        ExpressionPtr default_value;

        // where the declaration was written (§11.1)
        SourceSpan span;
    };

    // one property assignment inside an element, which is a static value or a binding (§11.1)
    struct PropertyAssignment
    {
        // the property name as written, which the checker resolves against the element's type (§11.1)
        std::string name;

        // the assigned expression, which is a literal for a static value (§11.1)
        ExpressionPtr value;

        // where the assignment was written (§11.1)
        SourceSpan span;
    };

    // one handler attached to an element, which runs when the element raises the named signal (§11.1)
    struct HandlerAttachment
    {
        // the signal name as written, which the checker resolves against the element's type (§11.1)
        std::string name;

        // the body, which may only assign properties or invoke signals (§11.1)
        std::vector<HandlerStatement> body;

        // where the handler was written (§11.1)
        SourceSpan span;
    };

    // the kinds of child element a tree node may contain, which is a closed set of two (§11.1)
    enum class TreeNodeKind
    {
        // a declared element type such as Column or Button, which the manifest or the widget catalog resolves (§11.1)
        Element,

        // a repeated element, whose body is instantiated once per item (§11.1)
        Repeat
    };

    // one child of an element, which is either a declared element or a repeated element (§11.1)
    class TreeNode
    {
    public:
        // what kind of child this is, which decides which fields below are set (§11.1)
        TreeNodeKind kind = TreeNodeKind::Element;

        // the element type name as written, which is empty for a repeat (§11.1)
        std::string element_type;

        // the author assigned id, which is the node's identity and is empty when the author wrote none (§11.1, AD-11)
        std::string explicit_id;

        // the property assignments of an element (§11.1)
        std::vector<PropertyAssignment> assignments;

        // the handlers of an element (§11.1)
        std::vector<HandlerAttachment> handlers;

        // the children of an element, which may themselves be repeats (§11.1)
        std::vector<std::unique_ptr<TreeNode>> children;

        // the repeated item expression of a repeat, which yields the item and the index (§11.1)
        ExpressionPtr repeat_source;

        // the name the repeated item is bound to inside the body (§11.1)
        std::string repeat_item_name;

        // the name the repeated index is bound to inside the body, which is empty when the author wrote none (§11.1)
        std::string repeat_index_name;

        // the body of a repeat, which is the element instantiated per item (§11.1)
        std::unique_ptr<TreeNode> repeat_body;

        // where the node was written (§11.1)
        SourceSpan span;
    };

    // a module brought into scope by an import, which names a manifest module (§11.1, AD-17)
    struct ImportDeclaration
    {
        // the module name as written, which the checker resolves against the manifest set (§11.1, AD-17)
        std::string module;

        // where the import was written (§11.1, AD-17)
        SourceSpan span;
    };

    // a whole parsed component, which is the unit the checker and the codegen both take (§11.1)
    struct ComponentDeclaration
    {
        // the component name as written, which becomes the generated facade class name (§11.1, AD-03a)
        std::string name;

        // the modules the component refers to (§11.1, AD-17)
        std::vector<ImportDeclaration> imports;

        // the properties the component declares (§11.1)
        std::vector<PropertyDeclaration> properties;

        // the signals the component declares (§11.1)
        std::vector<SignalDeclaration> signals;

        // the root element of the component, which is empty when the component declares none (§11.1)
        std::unique_ptr<TreeNode> root;

        // where the declaration starts, which is what a driver reports against (§11.1)
        SourceSpan span;
    };
} // namespace albor::compiler
