#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/constraints.h"
#include "core/geometry.h"
#include "core/scrollable_content.h"
#include "input/input_controller.h"
#include "input/input_dispatcher.h"
#include "model/item_key.h"
#include "render/frame_scene.h"
#include "render/render-software/software_render_surface.h"
#include "render/render-software/software_renderer_backend.h"
#include "render/screenshot.h"
#include "runtime/binding_engine.h"
#include "semantics/semantic_node.h"
#include "semantics/semantic_provider.h"
#include "style/theme.h"
#include "style/theme_builder.h"
#include "text/text-skia/skia_text_engine.h"
#include "ui/scene_builder.h"
#include "ui/ui_event.h"
#include "ui/ui_node.h"
#include "widgets/label.h"
#include "widgets/list_box.h"
#include "widgets/scroll_view.h"
#include "widgets/widget.h"
#include "window/window_metrics.h"

using namespace albor;

namespace
{
    // the row double that carries no semantics of its own, which a plain row of a list is (§10.1)
    class PlainRow final : public UiNode
    {
    };

    // the row double that carries its own semantics, which a real row of a list does (§10.1)
    class SemanticRow final : public UiNode, public SemanticProvider
    {
    public:
        [[nodiscard]] SemanticNode semantic_node() const override {
            // the row announces a label under its own name, which the list decorates rather than replaces (§10.1)
            SemanticNode node{SemanticRole::Label};
            node.set_name("cell");
            return node;
        }

        [[nodiscard]] std::size_t semantic_child_count() const override { return 1; }

        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // the single child is a leaf, so the forward has something to hand back (§10.1)
            if (index != 0)
                return nullptr;
            return std::make_unique<SemanticNodeProvider>(SemanticNode{SemanticRole::Label});
        }
    };

    // the content double that materializes a fixed set of rows, which is what a list overlays its states onto (§8.2)
    class RowContent final : public UiNode, public ScrollableContent
    {
    public:
        // the height one row takes, which the viewport applies before the extent is read (§8.2, AD-13)
        float row_extent = 20.0f;

        // the item index the first built row shows, which the window can start past (§8.2, AD-13)
        std::size_t first = 0;

        // the built rows, which the list announces by position (§10.1)
        std::vector<std::unique_ptr<UiNode>> rows;

        void add_plain_row() { rows.push_back(std::make_unique<PlainRow>()); }

        void add_semantic_row() { rows.push_back(std::make_unique<SemanticRow>()); }

        [[nodiscard]] Size content_extent() const override { return Size{200.0f, static_cast<float>(rows.size()) * row_extent}; }

        void set_scroll_viewport(const ScrollViewport&) override {}

        void set_row_extent(float extent) override { row_extent = extent; }

        [[nodiscard]] std::size_t materialized_row_count() const override { return rows.size(); }

        [[nodiscard]] std::size_t first_row_index() const override { return first; }

        [[nodiscard]] UiNode* row_node(std::size_t position) override { return position < rows.size() ? rows.at(position).get() : nullptr; }

        void measure(const SizeConstraints&, Size& out_size) override {
            out_size = content_extent();
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override { m_bounds = bounds; }
    };

    // the context double whose rows are read by their keys, which is what a keyed selection needs (§18.1)
    class KeySource
    {
    public:
        // the keys the rows are named by, in order (§18.1, AD-13)
        std::vector<std::string> keys;

        static model::ItemKey read(void* context, std::size_t index) {
            // the context travels back as the source it was bound with (§13.3)
            auto* source = static_cast<KeySource*>(context);
            // a row past the names has no identity, which is the empty key (§18.1)
            return index < source->keys.size() ? model::ItemKey{source->keys.at(index)} : model::ItemKey{};
        }
    };

    // the content double, bound to a list with rows and a key source, which most tests share (§8.2, §18.1)
    class Fixture
    {
    public:
        // the engine settles the properties of the list it builds (§8.1)
        BindingEngine engine;

        // the list under test, which owns its internal viewport (§8)
        ListBox box{&engine};

        // the content adopted into the internal viewport, which the list never owns (§8.2, AD-09)
        RowContent* content = nullptr;

        // the keys the rows are named by, which a keyed selection reads (§18.1)
        KeySource source;

        Fixture() {
            // the content is built before it is adopted, because the list takes ownership of what it hosts (§8.2)
            auto rows = std::make_unique<RowContent>();
            content = rows.get();
            // the content is the viewport's child, because the viewport scrolls exactly one container (§8.2)
            box.scroll_view().add_child(std::move(rows));
            // the reader is installed, because a list cannot capture a row identity on its own (§13.3)
            box.set_key_reader(&KeySource::read, &source);
        }

        // add a row under the given name, which is what a keyed selection matches (§18.1, AD-13)
        void add_row(const std::string& key) {
            content->add_plain_row();
            source.keys.push_back(key);
            box.set_item_count(content->rows.size());
        }

        // lay the list out over a viewport, which is what places rows and fixes the scroll range (§8.2)
        void layout(float width, float height) {
            box.set_viewport_extent(Size{width, height});
            Size measured{};
            box.measure(SizeConstraints::unconstrained(), measured);
            box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{width, height}));
        }
    };
} // namespace

