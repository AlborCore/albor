#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>

#include <gtest/gtest.h>

#include "compiler/diagnostic.h"
#include "compiler/ir_serializer.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "core/dev_hook.h"
#include "devrt/devrt.h"
#include "devrt/element_table.h"
#include "devrt/ir.h"
#include "devrt/reload_adapter.h"
#include "dual_engine_controls.h"
#include "dual_engine_panel.h"
#include "dual_engine_tabs.h"
#include "layout/column_layout.h"
#include "layout/row_layout.h"
#include "runtime/binding_engine.h"
#include "text/text_engine.h"
#include "ui/ui_node.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/slider.h"
#include "widgets/tab_page.h"
#include "widgets/tab_title.h"
#include "widgets/tab_view.h"

using namespace albor;
using namespace albor::devrt;

namespace
{
    // the engine double that measures to an empty layout, which keeps these tests off any shaper (§9.1)
    class TestTextEngine final : public TextEngine
    {
    public:
        // override: hand out the empty layout, because a reload never draws (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view, float) const override { return {}; }

        // override: hand out the empty layout at the explicit size, because a reload never draws (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view, float, float) const override { return {}; }

        // override: report the engine usable, because a widget checks readiness before it measures (§9.1)
        [[nodiscard]] bool ready() const override { return true; }
    };

    // the engines and the context a reload builds nodes over, which is what the runtime binds (§12.1, AD-02)
    struct ReloadFixture
    {
        // the reactive engine the built nodes declare their properties over (§7.3)
        BindingEngine engine;

        // the text engine the built nodes measure with, kept because a widget borrows it for its whole life (§9.1)
        TestTextEngine text;

        // the context carries the engines and the closed builtin set, which is the registry a Release lacks (§8, AD-01)
        ReloadContext context;

        ReloadFixture() {
            context.engine = &engine;
            context.text = &text;
            context.factory = [](std::string_view element, BindingEngine& engine, TextEngine& text) { return create_node(element, engine, text); };
            context.element_of = [](const UiNode& node) { return element_of(node); };
        }
    };

    // find a descendant carrying an id, which is how a test asserts on a patch (§12.1, AD-11)
    UiNode* find_by_id(UiNode& root, const std::string& id) {
        // the node the caller wants is recorded during the walk, because visit cannot return a value (§12.1)
        UiNode* found = nullptr;
        // the walk visits every node, which is what a deep search needs (§12.1)
        root.visit([&](UiNode& node) {
            // the first match wins, because ids are unique by the author's contract (§18.1, AD-11)
            if (found == nullptr && node.identity().explicit_id == id)
                found = &node;
        });
        // the node is returned, or nothing when no node carries the id (§12.1)
        return found;
    }

    // the value one property holds, chosen by the name because the type is what the setter reads (§11.1)
    std::string value_for(const std::string& property) {
        // a name outside the declared set is a table lie, so the test fails loudly rather than reading a default (§11.1)
        if (property == "text" || property == "placeholder")
            return "hello";
        // a float is any number the language writes, and the spacing is trimmed rather than refused (§11.1)
        if (property == "spacing" || property == "padding" || property == "font_size" || property == "overscan" || property == "row_height")
            return "12";
        // a colour is three channel floats, which is how the language writes one (§11.1)
        if (property == "foreground" || property == "background" || property == "outline" || property == "check_fill" || property == "track_color" || property == "thumb_color")
            return "0.1,0.2,0.3";
        // a scrollbar policy is a named member of §8 (§11.1)
        if (property == "scrollbar")
            return "as_needed";
        // a slider value is a plain number in the unit range (§11.1)
        if (property == "value")
            return "0.5";
        // everything left is a flag, which the language writes as one of two words (§11.1)
        return "true";
    }

    // count every node of a tree, which is how a test asserts on additions and removals (§12.1)
    std::size_t count_nodes(UiNode& root) {
        // the count starts at zero, because the walk visits the root among the rest (§8.1)
        std::size_t count = 0;
        // the tree is walked, because a patch may add at any depth, and the walk includes the root (§8.1)
        root.visit([&](UiNode&) { count++; });
        // the count is returned whole, which is what a test asserts on (§12.1)
        return count;
    }
} // namespace

TEST(DevIrChecks, AnIndentedDefinitionReadsAsATree) {
    // arrange a definition with a root, a property and two children (§12.1)
    const std::string text = "node Panel id=root\n"
                             "  prop spacing 12\n"
                             "  node Label id=title\n"
                             "  node Button id=ok\n";
    // act
    const std::optional<IrNode> parsed = parse_ir(text);
    // assert the structure the indentation encoded (§12.1)
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->element, "Panel");
    EXPECT_EQ(parsed->id, "root");
    ASSERT_EQ(parsed->properties.size(), 1U);
    EXPECT_EQ(parsed->properties.at(0).name, "spacing");
    ASSERT_EQ(parsed->children.size(), 2U);
    EXPECT_EQ(parsed->children.at(0).id, "title");
}

TEST(DevIrChecks, ADeepDefinitionNestsToItsDepth) {
    // arrange a definition three levels deep, which is where a shallow reader would flatten it (§12.1)
    const std::string text = "node A id=a\n  node B id=b\n    node C id=c\n";
    // act
    const std::optional<IrNode> parsed = parse_ir(text);
    // assert each level sits under the one above it (§12.1)
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->children.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).children.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).children.at(0).id, "c");
}

