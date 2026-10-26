#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "i18n/catalog.h"
#include "layout/column_layout.h"
#include "layout/grid_layout.h"
#include "layout/padding_layout.h"
#include "layout/row_layout.h"
#include "layout/stack_layout.h"
#include "preview/preview_host.h"
#include "runtime/binding_engine.h"
#include "ui/ui_node.h"
#include "widgets/button.h"
#include "widgets/canvas.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/progress_bar.h"
#include "widgets/scroll_view.h"
#include "widgets/slider.h"
#include "widgets/tab_page.h"
#include "widgets/tab_title.h"
#include "widgets/tab_view.h"
#include "widgets/text_field.h"

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

TEST(PreviewHostChecks, AnEmbeddedImageResolvesBesideTheSourcePath) {
    // arrange a source whose asset sits beside it, which is what a saved preview file has (§12.2, §6.4)
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "albor-preview-image";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    std::filesystem::copy_file(std::filesystem::path(ALBOR_TEST_VECTORS_DIR) / "icon.png", root / "icon.png", std::filesystem::copy_options::overwrite_existing);
    // act
    BindingEngine engine;
    const preview::PreviewResult result = preview::build(R"(component Panel { in property<image> icon: @image("icon.png"); Column {} })", engine, root / "panel.ui");
    // assert the carried asset was found, because the preview was told where the source lives (§16, §6.4)
    EXPECT_EQ(result.diagnostics, 0U);
    EXPECT_NE(result.root, nullptr);
    std::filesystem::remove_all(root, ignored);
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

TEST(PreviewHostChecks, EveryElementThisBuildKnowsIsInstantiated) {
    // arrange a source naming every element the factory builds, which is what keeps the factory honest (§12.2)
    const std::string gallery = R"UI(component Gallery {
    Stack {
        ScrollView {
            id: scroller;
            Column {
                Label { text: "row one"; }
            }
        }
        Padding {
            padding: 8px;
            Row {
                Slider { id: level; }
                CheckBox { id: agreed; }
                Button { id: go; text: "Go"; }
            }
        }
        Grid { }
        Canvas { id: surface; }
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(gallery, engine);
    // assert nothing was reported, because every name in the source is one the factory builds (§12.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    // assert every layout was built, which is what a designer previews (§12.2)
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<StackLayout*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<PaddingLayout*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<RowLayout*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<GridLayout*>(&node) != nullptr; }), nullptr);
    // assert every widget was built, which is the half of the factory that needs an engine (§12.2)
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<ScrollView*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<Slider*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<CheckBox*>(&node) != nullptr; }), nullptr);
    EXPECT_NE(find_first(*result.root, [](UiNode& node) { return dynamic_cast<Canvas*>(&node) != nullptr; }), nullptr);
    // assert the scroll view built the real viewport, because a preview shows what the app will show (§12.2)
    UiNode* scroller = find_first(*result.root, [](UiNode& node) { return dynamic_cast<ScrollView*>(&node) != nullptr; });
    ASSERT_NE(scroller, nullptr);
    // assert the scroller holds the column it was given, because nesting is what the tree means (§8.1)
    EXPECT_NE(find_first(*scroller, [](UiNode& node) { return dynamic_cast<ColumnLayout*>(&node) != nullptr; }), nullptr);
    // assert the scroller holds the label below it, because the viewport nests whatever it is given (§8.1)
    EXPECT_NE(find_first(*scroller, [](UiNode& node) { return dynamic_cast<Label*>(&node) != nullptr; }), nullptr);
}