TEST(ListBoxChecks, ABareListHasNoSelectionAndOneViewport) {
    // arrange, act
    Fixture fixture;
    // assert a list starts with no row chosen and one internal viewport (§8, §18.3)
    EXPECT_FALSE(fixture.box.has_selection());
    EXPECT_EQ(fixture.box.selected_index(), model::SelectionModel::NO_INDEX);
    EXPECT_TRUE(fixture.box.selected_key().text().empty());
    EXPECT_EQ(fixture.box.focused_index(), 0U);
    EXPECT_EQ(fixture.box.child_count(), 1U);
    EXPECT_EQ(fixture.box.semantic_node().role(), SemanticRole::List);
}

TEST(ListBoxChecks, TheViewportIsWhatTheListMeasures) {
    // arrange
    Fixture fixture;
    // act
    fixture.box.set_viewport_extent(Size{240.0f, 320.0f});
    Size measured{};
    fixture.box.measure(SizeConstraints::unconstrained(), measured);
    // assert a list never grows with its rows, so it is the viewport it declares (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 240.0f);
    EXPECT_FLOAT_EQ(measured.height, 320.0f);
    EXPECT_EQ(fixture.box.preferred_size(), measured);
}

TEST(ListBoxChecks, TheViewportTakesTheBoundsTheListWasGiven) {
    // arrange
    Fixture fixture;
    const Rect bounds = Rect::from_origin_size(Point{10.0f, 20.0f}, Size{240.0f, 320.0f});
    // act
    fixture.box.arrange(bounds);
    // assert the internal viewport fills the list, because it is the only child (§8.2)
    EXPECT_EQ(fixture.box.scroll_view().bounds(), bounds);
}

TEST(ListBoxChecks, TheCountBoundsTheFocusedRow) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    // act the focus is asked to land past the last row (§8.3)
    fixture.box.set_focused_index(99);
    // assert the focus is clamped into the rows the list hosts (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 2U);
}

TEST(ListBoxChecks, AListWithNoRowsRefusesAFocus) {
    // arrange a list that hosts nothing
    Fixture fixture;
    // act
    fixture.box.set_focused_index(3);
    // assert there is no row to focus, so the request is dropped (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, SelectingClampsIntoTheRowsTheListHosts) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    // act
    fixture.box.select(99);
    // assert a stale position lands on the last row rather than past it (§18.3)
    EXPECT_TRUE(fixture.box.has_selection());
    EXPECT_EQ(fixture.box.selected_index(), 2U);
    EXPECT_EQ(fixture.box.selected_key().text(), "c");
}

TEST(ListBoxChecks, SelectingWithNoRowsIsDropped) {
    // arrange a list that hosts nothing
    Fixture fixture;
    // act
    fixture.box.select(0);
    // assert there is no row to select, so nothing is chosen (§18.3)
    EXPECT_FALSE(fixture.box.has_selection());
}

TEST(ListBoxChecks, TheGeneratedSetterSharesTheOneSelection) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    // act the property setter the generated code writes is used (§11.3)
    fixture.box.set_selected_index(1);
    // assert it lands on the same selection the imperative call writes (§11.3, §18.3)
    EXPECT_EQ(fixture.box.selected_index(), 1U);
    EXPECT_EQ(fixture.box.selected_key().text(), "b");
}

TEST(ListBoxChecks, SelectingByIdentityFindsTheRow) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    // act
    fixture.box.select(model::ItemKey{"b"});
    // assert the scan stops on the row that carries the identity (§18.1)
    EXPECT_EQ(fixture.box.selected_index(), 1U);
    EXPECT_EQ(fixture.box.selected_key().text(), "b");
}

TEST(ListBoxChecks, SelectingByIdentityWithNoReaderIsDropped) {
    // arrange a list without a reader, which cannot map an identity to a row
    Fixture fixture;
    fixture.add_row("a");
    fixture.box.set_key_reader(nullptr, nullptr);
    // act
    fixture.box.select(model::ItemKey{"a"});
    // assert nothing is chosen, because the identity cannot be placed (§18.1)
    EXPECT_FALSE(fixture.box.has_selection());
}

TEST(ListBoxChecks, SelectingAnAbsentIdentityChoosesNothing) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    // act a key no row carries is selected (§18.1)
    fixture.box.select(model::ItemKey{"zzz"});
    // assert the scan ends without a match, so nothing is chosen (§18.1)
    EXPECT_FALSE(fixture.box.has_selection());
}