TEST(DevIrChecks, ADefinitionRoundTripsThroughTheFormat) {
    // arrange
    IrNode root;
    // the root carries an id and a child, which exercises both fields (§12.1)
    root.element = "Panel";
    // the id is set, because identity is what a reload matches (§12.1, AD-11)
    root.id = "root";
    // the child carries a property, because that is what the format must survive (§12.1)
    root.children.push_back(IrNode{.element = "Label", .id = "title", .properties = {IrProperty{.name = "text", .value = "hello"}}, .children = {}});
    // act
    const std::optional<IrNode> parsed = parse_ir(write_ir(root));
    // assert the whole structure survived, because a reload reads what a writer produced (§12.1)
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->id, "root");
    ASSERT_EQ(parsed->children.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).id, "title");
    ASSERT_EQ(parsed->children.at(0).properties.size(), 1U);
    EXPECT_EQ(parsed->children.at(0).properties.at(0).value, "hello");
}

TEST(DevIrChecks, AMalformedDefinitionIsRefused) {
    // assert each of these describes nothing, so a malformed reload cannot touch a running tree (§12.1)
    EXPECT_FALSE(parse_ir("").has_value());
    EXPECT_FALSE(parse_ir("not a node\n").has_value());
    EXPECT_FALSE(parse_ir("node Panel id=a\nnode Other id=b\n").has_value());
}

TEST(DevElementChecks, EveryBuiltinElementBuildsOverTheEngines) {
    // arrange the engines a window owns, which a widget borrows for the whole run (§8.1, §9.1)
    BindingEngine engine;
    TestTextEngine text;
    // assert each closed element of §8 resolves, because the definition language may name any of them
    for (const char* element : {"Column", "Row", "Padding", "Stack", "Grid", "Label", "Button", "Slider", "CheckBox", "Canvas", "ScrollView", "TextField"}) {
        EXPECT_TRUE(known_element(element)) << element;
        EXPECT_NE(create_node(element, engine, text), nullptr) << element;
    }
}

TEST(DevElementChecks, AnElementOutsideTheClosedSetIsRefused) {
    // assert the registry refuses rather than guesses, because a guess would replace a subtree silently (§12.1)
    EXPECT_FALSE(known_element("Panel"));
    EXPECT_FALSE(known_element(""));
    BindingEngine engine;
    TestTextEngine text;
    EXPECT_EQ(create_node("Panel", engine, text), nullptr);
}

TEST(DevElementChecks, APropertyLandsOnTheNodeThatDeclaresIt) {
    // arrange a label over the engines, which is the element the definition names (§8, §9.1)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> label = create_node("Label", engine, text);
    ASSERT_NE(label, nullptr);
    // act the definition assigns the text the author typed (§12.1)
    EXPECT_TRUE(apply_property(*label, "Label", "text", "hello"));
    // assert the label carries it, because that is what an edited value means (§12.1)
    EXPECT_EQ(static_cast<Label*>(label.get())->text(), "hello");
}

TEST(DevElementChecks, APropertyNeverLandsOnTheWrongElement) {
    // arrange a label, which declares text but no value (§11.1)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> label = create_node("Label", engine, text);
    ASSERT_NE(label, nullptr);
    // act the definition names a property no element declares, which resolves to nothing (§11.1)
    const bool applied = apply_property(*label, "Label", "value", "3");
    // assert the assignment was refused, because a label has no value to set (§11.1)
    EXPECT_FALSE(applied);
    // assert the alignment wildcard reaches every element, because both axes are declared by all of them (§8.2)
    EXPECT_TRUE(apply_property(*label, "Label", "horizontal_align", "center"));
    EXPECT_EQ(label->horizontal_align(), Align::Center);
}

TEST(DevElementChecks, EveryDeclaredPropertyLandsOnItsOwnElement) {
    // arrange the engines a window owns, which is what a rebuilt node is built over (§8.1, §9.1)
    BindingEngine engine;
    TestTextEngine text;
    // the closed catalogue of §8, paired with a value each property holds, which is what a definition writes (§11.1)
    const std::vector<std::pair<std::string, std::string>> assignments{
        {"Column", "spacing"}, {"Column", "padding"}, {"Row", "spacing"}, {"Grid", "spacing"}, {"Padding", "padding"}, {"Label", "text"}, {"Label", "foreground"}, {"Label", "font_size"}, {"Button", "text"}, {"Button", "background"}, {"Button", "disabled"}, {"Slider", "value"}, {"Slider", "disabled"}, {"Slider", "track_color"}, {"Slider", "thumb_color"}, {"CheckBox", "checked"}, {"CheckBox", "disabled"}, {"CheckBox", "outline"}, {"CheckBox", "check_fill"}, {"ScrollView", "overscan"}, {"ScrollView", "scrollbar"}, {"ScrollView", "row_height"}, {"TextField", "value"}, {"TextField", "placeholder"}, {"TextField", "read_only"}, {"TextField", "multiline"}, {"TextField", "disabled"}, {"TextField", "font_size"},
    };
    // assert each assignment lands, because a setter the table names but never runs is a table that lies (§11.1)
    for (const std::pair<std::string, std::string>& assignment : assignments) {
        const std::string& element = assignment.first;
        const std::string& property = assignment.second;
        std::unique_ptr<UiNode> node = create_node(element, engine, text);
        ASSERT_NE(node, nullptr) << element;
        // the value is the one the property holds, chosen per type so the setter runs rather than refuses (§11.1)
        const std::string value = value_for(property);
        EXPECT_TRUE(apply_property(*node, element, property, value)) << element << "." << property;
    }
}

