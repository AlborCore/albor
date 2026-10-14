#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "compiler/lexer.h"
#include "compiler/parser.h"

using namespace albor::compiler;

namespace
{
    // scan and parse a source, which the suites below use so they only state what they are about (§11.1)
    SourceFile parse(std::string_view source, DiagnosticList& diagnostics) {
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser then builds the file from those units (§11.1)
        return Parser(tokens, diagnostics).parse();
    }

    // scan and parse a source that is expected to be clean, which fails the test when it is not (§11.1)
    SourceFile parse(std::string_view source) {
        // the diagnostics are collected so a stray problem fails the test instead of disappearing (§11.1)
        DiagnosticList diagnostics;
        // the file is parsed, and the collected problems are checked before it is returned (§11.1)
        SourceFile file = parse(source, diagnostics);
        // a clean source produces nothing, so a problem here is a parser defect (§11.1)
        EXPECT_EQ(diagnostics.count(), 0U);
        return file;
    }

    // the first diagnostic of a run, which most failure assertions read better against (§11.1)
    const Diagnostic& first(const DiagnosticList& diagnostics) {
        // a run with no diagnostic fails the test rather than reading past the list (§11.1)
        EXPECT_GE(diagnostics.count(), 1U);
        return diagnostics.diagnostics().at(0);
    }

    // reach a literal without naming its concrete type, because the suites assert on values (§11.1)
    LiteralExpression& as_literal(const ExpressionPtr& expression) {
        // the cast is safe because every suite in this file builds the expression itself (§11.1)
        return static_cast<LiteralExpression&>(*expression);
    }

    // reach a property reference without naming its concrete type (§11.1)
    PropertyExpression& as_property(const ExpressionPtr& expression) {
        // the cast is safe because every suite in this file builds the expression itself (§11.1)
        return static_cast<PropertyExpression&>(*expression);
    }

    // reach a translation macro without naming its concrete type (§11.1, §9.2)
    TranslationExpression& as_translation(const ExpressionPtr& expression) {
        // the cast is safe because every suite in this file builds the expression itself (§11.1)
        return static_cast<TranslationExpression&>(*expression);
    }

    // reach a conversion without naming its concrete type (§11.1)
    ConversionExpression& as_conversion(const ExpressionPtr& expression) {
        // the cast is safe because every suite in this file builds the expression itself (§11.1)
        return static_cast<ConversionExpression&>(*expression);
    }

    // reach a binary application without naming its concrete type (§11.1)
    BinaryExpression& as_binary(const ExpressionPtr& expression) {
        // the cast is safe because every suite in this file builds the expression itself (§11.1)
        return static_cast<BinaryExpression&>(*expression);
    }

    // render an expression as a parenthesised string, which is how precedence assertions read (§11.1)
    std::string shape(const ExpressionPtr& expression) {
        // a literal renders as its own spelling (§11.1)
        if (const auto* literal = dynamic_cast<const LiteralExpression*>(expression.get()))
            return literal->text;
        // a reference renders as its name (§11.1)
        if (const auto* reference = dynamic_cast<const PropertyExpression*>(expression.get()))
            return reference->name;
        // a translation renders as the macro applied to its text (§11.1, §9.2)
        if (const auto* translation = dynamic_cast<const TranslationExpression*>(expression.get()))
            return "@tr(" + translation->text + ")";
        // a conversion renders as its target applied to its value (§11.1)
        if (const auto* conversion = dynamic_cast<const ConversionExpression*>(expression.get()))
            return conversion->target_type + "(" + shape(conversion->value) + ")";
        // a unary application renders as its operator applied to its operand (§11.1)
        if (const auto* unary = dynamic_cast<const UnaryExpression*>(expression.get()))
            return std::string(unary->op == UnaryOperator::Not ? "!" : "-") + shape(unary->value);
        // a grouping renders with parentheses, because the written grouping is what it preserves (§11.1)
        if (const auto* grouping = dynamic_cast<const GroupingExpression*>(expression.get()))
            return "(" + shape(grouping->inner) + ")";
        // a binary application renders as its operands joined by its operator (§11.1)
        const auto& binary = as_binary(expression);
        // the spelling is the one the parser accepted, so the assertion states the written operator (§11.1)
        std::string op = "?";
        // each operator has one spelling, because the grammar maps the unit straight onto it (§11.1)
        switch (binary.op) {
        case BinaryOperator::Add:
            op = "+";
            break;
        case BinaryOperator::Subtract:
            op = "-";
            break;
        case BinaryOperator::Multiply:
            op = "*";
            break;
        case BinaryOperator::Divide:
            op = "/";
            break;
        case BinaryOperator::Modulo:
            op = "%";
            break;
        case BinaryOperator::Equal:
            op = "==";
            break;
        case BinaryOperator::NotEqual:
            op = "!=";
            break;
        case BinaryOperator::Less:
            op = "<";
            break;
        case BinaryOperator::LessEqual:
            op = "<=";
            break;
        case BinaryOperator::Greater:
            op = ">";
            break;
        case BinaryOperator::GreaterEqual:
            op = ">=";
            break;
        case BinaryOperator::And:
            op = "&&";
            break;
        case BinaryOperator::Or:
            op = "||";
            break;
        }
        // the operands are parenthesised so the nesting is visible rather than implied (§11.1)
        return "(" + shape(binary.left) + " " + op + " " + shape(binary.right) + ")";
    }

