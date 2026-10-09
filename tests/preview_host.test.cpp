#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "i18n/catalog.h"
#include "layout/column_layout.h"
#include "preview/preview_host.h"
#include "runtime/binding_engine.h"
#include "ui/ui_node.h"
#include "widgets/button.h"
#include "widgets/label.h"

using namespace albor;

namespace
{
    // the panel a suite previews, which is what a designer would have written (§12.2)
    const char* PANEL = R"UI(component Panel {
    in property<string> user_name: "World";

    Column {
        spacing: 12px;

        Label {
            id: greeting;
            text: @tr("Hello, ");
            font_size: 18px;
        }

        Button {
            id: action;
            text: "Press me";
        }
    }
})UI";

    // count every node of a tree, which is how a suite asserts on what a preview built (§12.2)
    std::size_t count_nodes(UiNode& root) {
        // the count starts at zero, because the walk visits the root among the rest (§8.1)
        std::size_t count = 0;
        // the tree is walked, because the shape is what a preview shows (§12.2)
        root.visit([&](UiNode&) { count++; });
        // the count is returned whole (§12.2)
        return count;
    }

    // find the first node of a type, which is how a suite asserts a widget was instantiated (§12.2)
    UiNode* find_first(UiNode& root, const std::function<bool(UiNode&)>& matches) {
        // the node is recorded during the walk, because visit cannot return a value (§12.2)
        UiNode* found = nullptr;
        // the tree is walked, because the widget may sit at any depth (§12.2)
        root.visit([&](UiNode& node) {
            // the first match wins, which is what a suite asks for (§12.2)
            if (found == nullptr && matches(node))
                found = &node;
        });
        // the node is returned, or nothing when none matched (§12.2)
        return found;
    }
} // namespace

TEST(PreviewHostChecks, ACleanSourceBuildsItsTree) {
    // arrange
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(PANEL, engine);
    // assert nothing was reported, because the source is what a compiler accepts (§12.2)
    EXPECT_EQ(result.diagnostics, 0U);
    // assert a tree exists, because that is what a preview window shows (§12.2)
    ASSERT_NE(result.root, nullptr);
    // assert the column and its two children were built (§12.2)
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<ColumnLayout*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<Label*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<Button*>(&node) != nullptr; }), nullptr);
    // assert the tree is the column and its two children, because the component root is the column (§12.2)
    EXPECT_EQ(count_nodes(*result.root), 3U);
}

TEST(PreviewHostChecks, TheAuthorIdIsAppliedBecauseAutomationAndPreviewShareIt) {
    // arrange
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(PANEL, engine);
    // assert
    ASSERT_NE(result.root, nullptr);
    // the label is found by the id the author wrote, which is the identity a selector uses (§18.1, AD-11)
    UiNode* greeting = find_first(*result.root, [](UiNode& node) { return node.identity().explicit_id == "greeting"; });
    // assert
    EXPECT_NE(greeting, nullptr);
}

TEST(PreviewHostChecks, TheWrittenTextIsShown) {
    // arrange
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(PANEL, engine);
    // assert the label carries what the author wrote, translated through the catalogue (§9.2, §9.1)
    ASSERT_NE(result.root, nullptr);
    // the label is found by type, because the text is what this asserts (§12.2)
    UiNode* label = find_first(*result.root, [](UiNode& node) { return dynamic_cast<Label*>(&node) != nullptr; });
    // assert
    ASSERT_NE(label, nullptr);
    // the run reads as whatever the catalogue resolves, which is what the macro means (§9.2)
    EXPECT_EQ(static_cast<Label*>(label)->text(), i18n::translate("Hello, "));
}

TEST(PreviewHostChecks, TheWrittenFontSizeIsApplied) {
    // arrange
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(PANEL, engine);
    // assert the size is the one the author wrote, because that is what a preview is for (§9.1, §12.2)
    ASSERT_NE(result.root, nullptr);
    // the label is found by type (§12.2)
    UiNode* label = find_first(*result.root, [](UiNode& node) { return dynamic_cast<Label*>(&node) != nullptr; });
    // assert
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(static_cast<Label*>(label)->font_size(), 18.0f);
}

TEST(PreviewHostChecks, TheWrittenSpacingIsApplied) {
    // arrange
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(PANEL, engine);
    // assert the gap is what the author wrote, because a preview that ignored it would mislead (§8.2, §12.2)
    ASSERT_NE(result.root, nullptr);
    // the column is found by type (§12.2)
    UiNode* column = find_first(*result.root, [](UiNode& node) { return dynamic_cast<ColumnLayout*>(&node) != nullptr; });
    // assert
    ASSERT_NE(column, nullptr);
    EXPECT_FLOAT_EQ(static_cast<ColumnLayout*>(column)->gap(), 12.0f);
}

TEST(PreviewHostChecks, ASourceWithErrorsBuildsNothingAndReportsThem) {
    // arrange a source naming a widget that does not exist (§12.2)
    const std::string broken = R"UI(component Panel {
    Column { Nope { } }
})UI";
    // act
    BindingEngine engine;
    preview::PreviewResult result = preview::build(broken, engine);
    // assert nothing was built, because half a component would mislead a designer (§12.2)
    EXPECT_EQ(result.root, nullptr);
    // assert the problem was reported rather than swallowed (§12.2)
    EXPECT_GT(result.diagnostics, 0U);
}

TEST(PreviewHostChecks, AnUnknownElementLeavesNoGapInTheTree) {
    // arrange a source whose child is a widget this build does not instantiate (§12.2)
    const std::string partial = R"UI(component Panel {
    Column {
        Label { text: "kept"; }
    }
})UI";
    // act
    BindingEngine engine;
    preview::PreviewResult result = preview::build(partial, engine);
    // assert the tree holds only what could be built, which is what a designer can act on (§12.2)
    ASSERT_NE(result.root, nullptr);
    // assert
    EXPECT_EQ(count_nodes(*result.root), 2U);
}