TEST(DevElementChecks, EveryElementTakesBothAlignments) {
    // arrange the engines and one node per element, because both axes are declared by all of them (§8.2)
    BindingEngine engine;
    TestTextEngine text;
    for (const char* element : {"Column", "Row", "Padding", "Stack", "Grid", "Label", "Button", "Slider", "CheckBox", "Canvas", "ScrollView", "TextField"}) {
        std::unique_ptr<UiNode> node = create_node(element, engine, text);
        ASSERT_NE(node, nullptr) << element;
        // act and assert both axes land on every element, because alignment is a property of every node (§8.2)
        EXPECT_TRUE(apply_property(*node, element, "horizontal_align", "center")) << element;
        EXPECT_EQ(node->horizontal_align(), Align::Center) << element;
        EXPECT_TRUE(apply_property(*node, element, "vertical_align", "stretch")) << element;
        EXPECT_EQ(node->vertical_align(), Align::Stretch) << element;
    }
}

TEST(DevElementChecks, AnAssignmentIsTrimmedOfTheSpacingItWasWrittenWith) {
    // arrange a column, whose spacing is a float (§8.2)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> column = create_node("Column", engine, text);
    ASSERT_NE(column, nullptr);
    // act and assert the surrounding spaces are the definition's, not the value's (§11.1)
    EXPECT_TRUE(apply_property(*column, "Column", "spacing", "  8  "));
    EXPECT_TRUE(apply_property(*column, "Column", "padding", "\t4\n"));
}

TEST(DevElementChecks, EveryTypedPropertyRefusesAValueItCannotHold) {
    // arrange the engines a window owns, which is what a rebuilt node is built over (§8.1, §9.1)
    BindingEngine engine;
    TestTextEngine text;
    // the closed catalogue of the typed properties, each with a value of the wrong shape (§11.1)
    const std::vector<std::tuple<std::string, std::string, std::string>> refusals{
        {"Column", "spacing", "wide"}, {"Column", "padding", "wide"}, {"Row", "spacing", "wide"}, {"Grid", "spacing", "wide"}, {"Padding", "padding", "wide"}, {"Label", "foreground", "blue"}, {"Label", "font_size", "big"}, {"Button", "background", "blue"}, {"Button", "disabled", "yes"}, {"Slider", "value", "half"}, {"Slider", "disabled", "yes"}, {"Slider", "track_color", "blue"}, {"Slider", "thumb_color", "blue"}, {"CheckBox", "checked", "yes"}, {"CheckBox", "disabled", "yes"}, {"CheckBox", "outline", "blue"}, {"CheckBox", "check_fill", "blue"}, {"ScrollView", "overscan", "some"}, {"ScrollView", "scrollbar", "sometimes"}, {"ScrollView", "row_height", "tall"}, {"TextField", "read_only", "yes"}, {"TextField", "multiline", "yes"}, {"TextField", "disabled", "yes"}, {"TextField", "font_size", "big"}, {"Label", "horizontal_align", "middle"}, {"Label", "vertical_align", "middle"},
    };
    // assert each setter refuses rather than half applying, because a refused value leaves the node as it was (§11.1)
    for (const std::tuple<std::string, std::string, std::string>& refusal : refusals) {
        const std::string element = std::get<0>(refusal);
        const std::string property = std::get<1>(refusal);
        const std::string value = std::get<2>(refusal);
        std::unique_ptr<UiNode> node = create_node(element, engine, text);
        ASSERT_NE(node, nullptr) << element;
        EXPECT_FALSE(apply_property(*node, element, property, value)) << element << "." << property;
    }
}

TEST(DevElementChecks, AScrollbarPolicyIsReadFromItsNamedMembers) {
    // arrange a viewport, whose policy is a closed named set (§8)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> scroll = create_node("ScrollView", engine, text);
    ASSERT_NE(scroll, nullptr);
    // act and assert each member of the set lands, and a name outside it does not (§11.1)
    EXPECT_TRUE(apply_property(*scroll, "ScrollView", "scrollbar", "as_needed"));
    EXPECT_TRUE(apply_property(*scroll, "ScrollView", "scrollbar", "always_off"));
    EXPECT_TRUE(apply_property(*scroll, "ScrollView", "scrollbar", "always_on"));
    EXPECT_FALSE(apply_property(*scroll, "ScrollView", "scrollbar", "sometimes"));
}

TEST(DevElementChecks, AValueTheLanguageNeverWroteIsRefused) {
    // arrange a column, whose spacing is a float (§8.2)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> column = create_node("Column", engine, text);
    ASSERT_NE(column, nullptr);
    // act and assert each of these is text the property cannot hold, so the previous value survives (§11.1)
    EXPECT_FALSE(apply_property(*column, "Column", "spacing", "wide"));
    EXPECT_FALSE(apply_property(*column, "Column", "spacing", ""));
    EXPECT_FALSE(apply_property(*column, "Column", "spacing", "12px"));
    EXPECT_FALSE(apply_property(*column, "Column", "padding", "1,2"));
    EXPECT_FALSE(apply_property(*column, "Column", "vertical_align", "middle"));
    // a colour is read through its own reader, so its refusals are asserted on a property that is one (§11.1)
    std::unique_ptr<UiNode> label = create_node("Label", engine, text);
    ASSERT_NE(label, nullptr);
    // assert a triple that is not three numbers is not a colour, and neither is a pair of them (§11.1)
    EXPECT_FALSE(apply_property(*label, "Label", "foreground", "0.1,0.2,blue"));
    EXPECT_FALSE(apply_property(*label, "Label", "foreground", "0.1,0.2"));
    EXPECT_FALSE(apply_property(*label, "Label", "foreground", "blue"));
}

