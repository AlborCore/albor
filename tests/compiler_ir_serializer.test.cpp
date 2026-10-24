#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "compiler/diagnostic.h"
#include "compiler/ir_serializer.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"

using namespace albor;
using namespace albor::compiler;

namespace
{
    // the panel the serializer cases read, kept as one constant because a call never wraps (§11.1)
    constexpr std::string_view PANEL = R"(
component Panel {
    Column {
        spacing: 12px;
        Label {
            id: title;
            text: "Hello";
            font_size: 20px;
            foreground: #FF0000;
            visible: true;
        }
    }
}
)";

    // the binding case, which the IR cannot carry (§12.1)
    constexpr std::string_view BINDING = R"(
component Panel {
    in property<string> title: "x";
    Label { text: title; }
}
)";

    // the negation case, whose sign is part of the value (§11.1)
    constexpr std::string_view NEGATIVE = "component Panel { Column { spacing: -4px; } }";

    // the physical length case, which has no build time value (§11.1, AD-27)
    constexpr std::string_view PHYSICAL = "component Panel { Column { spacing: 4phx; } }";

    // the translucent colour case, whose alpha the reader does not take (§12.1)
    constexpr std::string_view TRANSLUCENT = "component Panel { Label { foreground: #FF000080; } }";

    // the numeric case, whose integer and float literals are read as written (§12.1)
    constexpr std::string_view NUMERIC = "component Panel { Column { Slider { value: 2; } Slider { value: 1.5; } } }";

    // the grouping case, which is transparent to the value (§11.1)
    constexpr std::string_view GROUPED = "component Panel { Column { spacing: (12px); } }";

    // the newline case, which a line oriented format cannot carry (§12.1)
    constexpr std::string_view NEWLINE = "component Panel { Label { text: \"a\\nb\"; } }";

    // the image case, whose marked spelling tells a reader to register the path as an asset (§12.1, §6.4)
    constexpr std::string_view IMAGE = "component Panel { Label { icon: @image(\"icons/icon.png\"); } }";

    // the grouped image case, whose parentheses do not change the value (§11.1)
    constexpr std::string_view GROUPED_IMAGE = "component Panel { Label { icon: (@image(\"icon.png\")); } }";

    // an image path with a newline cannot survive the line oriented format (§12.1)
    constexpr std::string_view NEWLINE_IMAGE = "component Panel { Label { icon: @image(\"a\\nb\"); } }";

    // an image path with a carriage return cannot survive it either (§12.1)
    constexpr std::string_view CARRIAGE_IMAGE = "component Panel { Label { icon: @image(\"a\rb\"); } }";

    // the conditional case, which has no IR form, so the component must be refused (§12.1)
    constexpr std::string_view CONDITIONAL = "component Panel { in property<bool> show: true; Column { if show { Label {} } } }";

    // the repeat case, which has no IR form, so the component must be refused (§12.1)
    constexpr std::string_view REPEAT = "component Panel { in property<vector<int>> items; Column { Repeat(item in items) { Label { text: \"x\"; } } } }";

    // the slot case, which marks a content point no snapshot carries (§12.1)
    constexpr std::string_view SLOT = "component Panel { Column { @children } }";

    // the handler case, which is compiled code rather than a value the IR can carry (§12.1)
    constexpr std::string_view HANDLER = "component Panel { Button { text: \"Run\"; on_clicked: { } } }";

    // scan and parse a source, which is what a serializer test needs before it can serialize (§11.1)
    SourceFile parse(std::string_view source, DiagnosticList& diagnostics) {
        // the scanner runs first, because the parser takes units (§11.1)
        const std::vector<Token> tokens = Lexer(source, diagnostics).tokenize();
        // the parser builds the untyped tree, which the serializer reads (§11.1)
        return Parser(tokens, diagnostics).parse();
    }
} // namespace