    // the whole of the example component from §11.1, which most suites parse as a whole (§11.1)
    constexpr std::string_view MAIN_VIEW = R"(
import albor.controls;

component MainView {
    in property<string> user_name: "World";
    in property<int> click_count: 0;

    signal run_clicked();

    Column {
        spacing: 12px;
        padding: 16px;

        Label {
            text: @tr("Hello, ") + user_name + "!";
            font_size: 18px;
        }

        Row {
            spacing: 8px;

            Button {
                id: action_button;
                text: @tr("Increment");
                on_clicked: {
                    click_count = click_count + 1;
                    run_clicked();
                }
            }

            Label {
                text: @tr("Count: ") + string(click_count);
            }
        }
    }
}
)";
} // namespace

TEST(Parser, EmptySourceProducesNoDeclarations) {
    // arrange, act
    const SourceFile file = parse("");
    // assert
    EXPECT_TRUE(file.imports.empty());
    EXPECT_TRUE(file.components.empty());
}

TEST(Parser, ReadsAQualifiedImport) {
    // arrange, act
    const SourceFile file = parse("import albor.controls;");
    // assert
    ASSERT_EQ(file.imports.size(), 1U);
    EXPECT_EQ(file.imports.at(0).module, "albor.controls");
    EXPECT_EQ(file.imports.at(0).span.start.line, 1U);
}

TEST(Parser, ReadsAnUnqualifiedImport) {
    // arrange, act
    const SourceFile file = parse("import albor;");
    // assert
    ASSERT_EQ(file.imports.size(), 1U);
    EXPECT_EQ(file.imports.at(0).module, "albor");
}

TEST(Parser, ReadsAStructWithItsFields) {
    // arrange, act
    const SourceFile file = parse("struct Frame { float width; float height; }");
    // assert
    ASSERT_EQ(file.structs.size(), 1U);
    const StructDeclaration& frame = file.structs.at(0);
    EXPECT_EQ(frame.name, "Frame");
    ASSERT_EQ(frame.fields.size(), 2U);
    EXPECT_EQ(frame.fields.at(0).name, "width");
    EXPECT_EQ(frame.fields.at(0).type, "float");
    EXPECT_EQ(frame.fields.at(1).name, "height");
    EXPECT_EQ(frame.fields.at(1).type, "float");
}

TEST(Parser, ReadsAStructFieldWithAParametricType) {
    // arrange, act
    const SourceFile file = parse("struct Box { vector<string> items; }");
    // assert
    ASSERT_EQ(file.structs.size(), 1U);
    ASSERT_EQ(file.structs.at(0).fields.size(), 1U);
    EXPECT_EQ(file.structs.at(0).fields.at(0).name, "items");
    EXPECT_EQ(file.structs.at(0).fields.at(0).type, "vector<string>");
}

