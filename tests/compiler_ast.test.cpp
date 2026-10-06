#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "compiler/ast.h"

using namespace albor::compiler;

namespace
{
    // reach a literal without naming its concrete type, because the tests assert on values rather than on classes (§11.1)
    LiteralExpression& as_literal(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds a literal itself (§11.1)
        return static_cast<LiteralExpression&>(*expression);
    }

    // reach a property reference without naming its concrete type (§11.1)
    PropertyExpression& as_property(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds a reference itself (§11.1)
        return static_cast<PropertyExpression&>(*expression);
    }

    // reach a translation macro without naming its concrete type (§11.1, §9.2)
    TranslationExpression& as_translation(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds a translation itself (§11.1)
        return static_cast<TranslationExpression&>(*expression);
    }

    // reach a conversion without naming its concrete type (§11.1)
    ConversionExpression& as_conversion(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds a conversion itself (§11.1)
        return static_cast<ConversionExpression&>(*expression);
    }

    // reach a binary application without naming its concrete type (§11.1)
    BinaryExpression& as_binary(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds an application itself (§11.1)
        return static_cast<BinaryExpression&>(*expression);
    }

    // reach a unary application without naming its concrete type (§11.1)
    UnaryExpression& as_unary(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds an application itself (§11.1)
        return static_cast<UnaryExpression&>(*expression);
    }

    // reach a grouping without naming its concrete type (§11.1)
    GroupingExpression& as_grouping(const ExpressionPtr& expression) {
        // the cast is safe because every test in this file builds a grouping itself (§11.1)
        return static_cast<GroupingExpression&>(*expression);
    }

    // a span that covers the whole of a one token construct, which most assertions share (§11.1)
    SourceSpan whole_span() {
        // the span runs from the first to the second column, so it is never empty (§11.1)
        return SourceSpan{.start = SourceLocation{.line = 1, .column = 1}, .end = SourceLocation{.line = 1, .column = 2}};
    }
} // namespace

TEST(LiteralExpression, CarriesItsKindTextAndSpan) {
    // arrange, act
    ExpressionPtr expression = LiteralExpression::make(LiteralExpression::Kind::Integer, "42", whole_span());
    // assert
    const LiteralExpression& literal = as_literal(expression);
    EXPECT_EQ(literal.kind, LiteralExpression::Kind::Integer);
    EXPECT_EQ(literal.text, "42");
    EXPECT_EQ(literal.span.start.line, 1U);
}

TEST(LiteralExpression, CarriesEveryLiteralKind) {
    // arrange, act, assert
    EXPECT_EQ(as_literal(LiteralExpression::make(LiteralExpression::Kind::Float, "1.5", whole_span())).kind, LiteralExpression::Kind::Float);
    EXPECT_EQ(as_literal(LiteralExpression::make(LiteralExpression::Kind::String, "text", whole_span())).kind, LiteralExpression::Kind::String);
    EXPECT_EQ(as_literal(LiteralExpression::make(LiteralExpression::Kind::Color, "FF0000", whole_span())).kind, LiteralExpression::Kind::Color);
    EXPECT_EQ(as_literal(LiteralExpression::make(LiteralExpression::Kind::Length, "12px", whole_span())).kind, LiteralExpression::Kind::Length);
    EXPECT_EQ(as_literal(LiteralExpression::make(LiteralExpression::Kind::Boolean, "true", whole_span())).kind, LiteralExpression::Kind::Boolean);
}

TEST(PropertyExpression, CarriesItsNameAndSpan) {
    // arrange, act
    ExpressionPtr expression = PropertyExpression::make("click_count", whole_span());
    // assert
    const PropertyExpression& reference = as_property(expression);
    EXPECT_EQ(reference.name, "click_count");
    EXPECT_EQ(reference.span.start.column, 1U);
}

TEST(TranslationExpression, CarriesItsTextAndSpan) {
    // arrange, act
    ExpressionPtr expression = TranslationExpression::make("Hello", whole_span());
    // assert
    // the text is what the catalog records, so it must survive the move into the node (§11.1, §9.2)
    const TranslationExpression& translation = as_translation(expression);
    EXPECT_EQ(translation.text, "Hello");
    EXPECT_EQ(translation.span.end.column, 2U);
}

TEST(ConversionExpression, CarriesItsTargetValueAndSpan) {
    // arrange
    ExpressionPtr operand = LiteralExpression::make(LiteralExpression::Kind::Integer, "7", whole_span());
    // act
    ExpressionPtr expression = ConversionExpression::make("string", std::move(operand), whole_span());
    // assert
    const ConversionExpression& conversion = as_conversion(expression);
    EXPECT_EQ(conversion.target_type, "string");
    ASSERT_TRUE(conversion.value != nullptr);
    EXPECT_EQ(as_literal(conversion.value).text, "7");
    EXPECT_EQ(conversion.span.start.line, 1U);
}

TEST(BinaryExpression, CarriesItsOperandsOperatorAndSpan) {
    // arrange
    ExpressionPtr left = PropertyExpression::make("count", whole_span());
    ExpressionPtr right = LiteralExpression::make(LiteralExpression::Kind::Integer, "1", whole_span());
    // act
    ExpressionPtr expression = BinaryExpression::make(BinaryOperator::Add, std::move(left), std::move(right), whole_span());
    // assert
    const BinaryExpression& binary = as_binary(expression);
    EXPECT_EQ(binary.op, BinaryOperator::Add);
    ASSERT_TRUE(binary.left != nullptr);
    ASSERT_TRUE(binary.right != nullptr);
    EXPECT_EQ(as_property(binary.left).name, "count");
    EXPECT_EQ(as_literal(binary.right).text, "1");
}