TEST(ListBoxChecks, ClearingDropsTheSelection) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.box.select(0);
    // act
    fixture.box.clear_selection();
    // assert the selection is gone, which is what clearing means (§18.3)
    EXPECT_FALSE(fixture.box.has_selection());
    EXPECT_EQ(fixture.box.selected_index(), model::SelectionModel::NO_INDEX);
}

TEST(ListBoxChecks, FocusAndSelectionAreIndependent) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    fixture.box.select(2);
    // act the focus moves onto a row the selection did not follow (§8.3)
    fixture.box.set_focused_index(0);
    // assert the two states are stored apart, which is what a list needs (D4)
    EXPECT_EQ(fixture.box.focused_index(), 0U);
    EXPECT_EQ(fixture.box.selected_index(), 2U);
}

TEST(ListBoxChecks, AShrinkingCountDropsAStaleSelection) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    fixture.box.select(2);
    // act the list shrinks below the selected row (§18.3)
    fixture.box.set_item_count(2);
    // assert the selection is dropped and the focus is clamped, because their rows are gone (§8.3, §18.3)
    EXPECT_FALSE(fixture.box.has_selection());
    EXPECT_EQ(fixture.box.focused_index(), 1U);
    // act the list empties entirely
    fixture.box.set_item_count(0);
    // assert the focus returns to the leading row, because there is none left (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, TheArrowKeysMoveTheFocusAndTheSelection) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    fixture.box.dispatch_event(down);
    // assert the down arrow steps one row and the selection follows it (§8.3)
    EXPECT_TRUE(down.handled);
    EXPECT_EQ(fixture.box.focused_index(), 1U);
    EXPECT_EQ(fixture.box.selected_index(), 1U);
    // act the up arrow steps back
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    fixture.box.dispatch_event(up);
    // assert
    EXPECT_TRUE(up.handled);
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, TheArrowKeysClampAtTheEnds) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    // act the up arrow is pressed at the leading row (§8.3)
    fixture.box.dispatch_event(up);
    // assert the move is clamped rather than wrapping (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 0U);
    // act the down arrow reaches the last row then is pressed again
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    fixture.box.dispatch_event(down);
    fixture.box.dispatch_event(down);
    // assert the focus stops on the last row (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 1U);
}

TEST(ListBoxChecks, TheHomeAndEndKeysJumpToTheEnds) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    // act
    fixture.box.dispatch_event(end);
    // assert end lands on the last row and selects it (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 2U);
    EXPECT_EQ(fixture.box.selected_index(), 2U);
    // act
    Event home{.kind = EventKind::KeyDown, .key = Key::Home};
    fixture.box.dispatch_event(home);
    // assert home returns to the first row (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, TheEndKeyOnAnEmptyListIsQuiet) {
    // arrange a list that hosts nothing
    Fixture fixture;
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    // act
    fixture.box.dispatch_event(end);
    // assert the key is claimed but nothing moves, because there is no last row (§8.3)
    EXPECT_TRUE(end.handled);
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, ThePageKeysStepAWholeViewport) {
    // arrange ten rows of twenty in a hundred tall viewport, which is five rows a page (§8.2, §8.3)
    Fixture fixture;
    for (int index = 0; index < 10; index++)
        fixture.add_row("row" + std::to_string(index));
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    Event page_down{.kind = EventKind::KeyDown, .key = Key::PageDown};
    // act
    fixture.box.dispatch_event(page_down);
    // assert a page moves the whole viewport, which is five rows here (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 5U);
    // act the page key steps back
    Event page_up{.kind = EventKind::KeyDown, .key = Key::PageUp};
    fixture.box.dispatch_event(page_up);
    // assert
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, APageWithNoRowHeightStepsOneRow) {
    // arrange a list whose row height was never set, so a page cannot be counted in rows (§8.2)
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    Event page_down{.kind = EventKind::KeyDown, .key = Key::PageDown};
    // act
    fixture.box.dispatch_event(page_down);
    // assert a page is a single row rather than a division by zero (§8.3)
    EXPECT_EQ(fixture.box.focused_index(), 1U);
}

TEST(ListBoxChecks, TheReturnKeyActivatesTheFocusedRow) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.box.set_focused_index(1);
    std::vector<std::string> raised;
    fixture.box.set_on_activate([&raised](const std::string& key) { raised.push_back(key); });
    Event enter{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    fixture.box.dispatch_event(enter);
    // assert the focused row is raised with its identity (§18.1)
    ASSERT_EQ(raised.size(), 1U);
    EXPECT_EQ(raised.at(0), "b");
}