TEST(Parser, ReportsAStructWithNoName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("struct { float width; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsTheExampleComponentWhole) {
    // arrange, act
    const SourceFile file = parse(MAIN_VIEW);
    // assert
    ASSERT_EQ(file.imports.size(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    const ComponentDeclaration& component = file.components.at(0);
    EXPECT_EQ(component.name, "MainView");
    ASSERT_EQ(component.properties.size(), 2U);
    ASSERT_EQ(component.signals.size(), 1U);
    ASSERT_TRUE(component.root != nullptr);
    EXPECT_EQ(component.root->element_type, "Column");
}

TEST(Parser, ReadsAnIncomingPropertyWithItsTypeAndDefault) {
    // arrange, act
    const SourceFile file = parse("component C { in property<string> name: \"World\"; }");
    // assert
    ASSERT_EQ(file.components.size(), 1U);
    const PropertyDeclaration& property = file.components.at(0).properties.at(0);
    EXPECT_EQ(property.name, "name");
    EXPECT_EQ(property.type, "string");
    EXPECT_EQ(property.direction, PropertyDirection::In);
    ASSERT_TRUE(property.default_value != nullptr);
    EXPECT_EQ(as_literal(property.default_value).text, "World");
}

TEST(Parser, ReadsAnAssignablePropertyAsInOut) {
    // arrange, act
    const SourceFile file = parse("component C { inout property<int> count: 0; }");
    // assert
    const PropertyDeclaration& property = file.components.at(0).properties.at(0);
    EXPECT_EQ(property.direction, PropertyDirection::InOut);
    EXPECT_EQ(property.type, "int");
}

TEST(Parser, ReadsAPropertyWithNoDefault) {
    // arrange, act
    const SourceFile file = parse("component C { in property<color> tint; }");
    // assert
    const PropertyDeclaration& property = file.components.at(0).properties.at(0);
    EXPECT_TRUE(property.default_value == nullptr);
    EXPECT_EQ(property.type, "color");
}

TEST(Parser, ReadsANestedTypeName) {
    // arrange, act
    const SourceFile file = parse("component C { in property<Snapshot<WaveformData>> data; }");
    // assert
    EXPECT_EQ(file.components.at(0).properties.at(0).type, "Snapshot<WaveformData>");
}

TEST(Parser, ReportsATypeNameThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { in property<string name; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsASignalWithNoParameters) {
    // arrange, act
    const SourceFile file = parse("component C { signal changed(); }");
    // assert
    const SignalDeclaration& signal = file.components.at(0).signals.at(0);
    EXPECT_EQ(signal.name, "changed");
    EXPECT_TRUE(signal.parameter_types.empty());
}

TEST(Parser, ReadsASignalWithParameters) {
    // arrange, act
    const SourceFile file = parse("component C { signal moved(float time_pos, int index); }");
    // assert
    const SignalDeclaration& signal = file.components.at(0).signals.at(0);
    ASSERT_EQ(signal.parameter_types.size(), 2U);
    EXPECT_EQ(signal.parameter_types.at(0), "float");
    EXPECT_EQ(signal.parameter_types.at(1), "int");
}

TEST(Parser, ReportsAComponentThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { in property<int> n;", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsAComponentWithNoRootElement) {
    // arrange, act
    const SourceFile file = parse("component C { signal changed(); }");
    // assert
    EXPECT_TRUE(file.components.at(0).root == nullptr);
}

TEST(Parser, ReportsASecondRootElement) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { Column {} Row {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsStaticAssignmentsOnAnElement) {
    // arrange, act
    const SourceFile file = parse("component C { Column { spacing: 12px; padding: 16px; } }");
    // assert
    const TreeNode& root = *file.components.at(0).root;
    ASSERT_EQ(root.assignments.size(), 2U);
    EXPECT_EQ(root.assignments.at(0).name, "spacing");
    EXPECT_EQ(as_literal(root.assignments.at(0).value).kind, LiteralExpression::Kind::Length);
    EXPECT_EQ(root.assignments.at(1).name, "padding");
}

TEST(Parser, ReadsAnAuthorIdentityAsABareName) {
    // arrange, act
    const SourceFile file = parse("component C { Button { id: action_button; } }");
    // assert
    const TreeNode& root = *file.components.at(0).root;
    // the identity is the node's own field rather than a property assignment (§18.1, AD-11)
    EXPECT_EQ(root.explicit_id, "action_button");
    EXPECT_TRUE(root.assignments.empty());
}

TEST(Parser, LeavesTheIdentityEmptyWhenTheAuthorWroteNone) {
    // arrange, act
    const SourceFile file = parse("component C { Button { text: \"go\"; } }");
    // assert
    EXPECT_TRUE(file.components.at(0).root->explicit_id.empty());
}

TEST(Parser, ReportsAnIdentityThatIsNotABareName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { Button { id: 1; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0200");
}

TEST(Parser, ReadsNestedChildren) {
    // arrange, act
    const SourceFile file = parse("component C { Column { Row { Label {} } } }");
    // assert
    const TreeNode& root = *file.components.at(0).root;
    ASSERT_EQ(root.children.size(), 1U);
    EXPECT_EQ(root.children.at(0)->element_type, "Row");
    ASSERT_EQ(root.children.at(0)->children.size(), 1U);
    EXPECT_EQ(root.children.at(0)->children.at(0)->element_type, "Label");
}

TEST(Parser, ReadsAHandlerWithAnAssignmentAndASignalInvocation) {
    // arrange, act
    const SourceFile file = parse("component C { Button { on_clicked: { count = count + 1; changed(); } } }");
    // assert
    const TreeNode& root = *file.components.at(0).root;
    ASSERT_EQ(root.handlers.size(), 1U);
    const HandlerAttachment& handler = root.handlers.at(0);
    // the prefix marks the member as a handler, so the signal is the name behind it (§11.1)
    EXPECT_EQ(handler.name, "clicked");
    ASSERT_EQ(handler.body.size(), 2U);
    EXPECT_EQ(handler.body.at(0).kind, HandlerStatementKind::Assignment);
    EXPECT_EQ(handler.body.at(0).target, "count");
    EXPECT_EQ(shape(handler.body.at(0).value), "(count + 1)");
    EXPECT_EQ(handler.body.at(1).kind, HandlerStatementKind::SignalInvocation);
    EXPECT_EQ(handler.body.at(1).target, "changed");
    EXPECT_TRUE(handler.body.at(1).arguments.empty());
}

TEST(Parser, ReadsASignalInvocationWithArguments) {
    // arrange, act
    const SourceFile file = parse("component C { B { on_x: { moved(index + 1, 2.0); } } }");
    // assert
    const HandlerStatement& statement = file.components.at(0).root->handlers.at(0).body.at(0);
    ASSERT_EQ(statement.arguments.size(), 2U);
    EXPECT_EQ(shape(statement.arguments.at(0)), "(index + 1)");
    EXPECT_EQ(shape(statement.arguments.at(1)), "2.0");
}

TEST(Parser, ReportsAHandlerStatementThatIsNeitherForm) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { on_x: { count + 1; } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0201");
}

TEST(Parser, ReportsAHandlerBodyThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { on_x: { count = 1; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsAnElementWithNoBodyContents) {
    // arrange, act
    const SourceFile file = parse("component C { Label {} }");
    // assert
    const TreeNode& root = *file.components.at(0).root;
    EXPECT_TRUE(root.assignments.empty());
    EXPECT_TRUE(root.handlers.empty());
    EXPECT_TRUE(root.children.empty());
}

TEST(Parser, ReportsAnElementThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { Column { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsARepeatWithAnItemAndAnIndex) {
    // arrange, act
    const SourceFile file = parse("component C { Column { Repeat(item in items, index) { Label { text: string(item); } } } }");
    // assert
    const TreeNode& repeat = *file.components.at(0).root->children.at(0);
    EXPECT_EQ(repeat.kind, TreeNodeKind::Repeat);
    EXPECT_EQ(repeat.repeat_item_name, "item");
    EXPECT_EQ(repeat.repeat_index_name, "index");
    EXPECT_EQ(shape(repeat.repeat_source), "items");
    ASSERT_TRUE(repeat.repeat_body != nullptr);
    EXPECT_EQ(repeat.repeat_body->element_type, "Label");
}

TEST(Parser, ReadsARepeatWithNoIndexName) {
    // arrange, act
    const SourceFile file = parse("component C { Repeat(item in items) { Label {} } }");
    // assert
    const TreeNode& repeat = *file.components.at(0).root;
    EXPECT_TRUE(repeat.repeat_index_name.empty());
    EXPECT_EQ(repeat.repeat_item_name, "item");
}

TEST(Parser, ReportsARepeatWithNoBody) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { Repeat(item in items) {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsACollectionAppendStatement) {
    // arrange, act
    const SourceFile file = parse("component C { inout property<vector<string>> items; Button { on_clicked: { items.append(\"x\"); } } }");
    // assert
    const HandlerStatement& statement = file.components.at(0).root->handlers.at(0).body.at(0);
    EXPECT_EQ(statement.kind, HandlerStatementKind::PropertyMethod);
    EXPECT_EQ(statement.target, "items");
    EXPECT_EQ(statement.method, "append");
    ASSERT_EQ(statement.arguments.size(), 1U);
}

TEST(Parser, ReadsAClearStatementWithNoArgument) {
    // arrange, act
    const SourceFile file = parse("component C { inout property<vector<int>> items; Button { on_clicked: { items.clear(); } } }");
    // assert
    const HandlerStatement& statement = file.components.at(0).root->handlers.at(0).body.at(0);
    EXPECT_EQ(statement.kind, HandlerStatementKind::PropertyMethod);
    EXPECT_EQ(statement.method, "clear");
    EXPECT_TRUE(statement.arguments.empty());
}

TEST(Parser, ReadsASetStatementWithTwoArguments) {
    // arrange, act
    const SourceFile file = parse("component C { inout property<vector<string>> items; Button { on_clicked: { items.set(0, \"y\"); } } }");
    // assert
    const HandlerStatement& statement = file.components.at(0).root->handlers.at(0).body.at(0);
    EXPECT_EQ(statement.kind, HandlerStatementKind::PropertyMethod);
    EXPECT_EQ(statement.method, "set");
    ASSERT_EQ(statement.arguments.size(), 2U);
}

TEST(Parser, ReadsEachLiteralKind) {
    // arrange
    const std::string_view source = "component C { B { a: 42; b: 1.5; c: \"text\"; d: #FF0000; e: 12px; f: true; } }";
    // act
    const SourceFile file = parse(source);
    // assert
    const std::vector<PropertyAssignment>& assignments = file.components.at(0).root->assignments;
    ASSERT_EQ(assignments.size(), 6U);
    EXPECT_EQ(as_literal(assignments.at(0).value).kind, LiteralExpression::Kind::Integer);
    EXPECT_EQ(as_literal(assignments.at(1).value).kind, LiteralExpression::Kind::Float);
    EXPECT_EQ(as_literal(assignments.at(2).value).kind, LiteralExpression::Kind::String);
    EXPECT_EQ(as_literal(assignments.at(3).value).kind, LiteralExpression::Kind::Color);
    EXPECT_EQ(as_literal(assignments.at(4).value).kind, LiteralExpression::Kind::Length);
    EXPECT_EQ(as_literal(assignments.at(5).value).kind, LiteralExpression::Kind::Boolean);
}

TEST(Parser, ReadsABooleanLiteralByName) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: true; b: false; } }");
    // assert
    const std::vector<PropertyAssignment>& assignments = file.components.at(0).root->assignments;
    EXPECT_EQ(as_literal(assignments.at(0).value).text, "true");
    EXPECT_EQ(as_literal(assignments.at(1).value).text, "false");
}

TEST(Parser, ReadsAPropertyReferenceAsAnIdentifier) {
    // arrange, act
    const SourceFile file = parse("component C { B { text: user_name; } }");
    // assert
    const PropertyAssignment& assignment = file.components.at(0).root->assignments.at(0);
    EXPECT_EQ(as_property(assignment.value).name, "user_name");
}

TEST(Parser, ReadsATranslationMacroAsItsOwnForm) {
    // arrange, act
    const SourceFile file = parse("component C { B { text: @tr(\"Hello\"); } }");
    // assert
    const PropertyAssignment& assignment = file.components.at(0).root->assignments.at(0);
    // the macro is a distinct form, which is how the catalog extractor finds every translatable string (§9.2)
    EXPECT_EQ(as_translation(assignment.value).text, "Hello");
}

TEST(Parser, ReportsATranslationOfAComputedValue) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { text: @tr(name); } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0202");
}

TEST(Parser, ReportsAnUnknownMacroAsACallThatIsNotAConversion) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { text: @unknown(\"x\"); } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsEachBuiltinConversion) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: string(1); b: int(\"1\"); c: float(1); d: bool(1); } }");
    // assert
    const std::vector<PropertyAssignment>& assignments = file.components.at(0).root->assignments;
    ASSERT_EQ(assignments.size(), 4U);
    EXPECT_EQ(as_conversion(assignments.at(0).value).target_type, "string");
    EXPECT_EQ(as_conversion(assignments.at(1).value).target_type, "int");
    EXPECT_EQ(as_conversion(assignments.at(2).value).target_type, "float");
    EXPECT_EQ(as_conversion(assignments.at(3).value).target_type, "bool");
}

TEST(Parser, ReportsACallThatIsNotABuiltinConversion) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { text: widget(1); } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReadsAUnaryApplicationAndAGroupingAsTheirOwnForms) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: !(1 + 2); } }");
    // assert
    const ExpressionPtr& value = file.components.at(0).root->assignments.at(0).value;
    ASSERT_NE(dynamic_cast<UnaryExpression*>(value.get()), nullptr);
    const auto& unary = static_cast<const UnaryExpression&>(*value);
    ASSERT_NE(dynamic_cast<const GroupingExpression*>(unary.value.get()), nullptr);
}

TEST(Parser, ReadsAGroupingAsItsOwnForm) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: (1 + 2) * 3; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "(((1 + 2)) * 3)");
}

TEST(Parser, ReadsAGroupingThatIsNeverClosed) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { a: (1 + 2; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, AdditiveOperatorsBindLooserThanMultiplicative) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 + 2 * 3 - 4; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 + (2 * 3)) - 4)");
}

TEST(Parser, MultiplicativeOperatorsGroupToTheLeft) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 * 2 / 3 % 4; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "(((1 * 2) / 3) % 4)");
}