TEST(IrSerializerChecks, SerializesElementsIdsAndStaticProperties) {
    // arrange a component whose tree carries a static literal of every kind (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(PANEL, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    ASSERT_EQ(file.components.size(), 1u);
    // act the component is serialized into the dev IR (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the element and its converted property are what the reader parses (§12.1)
    ASSERT_TRUE(ir.has_value());
    EXPECT_EQ(ir->element, "Column");
    ASSERT_EQ(ir->properties.size(), 1u);
    EXPECT_EQ(ir->properties.front().name, "spacing");
    EXPECT_EQ(ir->properties.front().value, "12");
    // assert the child carries its element, identity and converted properties (§12.1, AD-11)
    ASSERT_EQ(ir->children.size(), 1u);
    const devrt::IrNode& label = ir->children.front();
    EXPECT_EQ(label.element, "Label");
    EXPECT_EQ(label.id, "title");
    // the properties are looked up by name, because the writer preserves written order (§12.1)
    auto value_of = [&label](std::string_view name) -> std::string {
        // each property is compared by the name the author wrote (§12.1)
        for (const devrt::IrProperty& property : label.properties) {
            // the matching property answers with its value (§12.1)
            if (property.name == name)
                return property.value;
        }
        // a property the definition does not carry answers nothing (§12.1)
        return {};
    };
    // the string is decoded, the length loses its unit, and the colour becomes channels (§12.1, AD-27)
    EXPECT_EQ(value_of("text"), "Hello");
    EXPECT_EQ(value_of("font_size"), "20");
    EXPECT_EQ(value_of("foreground"), "1, 0, 0");
    EXPECT_EQ(value_of("visible"), "true");
}

TEST(IrSerializerChecks, DropsABindingBecauseTheIrHasNoBindingForm) {
    // arrange a component whose label reads a property rather than a literal (§11.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(BINDING, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act the component is serialized, and a binding is not representable (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the element survives while its binding is dropped, which the IR format cannot carry (§12.1)
    ASSERT_TRUE(ir.has_value());
    EXPECT_EQ(ir->element, "Label");
    EXPECT_TRUE(ir->properties.empty());
}

TEST(IrSerializerChecks, ANegativeLengthKeepsItsSign) {
    // arrange a length written with a sign, which the reader parses as part of the number (§11.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(NEGATIVE, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the sign survives with the unit dropped, which is what the reader parses (§12.1, AD-27)
    ASSERT_TRUE(ir.has_value());
    ASSERT_EQ(ir->properties.size(), 1u);
    EXPECT_EQ(ir->properties.front().value, "-4");
}