TEST(DevElementChecks, APropertyNeverLandsOnADifferentElement) {
    // arrange a label, whose dynamic type is not the element a button property belongs to (§11.1)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> label = create_node("Label", engine, text);
    ASSERT_NE(label, nullptr);
    // act and assert the button setter refuses the label, because a name alone must not carry an assignment (§11.1)
    EXPECT_FALSE(apply_property(*label, "Button", "text", "hello"));
    EXPECT_FALSE(apply_property(*label, "Button", "disabled", "true"));
    // assert the label kept its own text, which is what a refused assignment means (§11.1)
    EXPECT_EQ(static_cast<Label*>(label.get())->text(), "");
}

TEST(DevElementChecks, AColourAndAFlagAreReadFromTheDefinition) {
    // arrange the widgets that carry a colour and a flag (§8, §8.4)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> button = create_node("Button", engine, text);
    std::unique_ptr<UiNode> check = create_node("CheckBox", engine, text);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(check, nullptr);
    // act and assert the assignments reach the setters the catalogue names (§11.1)
    EXPECT_TRUE(apply_property(*button, "Button", "background", "0.1,0.2,0.3"));
    EXPECT_EQ(static_cast<Button*>(button.get())->background(), Color::rgb(0.1f, 0.2f, 0.3f));
    EXPECT_TRUE(apply_property(*button, "Button", "disabled", "true"));
    EXPECT_TRUE(button->disabled());
    EXPECT_TRUE(apply_property(*check, "CheckBox", "checked", "false"));
    EXPECT_FALSE(check->disabled());
}

TEST(DevElementChecks, ALiveNodeIsNamedByItsDynamicType) {
    // arrange one node per element, because naming is what finds a changed element (§12.1)
    BindingEngine engine;
    TestTextEngine text;
    std::unique_ptr<UiNode> label = create_node("Label", engine, text);
    std::unique_ptr<UiNode> button = create_node("Button", engine, text);
    std::unique_ptr<UiNode> column = create_node("Column", engine, text);
    ASSERT_NE(label, nullptr);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(column, nullptr);
    // assert the closed set names what it built (§12.1)
    EXPECT_EQ(element_of(*label), "Label");
    EXPECT_EQ(element_of(*button), "Button");
    EXPECT_EQ(element_of(*column), "Column");
    // assert a node outside the closed set has no element, because it is a bare tree node (§8)
    UiNode bare;
    EXPECT_TRUE(element_of(bare).empty());
}

TEST(DevReloadChecks, AFirstReloadPopulatesAnEmptyTree) {
    // arrange a tree with no children, which is what a host has before its first reload (§12.1)
    ReloadFixture fixture;
    UiNode root;
    // act
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=title\n"), fixture.context);
    // assert the child was added under the root, because the definition's root is the existing component (§12.1)
    EXPECT_EQ(report.added, 1U);
    EXPECT_EQ(count_nodes(root), 2U);
    // assert the named node is addressable, because identity is what a later edit matches (§12.1, AD-11)
    EXPECT_NE(find_by_id(root, "title"), nullptr);
}

TEST(DevReloadChecks, ASecondReloadPreservesTheNodesItMatches) {
    // arrange a tree already holding a named child (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=title\n"), fixture.context);
    // the matched node is recorded, because preserving its instance is the whole claim (§12.1, §12.2)
    UiNode* before = find_by_id(root, "title");
    ASSERT_NE(before, nullptr);
    // act the definition is unchanged, which is what an edit that changes nothing looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=title\n"), fixture.context);
    // assert the node kept its identity, which is what preserves the interaction state inside it (§12.1, §12.2)
    EXPECT_EQ(find_by_id(root, "title"), before);
    // assert nothing was added or removed, because the definition did not change (§12.1)
    EXPECT_EQ(report.added, 0U);
    EXPECT_EQ(report.removed, 0U);
    EXPECT_EQ(report.replaced, 0U);
    EXPECT_EQ(report.preserved, 1U);
}

TEST(DevReloadChecks, AMovedNodeFollowsItsIdRatherThanItsPosition) {
    // arrange two children, the second of which carries an id (§12.1, AD-11)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n  node Button id=second\n"), fixture.context);
    // the second node is recorded, because an edit will move it (§12.1, AD-11)
    UiNode* second = find_by_id(root, "second");
    ASSERT_NE(second, nullptr);
    // act the definition swaps the order, which is what a person editing a column does (§12.1)
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Button id=second\n  node Label id=first\n"), fixture.context);
    // assert the node kept its identity rather than the second slot, because the id is what identity means (§18.1, AD-11)
    EXPECT_EQ(find_by_id(root, "second"), second);
}