TEST(Parser, ComparisonBindsLooserThanAdditive) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 + 2 < 3 * 4 >= 5; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "(((1 + 2) < (3 * 4)) >= 5)");
}

TEST(Parser, EqualityBindsLooserThanComparison) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 < 2 == 3 > 4; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 < 2) == (3 > 4))");
}

TEST(Parser, EqualityChainsToTheLeft) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 == 2 == 3; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 == 2) == 3)");
}

TEST(Parser, ConjunctionBindsLooserThanEquality) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 == 2 && 3 != 4; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 == 2) && (3 != 4))");
}

TEST(Parser, DisjunctionBindsLooserThanConjunction) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 && 2 || 3 && 4; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 && 2) || (3 && 4))");
}

TEST(Parser, ConjunctionAndDisjunctionGroupToTheLeft) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 && 2 && 3 || 4; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "(((1 && 2) && 3) || 4)");
}

TEST(Parser, ReadsANegationBeforeAnyBinaryOperator) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: !enabled && 1; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "(!enabled && 1)");
}

TEST(Parser, ReadsAChainOfPrefixOperatorsToTheRight) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: !!enabled; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "!!enabled");
}

TEST(Parser, ReadsANumericNegationOfALiteral) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: -1; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "-1");
}

TEST(Parser, ReadsAConversionOfAComputedValue) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: string(click_count + 1); } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "string((click_count + 1))");
}