TEST(ListBoxChecks, TheSpaceKeyActivatesLikeReturn) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    std::vector<std::string> raised;
    fixture.box.set_on_activate([&raised](const std::string& key) { raised.push_back(key); });
    Event space{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    fixture.box.dispatch_event(space);
    // assert space raises the focused row, which is what the control means (§18.1)
    EXPECT_EQ(raised.size(), 1U);
}

TEST(ListBoxChecks, AListWithNoRowsRaisesNothing) {
    // arrange a list that hosts nothing
    Fixture fixture;
    std::size_t raised = 0;
    fixture.box.set_on_activate([&raised](const std::string&) { raised++; });
    Event enter{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    fixture.box.dispatch_event(enter);
    // assert there is no row to raise, so nothing is told (§18.1)
    EXPECT_EQ(raised, 0U);
}

TEST(ListBoxChecks, AHiddenListTakesNoKey) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.box.set_visible(false);
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    fixture.box.dispatch_event(down);
    // assert a hidden list draws nothing to aim at, so it claims no key (§8.3)
    EXPECT_FALSE(down.handled);
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, ADisabledListTakesNoKey) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.box.set_disabled(true);
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    fixture.box.dispatch_event(down);
    // assert a disabled list refuses navigation, which is what the state means (§8.3)
    EXPECT_FALSE(down.handled);
    EXPECT_TRUE(fixture.box.disabled());
}

TEST(ListBoxChecks, AModifiedKeyIsLeftToTheApplication) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    Event shortcut{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    shortcut.modifiers.command = true;
    // act
    fixture.box.dispatch_event(shortcut);
    // assert a modified key belongs to the application, which the standards reserve first (§8.3)
    EXPECT_FALSE(shortcut.handled);
    EXPECT_EQ(fixture.box.focused_index(), 0U);
}

TEST(ListBoxChecks, ANonKeyEventIsLeftAlone) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = 10.0f};
    // act
    fixture.box.dispatch_event(wheel);
    // assert the list only answers the keys it navigates with (§8.3)
    EXPECT_FALSE(wheel.handled);
}

TEST(ListBoxChecks, AKeyTheListDoesNotAnswerIsLeftAlone) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    Event letter{.kind = EventKind::KeyDown, .key = Key::A};
    // act
    fixture.box.dispatch_event(letter);
    // assert every other key belongs to a parent, so the list does not claim it (§8.3)
    EXPECT_FALSE(letter.handled);
}

TEST(ListBoxChecks, ASelectionChangeRunsTheInstalledWork) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    std::size_t changes = 0;
    fixture.box.set_on_selection_changed([&changes] { changes++; });
    // act
    fixture.box.select(1);
    // assert the work runs once, which is what the signal reports (§18.3)
    EXPECT_EQ(changes, 1U);
}

TEST(ListBoxChecks, TheSelectionSignalWorkIsSwapped) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    std::size_t first = 0;
    std::size_t second = 0;
    fixture.box.set_on_selection_changed([&first] { first++; });
    // act the work is swapped for a later listener (§18.3)
    fixture.box.set_on_selection_changed([&second] { second++; });
    fixture.box.select(1);
    // assert only the current work runs, because the swapped one is gone (§18.3)
    EXPECT_EQ(first, 0U);
    EXPECT_EQ(second, 1U);
}

TEST(ListBoxChecks, TheViewportSettingsReachTheInternalView) {
    // arrange
    Fixture fixture;
    // act
    fixture.box.set_row_height(24.0f);
    fixture.box.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    fixture.box.set_viewport_extent(Size{100.0f, 200.0f});
    // assert the settings belong to the internal view, which the list drives rather than shadows (§8.2)
    EXPECT_FLOAT_EQ(fixture.box.row_height(), 24.0f);
    EXPECT_EQ(fixture.box.scroll_view().row_height(), 24.0f);
    EXPECT_EQ(fixture.box.scroll_view().scrollbar_visibility(), ScrollbarVisibility::AlwaysOff);
    EXPECT_EQ(fixture.box.scroll_view().viewport_extent(), (Size{100.0f, 200.0f}));
}

TEST(ListBoxChecks, AnUnchangedDisabledWriteChangesNothing) {
    // arrange
    Fixture fixture;
    fixture.box.set_disabled(true);
    fixture.box.clear_dirty();
    // act the state is written again with the value it already holds (§8.3)
    fixture.box.set_disabled(true);
    // assert nothing asked for a frame, because the list answers the same keys (§8.3)
    EXPECT_FALSE(fixture.box.render_dirty());
}