TEST(DevReloadChecks, AnUnidentifiedEntryIsMatchedByItsPosition) {
    // arrange a tree holding two entries the author named nothing on, which is what a plain row looks like (§18.1, AD-11)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label\n  node Button\n"), fixture.context);
    ASSERT_EQ(root.child_count(), 2U);
    // the first instance is recorded, because position is all an unnamed entry has to be matched by (§18.1, AD-11)
    UiNode* before = root.child_at(0);
    ASSERT_NE(before, nullptr);
    // act the definition is reloaded unchanged, which is what a save with no names looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label\n  node Button\n"), fixture.context);
    // assert both slots kept their instance, because an unnamed entry follows its position (§18.1, AD-11)
    EXPECT_EQ(root.child_count(), 2U);
    EXPECT_EQ(root.child_at(0), before);
    EXPECT_EQ(report.preserved, 2U);
    EXPECT_EQ(report.added, 0U);
    EXPECT_EQ(report.removed, 0U);
}

TEST(DevReloadChecks, AnUnidentifiedEntryDoesNotStealACoveredSlot) {
    // arrange a tree whose first child the author named, which an unnamed entry must not claim (§18.1, AD-11)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=named\n  node Button\n"), fixture.context);
    ASSERT_EQ(root.child_count(), 2U);
    UiNode* named = root.child_at(0);
    ASSERT_NE(named, nullptr);
    // act the author adds an unnamed entry, which takes the first slot nobody covered (§12.1, AD-11)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=named\n  node Button\n  node Label\n"), fixture.context);
    // assert the named node kept its slot and the addition landed after it (§18.1, AD-11)
    EXPECT_EQ(root.child_at(0), named);
    EXPECT_EQ(report.added, 1U);
    EXPECT_EQ(report.preserved, 2U);
}

TEST(DevReloadChecks, ARemovedNodeIsDropped) {
    // arrange two children (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n  node Button id=second\n"), fixture.context);
    ASSERT_EQ(count_nodes(root), 3U);
    // act the definition keeps only one, which is what deleting a row looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n"), fixture.context);
    // assert the other is gone, because the definition no longer wants it (§12.1)
    EXPECT_EQ(report.removed, 1U);
    EXPECT_EQ(count_nodes(root), 2U);
    EXPECT_EQ(find_by_id(root, "second"), nullptr);
}

TEST(DevReloadChecks, AnAddedNodeAppears) {
    // arrange a tree with one child (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n"), fixture.context);
    // act the definition gains a child, which is what typing a new row looks like (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n  node Button id=second\n"), fixture.context);
    // assert the addition is reported and addressable (§12.1)
    EXPECT_EQ(report.added, 1U);
    EXPECT_NE(find_by_id(root, "second"), nullptr);
}

TEST(DevReloadChecks, AReloadWithNoFactoryAddsNothingAndKeepsTheTree) {
    // arrange a tree holding one child (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n"), fixture.context);
    // act the definition gains a child, but no factory exists to build it (§12.1, AD-02)
    ReloadContext blind = fixture.context;
    blind.factory = nullptr;
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=first\n  node Button id=second\n"), blind);
    // assert nothing was invented, because the extension builds no element of its own (§12.1, AD-02)
    EXPECT_EQ(report.added, 0U);
    // assert the existing node survived, because an incomplete reload must not damage a running tree (§12.1)
    EXPECT_NE(find_by_id(root, "first"), nullptr);
}

TEST(DevReloadChecks, AReloadWithNoEnginesAddsNothing) {
    // arrange a context with the engines unset, because a bind that named none cannot build anything (§12.1, AD-02)
    ReloadFixture fixture;
    ReloadContext blind = fixture.context;
    blind.engine = nullptr;
    UiNode root;
    // act the definition names a child, which the context cannot build without the reactive engine (§8.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=title\n"), blind);
    // assert nothing was invented, because a widget with no engine has nothing to declare over (§7.3)
    EXPECT_EQ(report.added, 0U);
    EXPECT_EQ(count_nodes(root), 1U);
}

TEST(DevReloadChecks, AnEditedPropertyReachesTheLiveTree) {
    // arrange a tree holding a label, which is the element an author edits text on (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=title\n"), fixture.context);
    UiNode* title = find_by_id(root, "title");
    ASSERT_NE(title, nullptr);
    // act the definition assigns the text the author just typed (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Label id=title\n    prop text hello\n"), fixture.context);
    // assert the instance survived, because only the value changed (§12.1, §12.2)
    EXPECT_EQ(find_by_id(root, "title"), title);
    EXPECT_EQ(report.preserved, 1U);
    // assert the label carries the new text, because that is what hot swap of a value means (§12.1)
    EXPECT_EQ(static_cast<Label*>(title)->text(), "hello");
}

TEST(DevReloadChecks, AChangedElementIsRebuiltInPlace) {
    // arrange a tree holding a label, and the identity of that instance (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=slot\n"), fixture.context);
    UiNode* before = find_by_id(root, "slot");
    ASSERT_NE(before, nullptr);
    // act the author changes the element, which is a different class and cannot keep its instance (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Button id=slot\n"), fixture.context);
    // assert the rebuild is reported rather than preserved, because the instance could not be kept (§12.1)
    EXPECT_EQ(report.replaced, 1U);
    EXPECT_EQ(report.preserved, 0U);
    // assert the node is now the new element and the tree did not grow (§12.1)
    UiNode* after = find_by_id(root, "slot");
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after, before);
    EXPECT_EQ(element_of(*after), "Button");
    EXPECT_EQ(count_nodes(root), 2U);
}