TEST(Parser, ReadsATranslationConcatenatedWithAProperty) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: @tr(\"Hello, \") + user_name + \"!\"; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((@tr(Hello, ) + user_name) + !)");
}

TEST(Parser, ReportsAUnitThatCannotStartAValue) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { B { a: ; } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsAMissingPropertyName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { in property<int>; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsAMissingComponentName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component { }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsAUnitThatCannotStartAComponentMember) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { 42 }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsAUnitThatCannotStartAnElementMember) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { Column { 42 } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsAUnitThatCannotStartTheFile) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("42", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsTheEndOfFileWhereAUnitWasExpected) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("import albor", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_NE(first(diagnostics).message.find("the end of the file"), std::string::npos);
}

TEST(Parser, RecoversAtTheNextDeclarationAfterABrokenOne) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component Broken { 42 }\ncomponent Good { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 2U);
    EXPECT_EQ(file.components.at(0).name, "Broken");
    // the following component is parsed whole, because the broken one consumed only its own body (§11.1)
    EXPECT_EQ(file.components.at(1).name, "Good");
}

TEST(Parser, RecoversAtTheNextDeclarationAfterABrokenImport) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("42 component Good { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "Good");
    // the recovered component is whole, because nothing inside it was malformed (§11.1)
    ASSERT_TRUE(file.components.at(0).root != nullptr);
    EXPECT_EQ(file.components.at(0).root->element_type, "Label");
}