TEST(ListBoxChecks, SelectingScrollsTheRowIntoView) {
    // arrange ten rows of twenty in a hundred tall viewport (§8.2)
    Fixture fixture;
    for (int index = 0; index < 10; index++)
        fixture.add_row("row" + std::to_string(index));
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    // act a row below the viewport is selected
    fixture.box.select(5);
    // assert the viewport scrolls down so the row meets the bottom, which is what revealing means (§8.2)
    EXPECT_FLOAT_EQ(fixture.box.scroll_view().offset(), 20.0f);
    // act a row above the viewport is selected
    fixture.box.select(0);
    // assert the viewport scrolls back up so the row meets the top (§8.2)
    EXPECT_FLOAT_EQ(fixture.box.scroll_view().offset(), 0.0f);
}

TEST(ListBoxChecks, RevealingWithNoRowHeightMovesNothing) {
    // arrange a list whose row height was never set, so no row has a position (§8.2)
    Fixture fixture;
    fixture.add_row("a");
    fixture.layout(200.0f, 100.0f);
    // act
    fixture.box.select(0);
    // assert nothing scrolls, because without a height there is nowhere to scroll to (§8.2)
    EXPECT_FLOAT_EQ(fixture.box.scroll_view().offset(), 0.0f);
}

TEST(ListBoxChecks, TheSemanticNodeNamesTheList) {
    // arrange
    Fixture fixture;
    fixture.box.set_explicit_id("files");
    fixture.box.set_disabled(true);
    fixture.box.arrange(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{100.0f, 200.0f}));
    // act
    const SemanticNode node = fixture.box.semantic_node();
    // assert the node carries the identity and the states an adapter reads (§10.1, AD-11)
    EXPECT_EQ(node.role(), SemanticRole::List);
    EXPECT_EQ(node.explicit_id(), "files");
    EXPECT_EQ(node.name(), "files");
    EXPECT_TRUE(node.disabled());
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{100.0f, 200.0f})));
}

TEST(ListBoxChecks, TheMaterializedRowsAreTheSemanticChildren) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    // act, assert only the built rows are announced, because only they exist to read (§10.1, AD-13)
    EXPECT_EQ(fixture.box.semantic_child_count(), 3U);
    // act a position past the built rows
    fixture.box.set_item_count(5);
    // assert the child count follows the built rows, not the item count (§10.1, AD-13)
    EXPECT_EQ(fixture.box.semantic_child_count(), 3U);
}

TEST(ListBoxChecks, APlainListHasNoSemanticChildren) {
    // arrange a list with no scrollable content
    Fixture fixture;
    // act, assert a list with nothing built announces no row (§10.1)
    EXPECT_EQ(fixture.box.semantic_child_count(), 0U);
    EXPECT_EQ(fixture.box.semantic_child(0), nullptr);
}

TEST(ListBoxChecks, ARowCarriesTheSelectedAndFocusedState) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    fixture.box.select(1);
    // act
    const std::unique_ptr<SemanticProvider> selected = fixture.box.semantic_child(1);
    const std::unique_ptr<SemanticProvider> other = fixture.box.semantic_child(0);
    // assert the chosen row carries selected and focused and its sibling carries neither (§10.1)
    ASSERT_NE(selected, nullptr);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(selected->semantic_node().role(), SemanticRole::ListItem);
    EXPECT_TRUE(selected->semantic_node().selected());
    EXPECT_TRUE(selected->semantic_node().focused());
    EXPECT_FALSE(other->semantic_node().selected());
    EXPECT_FALSE(other->semantic_node().focused());
}

TEST(ListBoxChecks, ARowIsPlacedByTheItemItShows) {
    // arrange a window that starts past the top, so a materialized position is not the item index (§8.2, AD-13)
    Fixture fixture;
    for (int index = 0; index < 6; index++)
        fixture.add_row("row" + std::to_string(index));
    fixture.content->first = 2;
    fixture.box.set_item_count(6);
    fixture.box.select(3);
    // act the first built row is position zero, which shows item two (§8.2)
    const std::unique_ptr<SemanticProvider> first_built = fixture.box.semantic_child(0);
    const std::unique_ptr<SemanticProvider> chosen = fixture.box.semantic_child(1);
    // assert the selected state follows the item, not the position (§10.1, AD-13)
    ASSERT_NE(first_built, nullptr);
    ASSERT_NE(chosen, nullptr);
    EXPECT_FALSE(first_built->semantic_node().selected());
    EXPECT_TRUE(chosen->semantic_node().selected());
}

TEST(ListBoxChecks, AChildPastTheBuiltRowsIsNothing) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    // act
    const std::unique_ptr<SemanticProvider> past = fixture.box.semantic_child(5);
    // assert a position past the built rows names no child, which the lazy walk expects (§10.1)
    EXPECT_EQ(past, nullptr);
}