TEST(DevReloadChecks, AChangedElementKeepsItsPosition) {
    // arrange three children, the middle one of which will change element (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=a\n  node Label id=b\n  node Label id=c\n"), fixture.context);
    ASSERT_EQ(root.child_count(), 3U);
    // act the middle entry changes element, which must not move the neighbours (§12.1)
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=a\n  node Button id=b\n  node Label id=c\n"), fixture.context);
    // assert the order is the one the definition declared (§12.1)
    ASSERT_EQ(root.child_count(), 3U);
    EXPECT_EQ(element_of(*root.child_at(0)), "Label");
    EXPECT_EQ(element_of(*root.child_at(1)), "Button");
    EXPECT_EQ(element_of(*root.child_at(2)), "Label");
}

TEST(DevReloadChecks, AChangedElementTheFactoryRefusesKeepsTheOldNode) {
    // arrange a tree holding a label, and a context whose factory cannot build the new element (§12.1)
    ReloadFixture fixture;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=slot\n"), fixture.context);
    UiNode* before = find_by_id(root, "slot");
    ASSERT_NE(before, nullptr);
    // act the definition names an element outside the closed set, which the registry refuses (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Panel id=slot\n"), fixture.context);
    // assert the node survived, because a refusal changes nothing (§12.1)
    EXPECT_EQ(find_by_id(root, "slot"), before);
    EXPECT_EQ(report.replaced, 0U);
    EXPECT_EQ(report.removed, 0U);
}

TEST(DevReloadChecks, AReloadWithoutAnElementResolverKeepsEveryInstance) {
    // arrange a context that cannot name a live node, because nothing resolved it (§12.1)
    ReloadFixture fixture;
    ReloadContext blind = fixture.context;
    blind.element_of = nullptr;
    UiNode root;
    (void)apply_reload(root, *parse_ir("node Column id=root\n  node Label id=slot\n"), fixture.context);
    UiNode* before = find_by_id(root, "slot");
    ASSERT_NE(before, nullptr);
    // act the definition names a different element, which cannot be detected without the resolver (§12.1)
    const ReloadReport report = apply_reload(root, *parse_ir("node Column id=root\n  node Button id=slot\n"), blind);
    // assert the node was kept, because a missing resolver never forces a rebuild (§12.1)
    EXPECT_EQ(find_by_id(root, "slot"), before);
    EXPECT_EQ(report.replaced, 0U);
}

TEST(DevHookChecks, ALinkedExtensionInstalledItsReloadEntry) {
    // assert the extension registered itself at load, because linking it is the switch (§12.1, AD-02)
    EXPECT_TRUE(dev::present());
}

TEST(DevHookChecks, AReloadWithNoBoundTreeChangesNothing) {
    // arrange the extension holding no tree, which is the state before any window exists (§12.1, AD-02)
    albor::devrt::bind_root(dev::WindowBinding{});
    ASSERT_FALSE(bound());
    // act a definition arrives with no tree to patch (§12.1, AD-12)
    dev::reload("node Column id=root\n  node Label id=title\n");
    // assert nothing was recorded, because there was nothing to change (§12.1)
    EXPECT_EQ(last_report().added, 0U);
}

TEST(DevHookChecks, AClosingWindowReleasesTheExtensionBinding) {
    // arrange a bound tree, which is what a main window hands the extension (§12.1, AD-02)
    ReloadFixture fixture;
    UiNode root;
    albor::devrt::bind_root(dev::WindowBinding{.root = &root, .engine = &fixture.engine, .text = &fixture.text, .has_tree = true});
    ASSERT_TRUE(bound());
    // act the window closes, which is what the runtime announces (§5.2, §12.1)
    unbind_root(&root);
    // assert the extension holds nothing, because a reload must never reach a destroyed window (§12.1, §5.2)
    EXPECT_FALSE(bound());
    // act and assert another window's tree is left alone, because the release names the root it owns (§5.2)
    UiNode other;
    albor::devrt::bind_root(dev::WindowBinding{.root = &other, .engine = &fixture.engine, .text = &fixture.text, .has_tree = true});
    unbind_root(&root);
    EXPECT_TRUE(bound());
    unbind_root(&other);
    EXPECT_FALSE(bound());
}

TEST(DevHookChecks, AHandDrawnWindowLeavesTheBoundTreeAlone) {
    // arrange a real tree bound for reload, which is what a developer edits (§12.1)
    ReloadFixture fixture;
    UiNode root;
    albor::devrt::bind_root(dev::WindowBinding{.root = &root, .engine = &fixture.engine, .text = &fixture.text, .has_tree = true});
    ASSERT_TRUE(bound());
    // act a hand drawn window binds without a tree, which the automation host still observes (§6.1, §10.3)
    UiNode hand_drawn;
    albor::devrt::bind_root(dev::WindowBinding{.root = &hand_drawn, .has_tree = false});
    // act a definition arrives, which must patch the real tree rather than the hand drawn one (§12.1)
    dev::reload("node Column id=root\n  node Label id=title\n");
    // assert the real tree changed and the hand drawn one kept its single node, which is the point of the flag (§12.1)
    EXPECT_TRUE(bound());
    EXPECT_EQ(count_nodes(root), 2U);
    EXPECT_EQ(count_nodes(hand_drawn), 1U);
    // act the hand drawn window closes, which must not release a tree it never owned (§5.2)
    unbind_root(&hand_drawn);
    EXPECT_TRUE(bound());
    // act the real window closes, which is what clears the slot (§5.2)
    unbind_root(&root);
    EXPECT_FALSE(bound());
}