TEST(Parser, RecoverySkipsTheBadMemberAndKeepsReadingTheBody) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { Column { 42 Label {} } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_TRUE(file.components.at(0).root != nullptr);
    ASSERT_EQ(file.components.at(0).root->children.size(), 1U);
    EXPECT_EQ(file.components.at(0).root->children.at(0)->element_type, "Label");
}

TEST(Parser, RecoveryStepsOverNestedBracesToTheNextDeclaration) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { Column { 42 { } } } component Other { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 2U);
    EXPECT_EQ(file.components.at(0).name, "C");
    EXPECT_EQ(file.components.at(1).name, "Other");
}

TEST(Parser, RecoveryStopsAtTheEndOfTheFile) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { Column { 42", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
}

TEST(Parser, DropsAComponentWhoseNameCouldNotBeRead) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_TRUE(file.components.empty());
}

TEST(Parser, TerminatesOnAStreamThatCarriesNoEndUnit) {
    // arrange
    const std::vector<Token> tokens = {Token{.kind = Token::Kind::Identifier, .text = "import", .value = "import", .span = SourceSpan{}}};
    DiagnosticList diagnostics;
    // act
    const SourceFile file = Parser(tokens, diagnostics).parse();
    // assert
    ASSERT_EQ(file.imports.size(), 1U);
    EXPECT_TRUE(file.imports.at(0).module.empty());
    EXPECT_GE(diagnostics.count(), 1U);
}