TEST(BinaryExpression, CarriesEveryOperatorTheLanguageDefines) {
    // arrange
    const std::vector<BinaryOperator> operators = {BinaryOperator::Add, BinaryOperator::Subtract, BinaryOperator::Multiply, BinaryOperator::Divide, BinaryOperator::Modulo, BinaryOperator::Equal, BinaryOperator::NotEqual, BinaryOperator::Less, BinaryOperator::LessEqual, BinaryOperator::Greater, BinaryOperator::GreaterEqual, BinaryOperator::And, BinaryOperator::Or};
    // act
    std::size_t count = 0;
    for (BinaryOperator op : operators)
        if (as_binary(BinaryExpression::make(op, nullptr, nullptr, whole_span())).op == op)
            count++;
    // assert
    // every operator the grammar accepts is represented, because the parser maps a token straight onto one (§11.1)
    EXPECT_EQ(count, operators.size());
}

TEST(UnaryExpression, CarriesItsOperandOperatorAndSpan) {
    // arrange
    ExpressionPtr operand = PropertyExpression::make("enabled", whole_span());
    // act
    ExpressionPtr expression = UnaryExpression::make(UnaryOperator::Not, std::move(operand), whole_span());
    // assert
    const UnaryExpression& unary = as_unary(expression);
    EXPECT_EQ(unary.op, UnaryOperator::Not);
    ASSERT_TRUE(unary.value != nullptr);
    EXPECT_EQ(as_property(unary.value).name, "enabled");
}

TEST(UnaryExpression, CarriesBothOperatorsTheLanguageDefines) {
    // arrange, act, assert
    EXPECT_EQ(as_unary(UnaryExpression::make(UnaryOperator::Not, nullptr, whole_span())).op, UnaryOperator::Not);
    EXPECT_EQ(as_unary(UnaryExpression::make(UnaryOperator::Negate, nullptr, whole_span())).op, UnaryOperator::Negate);
}

TEST(GroupingExpression, CarriesItsInnerExpressionAndSpan) {
    // arrange
    ExpressionPtr inner = LiteralExpression::make(LiteralExpression::Kind::Integer, "3", whole_span());
    // act
    ExpressionPtr expression = GroupingExpression::make(std::move(inner), whole_span());
    // assert
    const GroupingExpression& grouping = as_grouping(expression);
    ASSERT_TRUE(grouping.inner != nullptr);
    EXPECT_EQ(as_literal(grouping.inner).text, "3");
    EXPECT_EQ(grouping.span.start.line, 1U);
}

TEST(Expression, IsOwnedByItsTreeAndFreedWithIt) {
    // arrange, act
    ExpressionPtr owned = LiteralExpression::make(LiteralExpression::Kind::Integer, "1", whole_span());
    // the owner goes out of scope at the end of the block, which frees the node with it (§11.1)
    {
        // the owner is moved into the tree, so only the tree holds the node afterwards (§11.1)
        ExpressionPtr in_tree = std::move(owned);
        EXPECT_TRUE(in_tree != nullptr);
    }
    // assert
    EXPECT_TRUE(owned == nullptr);
}

TEST(HandlerStatement, DefaultsToAnAssignment) {
    // arrange, act
    HandlerStatement statement;
    // assert
    EXPECT_EQ(statement.kind, HandlerStatementKind::Assignment);
    EXPECT_TRUE(statement.target.empty());
    EXPECT_TRUE(statement.value == nullptr);
    EXPECT_TRUE(statement.arguments.empty());
}

TEST(PropertyDeclaration, DefaultsToTheIncomingDirectionAndNoDefault) {
    // arrange, act
    const PropertyDeclaration declaration{};
    // assert
    EXPECT_EQ(declaration.direction, PropertyDirection::In);
    EXPECT_TRUE(declaration.default_value == nullptr);
    EXPECT_TRUE(declaration.name.empty());
    EXPECT_TRUE(declaration.type.empty());
}

TEST(TreeNode, DefaultsToAnElementWithNoAuthorIdentity) {
    // arrange, act
    const TreeNode node{};
    // assert
    EXPECT_EQ(node.kind, TreeNodeKind::Element);
    // an author that wrote no id leaves the identity empty rather than inventing one (§18.1, AD-11)
    EXPECT_TRUE(node.explicit_id.empty());
    EXPECT_TRUE(node.element_type.empty());
    EXPECT_TRUE(node.repeat_source == nullptr);
    EXPECT_TRUE(node.repeat_body == nullptr);
    EXPECT_TRUE(node.children.empty());
}

TEST(ComponentDeclaration, StartsWithNothingDeclared) {
    // arrange, act
    const ComponentDeclaration declaration{};
    // assert
    EXPECT_TRUE(declaration.name.empty());
    EXPECT_TRUE(declaration.imports.empty());
    EXPECT_TRUE(declaration.properties.empty());
    EXPECT_TRUE(declaration.signals.empty());
    EXPECT_TRUE(declaration.root == nullptr);
}