TEST(DevHookChecks, ADefinitionTheReaderRefusedIsDropped) {
    // arrange a bound tree, because only a bound tree is patched (§12.1)
    ReloadFixture fixture;
    UiNode root;
    albor::devrt::bind_root(dev::WindowBinding{.root = &root, .engine = &fixture.engine, .text = &fixture.text, .has_tree = true});
    ASSERT_TRUE(bound());
    // act malformed text arrives, which the reader refuses (§12.1)
    dev::reload("not a definition\n");
    // assert the tree is untouched, because half a definition is worse than none (§12.1)
    EXPECT_EQ(count_nodes(root), 1U);
    EXPECT_EQ(last_report().added, 0U);
}

TEST(DevHookChecks, ADefinitionPatchesTheBoundTreeInPlace) {
    // arrange a bound tree, which is what a main window hands the extension (§12.1, AD-02)
    ReloadFixture fixture;
    UiNode root;
    albor::devrt::bind_root(dev::WindowBinding{.root = &root, .engine = &fixture.engine, .text = &fixture.text, .has_tree = true});
    // act the dev service delivers the definition, which is the whole of a reload (§12.1, AD-12)
    dev::reload("node Column id=root\n  node Label id=title\n    prop text hello\n");
    // assert the tree now carries what the file declares, which is what hot swap means (§12.1)
    ASSERT_EQ(count_nodes(root), 2U);
    UiNode* title = find_by_id(root, "title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(static_cast<Label*>(title)->text(), "hello");
    // assert the report tells a developer what happened (§12.1)
    EXPECT_EQ(last_report().added, 1U);
}

TEST(DevHookChecks, ABoundTreeRepaintsAfterAPatch) {
    // arrange a bound tree that has already settled, because a settled tree is the one that would not repaint (§6.1)
    ReloadFixture fixture;
    UiNode root;
    albor::devrt::bind_root(dev::WindowBinding{.root = &root, .engine = &fixture.engine, .text = &fixture.text, .has_tree = true});
    dev::reload("node Column id=root\n  node Label id=title\n");
    root.clear_dirty();
    // act the definition changes the tree, which must invalidate it (§12.1, §6.1)
    dev::reload("node Column id=root\n  node Label id=title\n  node Button id=ok\n");
    // assert the tree is dirty, because a patched tree has to show what it now declares (§6.1)
    EXPECT_TRUE(root.render_dirty());
}

TEST(DevReloadChecks, AppliesAComponentSerializedFromUi) {
    // arrange a ui component serialized into the dev ir, which is what the producer emits (§12.1, AD-12)
    constexpr std::string_view PANEL_SOURCE = R"(
component Panel {
    Column {
        spacing: 12px;
        Label {
            id: title;
            text: "Hello";
            font_size: 20px;
        }
    }
}
)";
    // the panel is scanned and parsed, which is what the producer does before it serializes (§12.1)
    albor::compiler::DiagnosticList diagnostics;
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(PANEL_SOURCE, diagnostics).tokenize();
    const albor::compiler::SourceFile file = albor::compiler::Parser(tokens, diagnostics).parse();
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    ASSERT_EQ(file.components.size(), 1u);
    const std::optional<IrNode> definition = albor::compiler::serialize_component(file.components.front());
    ASSERT_TRUE(definition.has_value());
    ReloadFixture fixture;
    UiNode root;
    // act the serialized definition is applied to a live tree (§12.1)
    const ReloadReport report = apply_reload(root, *definition, fixture.context);
    // assert the tree carries the element and the properties the ui declared (§12.1)
    EXPECT_EQ(report.added, 1U);
    UiNode* title = find_by_id(root, "title");
    ASSERT_NE(title, nullptr);
    auto* label = dynamic_cast<Label*>(title);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "Hello");
    EXPECT_FLOAT_EQ(label->font_size(), 20.0f);
}