TEST(IrSerializerChecks, APhysicalLengthIsRefusedRatherThanAppliedWrong) {
    // arrange a physical length, which has no build time value (§11.1, AD-27)
    DiagnosticList diagnostics;
    const SourceFile file = parse(PHYSICAL, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the property is dropped rather than written in a unit the reader cannot parse (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    ASSERT_TRUE(ir.has_value());
    EXPECT_TRUE(ir->properties.empty());
}

TEST(IrSerializerChecks, ATranslucentColourIsRefusedRatherThanAppliedOpaque) {
    // arrange a colour with an alpha, which the reader does not take (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(TRANSLUCENT, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the property is dropped rather than applied opaque, which would change the picture (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    ASSERT_TRUE(ir.has_value());
    EXPECT_TRUE(ir->properties.empty());
}

TEST(IrSerializerChecks, AConditionalRefusesTheWholeComponent) {
    // arrange a tree with a conditional, which the IR cannot half represent (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(CONDITIONAL, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the whole component is refused, because a partial definition would drop live content on apply (§12.1)
    EXPECT_FALSE(serialize_component(file.components.front()).has_value());
}

TEST(IrSerializerChecks, ARepeatRefusesTheWholeComponent) {
    // arrange a tree with a repeat, which the IR cannot half represent (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(REPEAT, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the whole component is refused, because a partial definition would drop live rows (§12.1)
    EXPECT_FALSE(serialize_component(file.components.front()).has_value());
}

TEST(IrSerializerChecks, ASlotRefusesTheWholeComponent) {
    // arrange a tree with a content slot, which the IR cannot half represent (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(SLOT, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the whole component is refused, because a partial definition would drop content (§12.1)
    EXPECT_FALSE(serialize_component(file.components.front()).has_value());
}

TEST(IrSerializerChecks, AHandlerIsDroppedBecauseTheIrCarriesNoCode) {
    // arrange a control whose handler is compiled code beside a literal (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(HANDLER, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act the component is serialized, and a handler is not a value the IR carries (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the literal survives while the handler is absent, because the node keeps its compiled handler (§12.1)
    ASSERT_TRUE(ir.has_value());
    ASSERT_EQ(ir->properties.size(), 1u);
    EXPECT_EQ(ir->properties.front().name, "text");
    EXPECT_EQ(ir->properties.front().value, "Run");
}

TEST(IrSerializerChecks, AComponentWithoutARootSerializesToNothing) {
    // arrange a component that declares no root element (§11.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse("component Empty { in property<int> value: 0; }", diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert a component with no tree has nothing to serialize (§12.1)
    EXPECT_FALSE(serialize_component(file.components.front()).has_value());
}

TEST(IrSerializerChecks, ANumericLiteralKeepsItsWrittenForm) {
    // arrange integer and float literals, which the reader parses as written (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(NUMERIC, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert both numeric kinds survive, because the reader parses a bare number (§12.1)
    ASSERT_TRUE(ir.has_value());
    ASSERT_EQ(ir->children.size(), 2u);
    EXPECT_EQ(ir->children[0].properties.front().value, "2");
    EXPECT_EQ(ir->children[1].properties.front().value, "1.5");
}

TEST(IrSerializerChecks, AGroupedLiteralIsTransparent) {
    // arrange a length wrapped in parentheses, which does not change the value (§11.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(GROUPED, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the grouped literal is unwrapped, because the parentheses are not a value (§11.1)
    ASSERT_TRUE(ir.has_value());
    ASSERT_EQ(ir->properties.size(), 1u);
    EXPECT_EQ(ir->properties.front().value, "12");
}

TEST(IrSerializerChecks, AStringWithANewlineIsRefused) {
    // arrange a string whose decoded value carries a newline, which the line oriented format cannot hold (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(NEWLINE, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the property is dropped rather than emitting a malformed line (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    ASSERT_TRUE(ir.has_value());
    EXPECT_TRUE(ir->properties.empty());
}

TEST(IrSerializerChecks, AnEmbeddedImageKeepsItsAssetMarker) {
    // arrange an image path, which the dev runtime must not mistake for a plain string (§12.1, §6.4)
    DiagnosticList diagnostics;
    const SourceFile file = parse(IMAGE, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the marker and the path survive, so a reader can register the asset (§12.1, §6.4)
    ASSERT_TRUE(ir.has_value());
    ASSERT_EQ(ir->properties.size(), 1u);
    EXPECT_EQ(ir->properties.front().name, "icon");
    EXPECT_EQ(ir->properties.front().value, std::string(devrt::IMAGE_VALUE_PREFIX) + "icons/icon.png");
}

TEST(IrSerializerChecks, AGroupedEmbeddedImageKeepsItsAssetMarker) {
    // arrange an image wrapped in parentheses, which does not change the value (§11.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(GROUPED_IMAGE, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    // assert the grouping is transparent and the marker survives (§11.1, §6.4)
    ASSERT_TRUE(ir.has_value());
    ASSERT_EQ(ir->properties.size(), 1u);
    EXPECT_EQ(ir->properties.front().value, std::string(devrt::IMAGE_VALUE_PREFIX) + "icon.png");
}

TEST(IrSerializerChecks, AnImagePathWithANewlineIsRefused) {
    // arrange an image path whose decoded value carries a newline, which the format cannot hold (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(NEWLINE_IMAGE, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the property is dropped rather than emitting a malformed line (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    ASSERT_TRUE(ir.has_value());
    EXPECT_TRUE(ir->properties.empty());
}

TEST(IrSerializerChecks, AnImagePathWithACarriageReturnIsRefused) {
    // arrange an image path whose decoded value carries a carriage return, which the format cannot hold (§12.1)
    DiagnosticList diagnostics;
    const SourceFile file = parse(CARRIAGE_IMAGE, diagnostics);
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    // act, assert the property is dropped rather than splitting the record (§12.1)
    const std::optional<devrt::IrNode> ir = serialize_component(file.components.front());
    ASSERT_TRUE(ir.has_value());
    EXPECT_TRUE(ir->properties.empty());
}
