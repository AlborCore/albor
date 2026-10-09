#include "compiler/ast.h"

#include <utility>

namespace albor::compiler
{
    ExpressionPtr LiteralExpression::make(Kind kind, std::string text, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<LiteralExpression>();
        // the kind is recorded first, because it decides how the checker types the value (§11.1)
        node->kind = kind;
        // the spelling is moved in rather than copied, because the scanner already built it (§11.1)
        node->text = std::move(text);
        // the span is what a diagnostic about the literal points at (§11.1)
        node->span = span;
        return node;
    }

    ExpressionPtr PropertyExpression::make(std::string name, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<PropertyExpression>();
        // the name is moved in rather than copied, because the scanner already built it (§11.1)
        node->name = std::move(name);
        // the span is what a diagnostic about the reference points at (§11.1)
        node->span = span;
        return node;
    }

    ExpressionPtr TranslationExpression::make(std::string text, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<TranslationExpression>();
        // the source text is moved in rather than copied, because the scanner already built it (§11.1, §9.2)
        node->text = std::move(text);
        // the span is what the catalog records as the string's location (§11.1, §9.2)
        node->span = span;
        return node;
    }

    ExpressionPtr ConversionExpression::make(std::string target_type, ExpressionPtr value, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<ConversionExpression>();
        // the target type is moved in rather than copied, because the scanner already built it (§11.1)
        node->target_type = std::move(target_type);
        // the operand is moved in, because only one tree may own it (§11.1)
        node->value = std::move(value);
        // the span covers the whole call, so a diagnostic about the conversion points at it (§11.1)
        node->span = span;
        return node;
    }

    ExpressionPtr BinaryExpression::make(BinaryOperator op, ExpressionPtr left, ExpressionPtr right, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<BinaryExpression>();
        // the operator is recorded first, because it decides how the operands are typed (§11.1)
        node->op = op;
        // the left operand is moved in, because only one tree may own it (§11.1)
        node->left = std::move(left);
        // the right operand is moved in, because only one tree may own it (§11.1)
        node->right = std::move(right);
        // the span covers the whole application, so a diagnostic about it points at it (§11.1)
        node->span = span;
        return node;
    }

    ExpressionPtr UnaryExpression::make(UnaryOperator op, ExpressionPtr value, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<UnaryExpression>();
        // the operator is recorded first, because it decides how the operand is typed (§11.1)
        node->op = op;
        // the operand is moved in, because only one tree may own it (§11.1)
        node->value = std::move(value);
        // the span covers the whole application, so a diagnostic about it points at it (§11.1)
        node->span = span;
        return node;
    }

    ExpressionPtr GroupingExpression::make(ExpressionPtr inner, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<GroupingExpression>();
        // the grouped expression is moved in, because only one tree may own it (§11.1)
        node->inner = std::move(inner);
        // the span covers the parentheses, so a diagnostic about the grouping points at them (§11.1)
        node->span = span;
        return node;
    }

    ExpressionPtr ArrayExpression::make(std::vector<ExpressionPtr> elements, SourceSpan span) {
        // the node is allocated before it is filled, because the caller only holds the owning pointer (§11.1)
        auto node = std::make_unique<ArrayExpression>();
        // the elements are moved in, because only one tree may own them (§11.1)
        node->elements = std::move(elements);
        // the span covers the brackets, so a diagnostic about the list points at them (§8.1)
        node->span = span;
        return node;
    }
} // namespace albor::compiler