TEST(ListBoxChecks, AProviderRowKeepsItsOwnNode) {
    // arrange a row that carries its own semantics (§10.1)
    Fixture fixture;
    fixture.content->add_semantic_row();
    fixture.source.keys.push_back("a");
    fixture.box.set_item_count(1);
    // act
    const std::unique_ptr<SemanticProvider> row = fixture.box.semantic_child(0);
    // assert the row's own node is decorated into a list item rather than replaced (§10.1)
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->semantic_node().role(), SemanticRole::ListItem);
    EXPECT_EQ(row->semantic_node().name(), "cell");
    // assert the row's children are forwarded, because a list adds none of its own (§10.1)
    EXPECT_EQ(row->semantic_child_count(), 1U);
    EXPECT_NE(row->semantic_child(0), nullptr);
    EXPECT_EQ(row->semantic_child(1), nullptr);
}

TEST(ListBoxChecks, ARowActivationSelectsAndRaisesIt) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    std::vector<std::string> raised;
    fixture.box.set_on_activate([&raised](const std::string& key) { raised.push_back(key); });
    // act the activation action an adapter invokes is run (§10.1, §18.1)
    const std::unique_ptr<SemanticProvider> row = fixture.box.semantic_child(2);
    ASSERT_NE(row, nullptr);
    const bool invoked = row->semantic_node().invoke("activate");
    // assert the row is selected and raised with its identity (§18.1, §18.3)
    EXPECT_TRUE(invoked);
    EXPECT_EQ(fixture.box.selected_index(), 2U);
    ASSERT_EQ(raised.size(), 1U);
    EXPECT_EQ(raised.at(0), "c");
}

TEST(ListBoxChecks, TheListBuildsATreeOfRows) {
    // arrange
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.box.select(1);
    // act
    SemanticTree tree;
    tree.build_from(fixture.box);
    // assert the walk reaches the list and its rows, which is what an adapter publishes (§10.1)
    EXPECT_EQ(tree.count_role(SemanticRole::List), 1U);
    EXPECT_EQ(tree.count_role(SemanticRole::ListItem), 2U);
}

TEST(ListBoxChecks, ASelectionAskedForBeforeTheRowsIsAppliedWhenTheyExist) {
    // arrange
    Fixture fixture;
    fixture.source.keys.push_back("a");
    fixture.source.keys.push_back("b");
    fixture.box.set_selected_index(1);
    // assert nothing holds yet, because no row exists to select (§18.3)
    EXPECT_FALSE(fixture.box.has_selection());
    // act the rows arrive, which is what the generated wiring does in one step (§11.3, §18.3)
    fixture.content->add_plain_row();
    fixture.content->add_plain_row();
    fixture.box.set_item_count(2);
    // assert the asked row is selected, with the identity the reader names (§18.3, AD-11)
    EXPECT_TRUE(fixture.box.has_selection());
    EXPECT_EQ(fixture.box.selected_index(), 1U);
    EXPECT_EQ(fixture.box.selected_key().text(), "b");
    EXPECT_EQ(fixture.box.focused_index(), 1U);
}

TEST(ListBoxChecks, ASelectionAskedForBeforeTheRowsSurvivesAnEmptyCount) {
    // arrange
    Fixture fixture;
    fixture.source.keys.push_back("a");
    fixture.box.set_selected_index(0);
    // act a count that still holds no row must not lose the ask (§18.3)
    fixture.box.set_item_count(0);
    // assert the ask is still pending, so a later read applies it (§18.3)
    EXPECT_FALSE(fixture.box.has_selection());
    fixture.content->add_plain_row();
    fixture.box.set_item_count(1);
    EXPECT_TRUE(fixture.box.has_selection());
    EXPECT_EQ(fixture.box.selected_key().text(), "a");
}

TEST(ListBoxChecks, TheOverscanReachesTheInternalView) {
    // arrange, act
    Fixture fixture;
    fixture.box.set_overscan(32.0f);
    // assert the internal view prefetches the band the list was given (§8.2, AD-13)
    EXPECT_EQ(fixture.box.scroll_view().overscan(), 32.0f);
}

TEST(ListBoxChecks, APressSelectsTheRowUnderThePointer) {
    // arrange three rows of twenty in a tall viewport (§8.2)
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    // act a press lands on the third row, which spans y in [40, 60) (§8.2)
    Event press{.kind = EventKind::PointerDown, .position = Point{5.0f, 45.0f}};
    fixture.box.dispatch_event(press);
    // assert the list claimed the press and chose the row it landed on (§8.3, §18.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(fixture.box.focused_index(), 2U);
    EXPECT_EQ(fixture.box.selected_index(), 2U);
    EXPECT_EQ(fixture.box.selected_key().text(), "c");
}