TEST(Parser, TerminatesOnAnEmptyStream) {
    // arrange
    const std::vector<Token> tokens;
    DiagnosticList diagnostics;
    // act
    const SourceFile file = Parser(tokens, diagnostics).parse();
    // assert
    EXPECT_TRUE(file.imports.empty());
    EXPECT_TRUE(file.components.empty());
    EXPECT_EQ(diagnostics.count(), 0U);
    // an empty stream has no construct to close, so no position was ever asked of it (§11.1)
    EXPECT_EQ(tokens.size(), 0U);
}

TEST(Parser, ReportsAMissingComponentKeywordPart) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { in int n; }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
    EXPECT_EQ(file.components.size(), 1U);
}

TEST(Parser, ReportsASignalParameterThatIsNotAName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { signal moved(float); }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsASignalParameterWhoseTypeIsNotAName) {
    // arrange
    DiagnosticList diagnostics;
    // act
    parse("component C { signal moved(42 time); }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0203");
}

TEST(Parser, ReportsAHandlerWrittenWithoutTheOnPrefix) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { B { clicked: { count = 1; } } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    EXPECT_EQ(first(diagnostics).code, "E0204");
    // the handler is still attached to the name the author wrote, so the reader can see what was meant (§11.1)
    ASSERT_EQ(file.components.at(0).root->handlers.size(), 1U);
    EXPECT_EQ(file.components.at(0).root->handlers.at(0).name, "clicked");
}

TEST(Parser, ReadsTheLessEqualAndGreaterEqualForms) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 <= 2 >= 3; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 <= 2) >= 3)");
}

TEST(Parser, ReadsTheLessAndGreaterForms) {
    // arrange, act
    const SourceFile file = parse("component C { B { a: 1 < 2 > 3; } }");
    // assert
    EXPECT_EQ(shape(file.components.at(0).root->assignments.at(0).value), "((1 < 2) > 3)");
}

TEST(Parser, RecoveryStepsOutOfANestedBodyWithoutEndingTheComponent) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { Column { 42 { } } Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    // the outer body is still open, so the element after the broken one is a second root rather than a lost member (§11.1)
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "C");
}

TEST(Parser, ReportsASignalWhoseParameterListRunsToTheEnd) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { signal moved(", diagnostics);
    // assert
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).signals.at(0).name, "moved");
    EXPECT_GE(diagnostics.count(), 1U);
}