TEST(DevReloadChecks, ACompiledFormAndTheDevIrAgreeOnTheSameUi) {
    // arrange the same ui source compiled into a facade and serialized into the dev ir (§16, AD-25)
    std::ifstream file(ALBOR_DUAL_ENGINE_UI, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    albor::compiler::DiagnosticList diagnostics;
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(source, diagnostics).tokenize();
    const albor::compiler::SourceFile parsed = albor::compiler::Parser(tokens, diagnostics).parse();
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    const std::optional<IrNode> definition = albor::compiler::serialize_component(parsed.components.front());
    ASSERT_TRUE(definition.has_value());
    // the compiled path builds the facade, which is the production output (§11.3)
    BindingEngine engine;
    TestTextEngine text;
    DualEnginePanel panel(&engine, &text);
    // the dev path applies the serialized definition onto a matching root (§12.1)
    ColumnLayout root;
    ReloadFixture fixture;
    const ReloadReport report = apply_reload(root, *definition, fixture.context);
    EXPECT_EQ(report.added, 2U);
    // assert both trees carry the same elements and the same properties, which is what AD-25 claims (§16)
    const UiNode& compiled_column = *static_cast<UiNode&>(panel).child_at(0);
    ASSERT_EQ(compiled_column.child_count(), root.child_count());
    const auto* compiled_label = dynamic_cast<const Label*>(compiled_column.child_at(0));
    const auto* ir_label = dynamic_cast<const Label*>(root.child_at(0));
    ASSERT_NE(compiled_label, nullptr);
    ASSERT_NE(ir_label, nullptr);
    EXPECT_EQ(compiled_label->text(), ir_label->text());
    EXPECT_FLOAT_EQ(compiled_label->font_size(), ir_label->font_size());
    const auto* compiled_button = dynamic_cast<const Button*>(compiled_column.child_at(1));
    const auto* ir_button = dynamic_cast<const Button*>(root.child_at(1));
    ASSERT_NE(compiled_button, nullptr);
    ASSERT_NE(ir_button, nullptr);
    EXPECT_EQ(compiled_button->label(), ir_button->label());
    EXPECT_EQ(compiled_button->disabled(), ir_button->disabled());
}

TEST(DevReloadChecks, ACompiledControlsFormAndTheDevIrAgreeOnTheSameUi) {
    // arrange the same ui source compiled into a facade and serialized into the dev ir (§16, AD-25)
    std::ifstream file(ALBOR_DUAL_ENGINE_CONTROLS_UI, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    albor::compiler::DiagnosticList diagnostics;
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(source, diagnostics).tokenize();
    const albor::compiler::SourceFile parsed = albor::compiler::Parser(tokens, diagnostics).parse();
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    const std::optional<IrNode> definition = albor::compiler::serialize_component(parsed.components.front());
    ASSERT_TRUE(definition.has_value());
    // the compiled path builds the facade, which is the production output (§11.3)
    BindingEngine engine;
    TestTextEngine text;
    DualEngineControls controls(&engine, &text);
    // the dev path applies the serialized definition onto a matching root (§12.1)
    RowLayout root;
    ReloadFixture fixture;
    const ReloadReport report = apply_reload(root, *definition, fixture.context);
    EXPECT_EQ(report.added, 2U);
    // assert both trees carry the same controls and the same properties, which is what AD-25 claims (§16)
    const UiNode& compiled_row = *static_cast<UiNode&>(controls).child_at(0);
    ASSERT_EQ(compiled_row.child_count(), root.child_count());
    const auto* compiled_check = dynamic_cast<const CheckBox*>(compiled_row.child_at(0));
    const auto* ir_check = dynamic_cast<const CheckBox*>(root.child_at(0));
    ASSERT_NE(compiled_check, nullptr);
    ASSERT_NE(ir_check, nullptr);
    EXPECT_EQ(compiled_check->checked(), ir_check->checked());
    EXPECT_EQ(compiled_check->disabled(), ir_check->disabled());
    const auto* compiled_slider = dynamic_cast<const Slider*>(compiled_row.child_at(1));
    const auto* ir_slider = dynamic_cast<const Slider*>(root.child_at(1));
    ASSERT_NE(compiled_slider, nullptr);
    ASSERT_NE(ir_slider, nullptr);
    EXPECT_FLOAT_EQ(compiled_slider->value(), ir_slider->value());
    EXPECT_EQ(compiled_slider->disabled(), ir_slider->disabled());
}

TEST(DevReloadChecks, ACompiledTabsFormAndTheDevIrAgreeOnTheSameUi) {
    // arrange the same ui source compiled into a facade and serialized into the dev ir (§16, AD-25)
    std::ifstream file(ALBOR_DUAL_ENGINE_TABS_UI, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    albor::compiler::DiagnosticList diagnostics;
    const std::vector<albor::compiler::Token> tokens = albor::compiler::Lexer(source, diagnostics).tokenize();
    const albor::compiler::SourceFile parsed = albor::compiler::Parser(tokens, diagnostics).parse();
    ASSERT_TRUE(diagnostics.diagnostics().empty());
    const std::optional<IrNode> definition = albor::compiler::serialize_component(parsed.components.front());
    ASSERT_TRUE(definition.has_value());
    // the compiled path builds the facade, which is the production output (§11.3)
    BindingEngine engine;
    TestTextEngine text;
    DualEngineTabs tabs(&engine, &text);
    // the dev path applies the serialized definition onto a matching root (§12.1)
    TabView root(&engine);
    ReloadFixture fixture;
    const ReloadReport report = apply_reload(root, *definition, fixture.context);
    EXPECT_EQ(report.added, 6U);
    // assert both trees carry the same view, selection and pages, which is what AD-25 claims (§16)
    const UiNode& compiled = *static_cast<UiNode&>(tabs).child_at(0);
    const auto* compiled_view = dynamic_cast<const TabView*>(&compiled);
    ASSERT_NE(compiled_view, nullptr);
    ASSERT_EQ(compiled_view->tab_count(), root.tab_count());
    EXPECT_EQ(compiled_view->tab_count(), 2U);
    EXPECT_EQ(compiled_view->selected(), root.selected());
    EXPECT_EQ(compiled_view->selected(), 1);
    EXPECT_TRUE(compiled_view->vertical());
    EXPECT_TRUE(root.vertical());
    for (std::size_t index = 0; index < 2; index++) {
        const auto* compiled_page = dynamic_cast<const TabPage*>(compiled.child_at(index));
        const auto* ir_page = dynamic_cast<const TabPage*>(root.child_at(index));
        ASSERT_NE(compiled_page, nullptr);
        ASSERT_NE(ir_page, nullptr);
        EXPECT_EQ(compiled_page->label(), ir_page->label());
        EXPECT_EQ(compiled_page->title() != nullptr, ir_page->title() != nullptr);
        if (const TabTitle* compiled_title = compiled_page->title()) {
            ASSERT_NE(ir_page->title(), nullptr);
            EXPECT_EQ(compiled_title->child_count(), ir_page->title()->child_count());
        }
    }
}