TEST(ListBoxChecks, APressPastTheRowsIsLeftToAParent) {
    // arrange two rows in a viewport taller than them, so there is empty space below (§8.2)
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    // act a press lands well below the last row (§8.2)
    Event press{.kind = EventKind::PointerDown, .position = Point{5.0f, 90.0f}};
    fixture.box.dispatch_event(press);
    // assert the list left the press alone, because it names no row (§8.3)
    EXPECT_FALSE(press.handled);
    EXPECT_FALSE(fixture.box.has_selection());
}

TEST(ListBoxChecks, ADisabledListClaimsNoPress) {
    // arrange a list that refuses input (§8.3)
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    fixture.box.set_disabled(true);
    // act a press lands on a row (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{5.0f, 5.0f}};
    fixture.box.dispatch_event(press);
    // assert a disabled list refuses the press, which is what the state means (§8.3)
    EXPECT_FALSE(press.handled);
    EXPECT_FALSE(fixture.box.has_selection());
}

TEST(ListBoxChecks, TheInternalViewportLeavesTheNavigationKeysToTheList) {
    // arrange a scrollable list whose rows fill more than the viewport (§8.2)
    Fixture fixture;
    for (int index = 0; index < 10; index++)
        fixture.add_row("row" + std::to_string(index));
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act the key is offered to the hosted viewport directly (§8.3)
    fixture.box.scroll_view().dispatch_event(down);
    // assert the viewport does not claim a key the list owns (§8.3)
    EXPECT_FALSE(down.handled);
    // assert it did not scroll either, which is what a claim would have done (§8.2)
    EXPECT_FLOAT_EQ(fixture.box.scroll_view().offset(), 0.0f);
}

TEST(ListBoxChecks, ANavigationKeyBubblesPastTheInternalViewportToTheList) {
    // arrange a scrollable list with the caret on a row, which is the case an earlier revision left to the viewport (§8.3)
    Fixture fixture;
    for (int index = 0; index < 10; index++)
        fixture.add_row("row" + std::to_string(index));
    fixture.box.set_row_height(20.0f);
    fixture.layout(200.0f, 100.0f);
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act the key is routed from the row up through the viewport, which is the real path (§8.3)
    InputDispatcher::dispatch_to_focused(fixture.box, down, fixture.content);
    // assert the list claimed the key and stepped its selection, which is what a list means (§8.3, §18.3)
    EXPECT_TRUE(down.handled);
    EXPECT_EQ(fixture.box.focused_index(), 1U);
    EXPECT_EQ(fixture.box.selected_index(), 1U);
    // assert the viewport did not scroll instead, which is the defect this guards (§8.2)
    EXPECT_FLOAT_EQ(fixture.box.scroll_view().offset(), 0.0f);
}

TEST(ListBoxChecks, TheSemanticNodeDrivesSelectionTheWayTheKeysDo) {
    // arrange a list of three rows, whose activation is watched (§18.1)
    Fixture fixture;
    fixture.add_row("a");
    fixture.add_row("b");
    fixture.add_row("c");
    std::vector<std::string> raised;
    fixture.box.set_on_activate([&raised](const std::string& key) { raised.push_back(key); });
    const SemanticNode node = fixture.box.semantic_node();
    // assert the navigation actions are listed, so a client reads them before acting (§10.1, §10.3)
    EXPECT_TRUE(node.has_action("select_next"));
    EXPECT_TRUE(node.has_action("select_previous"));
    EXPECT_TRUE(node.has_action("select_first"));
    EXPECT_TRUE(node.has_action("select_last"));
    EXPECT_TRUE(node.has_action("activate"));
    // act the next-row action runs, which moves the selection onto the second row (§10.1)
    ASSERT_TRUE(node.invoke("select_next"));
    EXPECT_EQ(fixture.box.selected_index(), 1U);
    EXPECT_EQ(fixture.box.selected_key().text(), "b");
    // act the last-row action runs, which lands on the final row (§10.1)
    ASSERT_TRUE(node.invoke("select_last"));
    EXPECT_EQ(fixture.box.selected_index(), 2U);
    // act the activate action runs, which selects and raises the focused row (§10.1, §18.1)
    ASSERT_TRUE(node.invoke("activate"));
    ASSERT_EQ(raised.size(), 1U);
    EXPECT_EQ(raised.at(0), "c");
}

TEST(ListBoxChecks, ARowBuiltAfterTheThemeIsInstalledInheritsIt) {
    // arrange a themed list whose rows arrive later, which is what virtualization does (§6.1, §8.2, AD-13)
    BindingEngine engine;
    ListBox box(&engine);
    box.set_viewport_extent(Size{300.0f, 60.0f});
    box.set_row_height(20.0f);
    const Theme theme = Theme::macos(Accents{}, ColorScheme::Light);
    apply_theme(box, theme);
    auto row = std::make_unique<Label>(&engine, nullptr, "row");
    Label* const row_ptr = row.get();
    box.scroll_view().add_child(std::move(row));
    // act the list is laid out, which is when the row would draw
    Size measured{};
    box.measure(SizeConstraints::unconstrained(), measured);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{300.0f, 60.0f}));
    // assert the late row resolves the installed theme, which the install walk could not reach by itself (§6.1)
    EXPECT_EQ(row_ptr->theme(), &theme);
}