TEST(Parser, ReportsAnArgumentListThatRunsToTheEnd) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { B { on_x: { moved(1 } }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
}

TEST(Parser, ReportsAnArgumentListThatEndsAtTheEndOfTheStream) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { B { on_x: { moved(", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    // the invocation carries no argument, because none was ever read (§11.1)
    ASSERT_EQ(file.components.at(0).root->handlers.size(), 1U);
    ASSERT_EQ(file.components.at(0).root->handlers.at(0).body.size(), 1U);
    EXPECT_TRUE(file.components.at(0).root->handlers.at(0).body.at(0).arguments.empty());
}

TEST(Parser, ReportsAHandlerBodyThatRunsToTheEnd) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("component C { B { on_x: { count = 1;", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
}

TEST(Parser, RecoverySkipsANestedDeclarationKeywordInsideABrokenBody) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("42 { component Nested { Label {} } } component Good { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "Good");
}

TEST(Parser, RecoveryStopsAtAnImportThatFollowsABrokenDeclaration) {
    // arrange
    DiagnosticList diagnostics;
    // act
    const SourceFile file = parse("42 import albor.controls; component Good { Label {} }", diagnostics);
    // assert
    ASSERT_GE(diagnostics.count(), 1U);
    ASSERT_EQ(file.imports.size(), 1U);
    EXPECT_EQ(file.imports.at(0).module, "albor.controls");
    ASSERT_EQ(file.components.size(), 1U);
    EXPECT_EQ(file.components.at(0).name, "Good");
}

TEST(Parser, ReadsSeveralDeclarationsInOneFile) {
    // arrange, act
    const SourceFile file = parse("import a; import b.c; component A { Label {} } component B { Row {} }");
    // assert
    EXPECT_EQ(file.imports.size(), 2U);
    ASSERT_EQ(file.components.size(), 2U);
    EXPECT_EQ(file.components.at(0).name, "A");
    EXPECT_EQ(file.components.at(1).name, "B");
}

TEST(Parser, SpansCoverTheConstructsTheyBelongTo) {
    // arrange, act
    const SourceFile file = parse(MAIN_VIEW);
    // assert
    const ComponentDeclaration& component = file.components.at(0);
    // the component span runs from its keyword to its closing brace (§11.1)
    EXPECT_EQ(component.span.start.line, 4U);
    EXPECT_GE(component.span.end.line, component.span.start.line);
    // the root element span runs from its type name to its closing brace (§11.1)
    EXPECT_EQ(component.root->span.start.line, 10U);
    // a binding value span covers the expression it holds (§11.1)
    const TreeNode* label = component.root->children.at(0).get();
    ASSERT_EQ(label->assignments.size(), 2U);
    EXPECT_EQ(label->assignments.at(0).value->location().start.line, 15U);
    // a handler span runs from its written name to the end of its body (§11.1)
    const TreeNode* row = component.root->children.at(1).get();
    ASSERT_EQ(row->children.size(), 2U);
    const TreeNode* button = row->children.at(0).get();
    ASSERT_EQ(button->handlers.size(), 1U);
    EXPECT_EQ(button->handlers.at(0).span.start.line, 25U);
    EXPECT_EQ(button->handlers.at(0).span.end.line, 28U);
    EXPECT_EQ(button->handlers.at(0).span.end.line, 28U);
}

TEST(Parser, CommentsAndWhitespaceDoNotChangeTheTree) {
    // arrange, act
    const SourceFile file = parse("component C { /* a */ Column { // a line\n spacing: 12px; } }");
    // assert
    const TreeNode& root = *file.components.at(0).root;
    ASSERT_EQ(root.assignments.size(), 1U);
    EXPECT_EQ(root.assignments.at(0).name, "spacing");
}

TEST(Parser, ReadsABracketedListAsOneExpression) {
    // arrange a list of numbers, which is the only way an author writes a collection (§8.1)
    const SourceFile file = parse("component C { in property<vector<int>> items: [1, 2, 3]; Column {} }");
    // act
    const ExpressionPtr& value = file.components.at(0).properties.at(0).default_value;
    // assert the brackets produced one expression rather than three, because a list is a single value (§8.1)
    const ArrayExpression& array = dynamic_cast<const ArrayExpression&>(*value);
    // assert each element was kept in written order (§8.1)
    ASSERT_EQ(array.elements.size(), 3U);
    EXPECT_EQ(dynamic_cast<const LiteralExpression&>(*array.elements.at(0)).text, "1");
    EXPECT_EQ(dynamic_cast<const LiteralExpression&>(*array.elements.at(2)).text, "3");
}

TEST(Parser, ReadsAnEmptyListAsACollectionOfNothing) {
    // arrange an empty list, which is a value with no elements (§8.1)
    const SourceFile file = parse("component C { in property<vector<int>> items: []; Column {} }");
    // act
    const ArrayExpression& array = dynamic_cast<const ArrayExpression&>(*file.components.at(0).properties.at(0).default_value);
    // assert
    EXPECT_TRUE(array.elements.empty());
}

TEST(Parser, AllowsATrailingSeparatorInAList) {
    // arrange a list with a trailing comma, which is how an author writes a growing list (§8.1)
    const SourceFile file = parse("component C { in property<vector<int>> items: [1, 2,]; Column {} }");
    // act
    const ArrayExpression& array = dynamic_cast<const ArrayExpression&>(*file.components.at(0).properties.at(0).default_value);
    // assert the separator added no element, because it ends the list (§8.1)
    ASSERT_EQ(array.elements.size(), 2U);
    EXPECT_EQ(dynamic_cast<const LiteralExpression&>(*array.elements.at(1)).text, "2");
}

TEST(Parser, ReadsANestedListAsAListOfLists) {
    // arrange a list holding a list, whose element is itself bracketed (§8.1)
    const SourceFile file = parse("component C { in property<vector<vector<int>>> grid: [[1], [2]]; Column {} }");
    // act
    const ArrayExpression& outer = dynamic_cast<const ArrayExpression&>(*file.components.at(0).properties.at(0).default_value);
    // assert the inner brackets produced collections rather than being consumed by the outer list (§8.1)
    ASSERT_EQ(outer.elements.size(), 2U);
    EXPECT_NE(dynamic_cast<const ArrayExpression*>(&*outer.elements.at(0)), nullptr);
}

TEST(Parser, ReportsAListWhoseClosingBracketIsMissing) {
    // arrange diagnostics, because a list is delimited and a missing bracket is a defect (§8.1)
    DiagnosticList diagnostics;
    // act the source opens a list and never closes it (§8.1)
    parse("component C { in property<vector<int>> items: [1, 2; Column {} }", diagnostics);
    // assert the problem is reported rather than the list absorbing the rest of the file (§11.1)
    EXPECT_TRUE(diagnostics.count() > 0U);
}