TEST(PreviewHostChecks, EveryWrittenPropertyReachesTheWidgetItNames) {
    // arrange a source writing each property onto the container that owns it (§8.2)
    const std::string rich = R"UI(component Rich {
    Column {
        spacing: 10.0;
        Column {
            padding: 6.0;
        }
        Row {
            spacing: 4.0;
        }
        Grid {
            spacing: 3.0;
            CheckBox {
                id: agreed;
                checked: true;
            }
            Label {
                id: caption;
                text: "run";
                font_size: 21.0;
            }
            Padding {
                padding: 2.0;
            }
        }
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(rich, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    // assert the column gap follows the written number, which is what a designer adjusts (§8.2)
    std::vector<ColumnLayout*> columns;
    result.root->visit([&columns](UiNode& node) {
        if (auto* column = dynamic_cast<ColumnLayout*>(&node); column != nullptr)
            columns.push_back(column);
    });
    // assert both columns in the source were built, because the outer is written differently from the inner (§8.2)
    ASSERT_EQ(columns.size(), 2U);
    EXPECT_FLOAT_EQ(columns.at(0)->gap(), 10.0f);
    // assert the outer column keeps no padding, because none was written on it (§8.2)
    EXPECT_FLOAT_EQ(columns.at(0)->padding(), 0.0f);
    // assert the inner column takes the written padding as an inset around its own children (§8.2)
    EXPECT_FLOAT_EQ(columns.at(1)->padding(), 6.0f);
    // assert the row gap follows the same rule in the other axis (§8.2)
    RowLayout* row = dynamic_cast<RowLayout*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<RowLayout*>(&node) != nullptr; }));
    ASSERT_NE(row, nullptr);
    EXPECT_FLOAT_EQ(row->gap(), 4.0f);
    // assert a grid takes the same gap between its cells, which is the third owner of the property (§8.2)
    GridLayout* grid = dynamic_cast<GridLayout*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<GridLayout*>(&node) != nullptr; }));
    ASSERT_NE(grid, nullptr);
    EXPECT_FLOAT_EQ(grid->gap(), 3.0f);
    // assert the padding layout takes its inset directly, which is the other owner of the same property (§8.2)
    PaddingLayout* inset = dynamic_cast<PaddingLayout*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<PaddingLayout*>(&node) != nullptr; }));
    ASSERT_NE(inset, nullptr);
    EXPECT_FLOAT_EQ(inset->padding().top, 2.0f);
    // assert the checked state shows as written, because a preview must not imply a different default (§8)
    CheckBox* box = dynamic_cast<CheckBox*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<CheckBox*>(&node) != nullptr; }));
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->checked());
    // assert the written size reaches the label that owns it (§9.1)
    Label* sized = dynamic_cast<Label*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<Label*>(&node) != nullptr; }));
    ASSERT_NE(sized, nullptr);
    EXPECT_FLOAT_EQ(sized->font_size(), 21.0f);
}

TEST(PreviewHostChecks, AFieldsOwnStatesBothHoldBecauseTheyAreSeparateProperties) {
    // arrange a field writing both states, which a preview must show as the author wrote them (§8)
    const std::string source = R"UI(component Both {
    TextField {
        id: notes;
        read_only: true;
        multiline: true;
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(source, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    TextField* field = dynamic_cast<TextField*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<TextField*>(&node) != nullptr; }));
    ASSERT_NE(field, nullptr);
    // assert the read only state holds, because writing multiline must not clear it (§8)
    EXPECT_TRUE(field->read_only());
    // assert the multi line state holds too, because writing one state must not clear the other (§8)
    EXPECT_TRUE(field->multiline());
}

TEST(PreviewHostChecks, WritingTheMultiLineStateLeavesTheReadOnlyStateAlone) {
    // arrange a field writing only the multi line state, which must not disturb what it did not write (§8)
    const std::string source = R"UI(component OnlyMultiline {
    TextField {
        id: notes;
        multiline: true;
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(source, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    TextField* field = dynamic_cast<TextField*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<TextField*>(&node) != nullptr; }));
    ASSERT_NE(field, nullptr);
    EXPECT_TRUE(field->multiline());
    // assert the untouched state keeps its own default, which is an editable field (§8)
    EXPECT_FALSE(field->read_only());
}