TEST(ListBoxChecks, ASelectedThemedListPaintsReadableRowsAndASelectionBand) {
    // arrange a light themed list with three rows, whose rows are built after the theme is installed (§6.1, §8.2)
    BindingEngine engine;
    SkiaTextEngine text;
    ListBox box(&engine);
    box.set_viewport_extent(Size{300.0f, 60.0f});
    box.set_row_height(20.0f);
    const Theme theme = Theme::macos(Accents{.surface = Color::rgb(0.925f, 0.925f, 0.930f), .on_surface = Color::rgb(0.18f, 0.18f, 0.20f)}, ColorScheme::Light);
    apply_theme(box, theme);
    for (int index = 0; index < 3; index++)
        box.scroll_view().add_child(std::make_unique<Label>(&engine, &text, "row " + std::to_string(index)));
    box.set_item_count(3);
    // act the list and its rows are laid out, and a row is selected
    Size measured{};
    box.measure(SizeConstraints::unconstrained(), measured);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{300.0f, 60.0f}));
    box.select(1);
    for (int index = 0; index < 3; index++)
        if (auto* row = dynamic_cast<Label*>(box.scroll_view().child_at(static_cast<std::size_t>(index))))
            row->arrange(Rect::from_origin_size(Point{.x = 0.0f, .y = static_cast<float>(index) * 20.0f}, Size{.width = 300.0f, .height = 20.0f}));
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{300.0f, 40.0f}, 1.0f);
    SceneBuilder builder;
    const std::shared_ptr<const FrameScene> scene = builder.build(box, metrics, Color::rgb(0.925f, 0.925f, 0.930f));
    auto surface = software::SoftwareRenderSurface::create(metrics);
    ASSERT_NE(surface, nullptr);
    software::SoftwareRendererBackend backend;
    backend.execute_frame(*surface, *scene);
    const Screenshot shot = capture_surface(*surface);
    ASSERT_FALSE(shot.empty());
    // assert the rows draw dark text rather than the unthemed white, which is what "text cannot be read" meant (§6.1)
    bool dark_text = false;
    // assert the selected row carries a highlight, which is what shows a selection (§6.1, §18.3)
    bool selection_band = false;
    for (int y = 0; y < shot.height; y++)
        for (int x = 0; x < shot.width; x++) {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(shot.width) + static_cast<std::size_t>(x)) * 4U;
            const std::uint8_t red = shot.pixels[at];
            const std::uint8_t green = shot.pixels[at + 1];
            const std::uint8_t blue = shot.pixels[at + 2];
            // a glyph pixel is dark, which the unthemed white run never is (§6.1)
            if (red < 100U && green < 100U && blue < 100U)
                dark_text = true;
            // the selection token is a blue band over the grey surface, so a clearly blue pixel marks it (§6.1)
            if (blue > red + 20U && blue > green + 10U && blue > 150U)
                selection_band = true;
        }
    EXPECT_TRUE(dark_text);
    EXPECT_TRUE(selection_band);
}

TEST(ListBoxChecks, APressOnARowBesideTheThumbSelectsThatRow) {
    // arrange a scrollable list, so a thumb is drawn up the trailing edge over the rows (§8.2, §8.3)
    Fixture fixture;
    for (int index = 0; index < 8; index++)
        fixture.add_row("row" + std::to_string(index));
    fixture.box.set_row_height(24.0f);
    fixture.layout(320.0f, 160.0f);
    InputController controller;
    controller.attach(fixture.box);
    // act a press lands on the fourth row at the thumb's height, which the bar must not steal (§8.3)
    Event fourth{.kind = EventKind::PointerDown, .position = Point{50.0f, 84.0f}};
    controller.deliver(fourth);
    // assert the fourth row is selected, because the row is where the press landed (§18.3)
    EXPECT_EQ(fixture.box.selected_index(), 3U);
    Event release{.kind = EventKind::PointerUp, .position = Point{50.0f, 84.0f}};
    controller.deliver(release);
    // act a press on the first row, which sits beside the thumb, still reaches the list (§8.3)
    Event first{.kind = EventKind::PointerDown, .position = Point{50.0f, 12.0f}};
    controller.deliver(first);
    // assert an earlier row stays clickable after a later one is selected, which is the defect this guards (§18.3)
    EXPECT_EQ(fixture.box.selected_index(), 0U);
}