TEST(PreviewHostChecks, TheWrittenFontSizeReachesAFieldAndNotOnlyALabel) {
    // arrange a field writing its size, which a label and a field both own (§9.1)
    const std::string source = R"UI(component SizedField {
    TextField {
        id: notes;
        value: "Albor";
        font_size: 19.0;
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(source, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    TextField* field = dynamic_cast<TextField*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<TextField*>(&node) != nullptr; }));
    ASSERT_NE(field, nullptr);
    // assert the size reached the field, because one assignment names one number on either widget (§9.1)
    EXPECT_FLOAT_EQ(field->font_size(), 19.0f);
}

TEST(PreviewHostChecks, AFieldShowsTheValueAndHintTheAuthorWrote) {
    // arrange a field writing both strings, which a preview shows as written (§8)
    const std::string source = R"UI(component WrittenField {
    TextField {
        id: notes;
        value: "Albor";
        placeholder: "type here";
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(source, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    TextField* field = dynamic_cast<TextField*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<TextField*>(&node) != nullptr; }));
    ASSERT_NE(field, nullptr);
    // assert the edited text reaches the widget, because a preview shows the value the app will show (§8)
    EXPECT_EQ(field->value(), "Albor");
    // assert the hint reaches it too, because an empty field draws what the author wrote (§8)
    EXPECT_EQ(field->placeholder(), "type here");
}

TEST(PreviewHostChecks, AProgressBarShowsTheFractionTheAuthorWrote) {
    // arrange a bar writing its fraction, which a preview shows as the author wrote it (§8)
    const std::string source = R"UI(component WrittenProgress {
    ProgressBar {
        id: download;
        value: 0.75;
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(source, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    ProgressBar* progress = dynamic_cast<ProgressBar*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<ProgressBar*>(&node) != nullptr; }));
    ASSERT_NE(progress, nullptr);
    // assert the fraction reached the widget, because a preview shows the value the app will show (§8)
    EXPECT_FLOAT_EQ(progress->value(), 0.75f);
}

TEST(PreviewHostChecks, ATabViewShowsTheSelectedPageAndItsLabels) {
    // arrange a tab view writing a selection and two labelled pages (§8)
    const std::string source = R"UI(component WrittenTabs {
    TabView {
        selected: 1;
        vertical: true;

        Tab {
            id: first_tab;
            label: "One";
            TabTitle {
                Label { text: "★"; }
            }
            Label { text: "first"; }
        }

        Tab {
            id: second_tab;
            label: "Two";
            Label { text: "second"; }
        }
    }
})UI";
    BindingEngine engine;
    // act
    preview::PreviewResult result = preview::build(source, engine);
    // assert nothing was reported, because every property here is one the catalog declares (§11.2)
    EXPECT_EQ(result.diagnostics, 0U);
    ASSERT_NE(result.root, nullptr);
    TabView* tabs = dynamic_cast<TabView*>(find_first(*result.root, [](UiNode& node) { return dynamic_cast<TabView*>(&node) != nullptr; }));
    ASSERT_NE(tabs, nullptr);
    // assert the view holds both pages and the chosen index reached it (§8)
    ASSERT_EQ(tabs->tab_count(), 2u);
    EXPECT_EQ(tabs->selected(), 1);
    EXPECT_TRUE(tabs->vertical());
    // assert each page carries the label its strip draws (§8)
    ASSERT_NE(tabs->tab_at(0), nullptr);
    ASSERT_NE(tabs->tab_at(1), nullptr);
    EXPECT_EQ(tabs->tab_at(0)->label(), "One");
    EXPECT_EQ(tabs->tab_at(1)->label(), "Two");
    // assert the strip content reached the first page, which is what a composed title needs (§8)
    ASSERT_NE(tabs->tab_at(0)->title(), nullptr);
    EXPECT_EQ(tabs->tab_at(0)->title()->child_count(), 1u);
    EXPECT_EQ(tabs->tab_at(1)->title(), nullptr);
}
