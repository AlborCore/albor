#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model/item_key.h"
#include "model/keyed_view.h"
#include "model/scrollable_rows.h"
#include "ui/ui_node.h"

using namespace albor;
using namespace albor::model;

namespace
{
    // one row the view holds, which carries the value it was built from (§18.1, AD-13)
    struct Recorded
    {
        // the identity the item carries, which is what a row is keyed by (§18.1, AD-13)
        std::string key;

        // the text the row shows, which is what an update rewrites (§18.1)
        std::string text;
    };

    // build a plain node, because a row is any node the container can hold (§18.1)
    std::unique_ptr<UiNode> make_row(void*, const Recorded&, std::size_t) {
        // the node is allocated, because a row is materialized rather than borrowed (§18.1)
        return std::make_unique<UiNode>();
    }

    // rewrite a row, which is what a matched item does (§18.1, AD-13)
    void update_row(void*, UiNode&, const Recorded& item, std::size_t) {
        // nothing is recorded, because the test reads identity rather than content (§18.1, AD-13)
        static_cast<void>(item);
    }

    // read the identity an item carries, which is the field the record declared (§18.1, AD-13)
    ItemKey key_of(void*, const Recorded& item) {
        // the key is read, because that is what the view matches on (§18.1, AD-13)
        return ItemKey(item.key);
    }

    // how many rows the counting builder has materialized, which is what proves a scroll recycles (§8.2, AD-13)
    std::size_t builds = 0;

    // how many rows the counting updater has rewritten, which proves a match is updated in place (§18.1, AD-13)
    std::size_t writes = 0;

    // build a row and count it, because a rebuild is what recycling avoids (§8.2, AD-13)
    std::unique_ptr<UiNode> counting_row(void*, const Recorded&, std::size_t) {
        // the count rises before the node is handed back, so a test reads what the scroll allocated (§8.2, AD-13)
        builds++;
        return std::make_unique<UiNode>();
    }

    // a second updater, which exists so a view change is observable (§18.1, AD-13)
    void counting_update_2(void*, UiNode&, const Recorded&, std::size_t) {
        // the count rises once per row the replacement updater rewrote (§18.1, AD-13)
        writes++;
    }

    // rewrite a row and count it, because a matched item is updated rather than rebuilt (§18.1, AD-13)
    void counting_update(void*, UiNode&, const Recorded&, std::size_t) {
        // the count rises once per row the view rewrote (§18.1, AD-13)
        writes++;
    }
} // namespace

TEST(KeyedView, AViewBuildsOneRowPerItem) {
    // arrange a parent, because rows hang from the container that holds them (§8.1, §18.1)
    UiNode parent;
    // the view is built over the parent at the offset its repeat was written (§18.1, AD-13)
    KeyedView<Recorded> view(&parent, 0);
    // act
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "one"}, {.key = "b", .text = "two"}}, &make_row, &update_row, &key_of);
    // assert one row per item was materialized (§18.1, AD-13)
    EXPECT_EQ(view.size(), 2U);
    EXPECT_EQ(parent.child_count(), 2U);
}

TEST(KeyedView, AMatchedRowIsReusedRatherThanRebuilt) {
    // arrange
    UiNode parent;
    KeyedView<Recorded> view(&parent, 0);
    // act the rows are materialized once (§18.1, AD-13)
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "one"}}, &make_row, &update_row, &key_of);
    // act the same node is recorded, which is what the reuse check needs (§18.1, AD-13)
    UiNode* before = view.row_at(0);
    // act the item is updated in place, which must not rebuild the row (§18.1, AD-13)
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "changed"}}, &make_row, &update_row, &key_of);
    // assert the row is the same node, because identity decided the match (§18.1, AD-13)
    EXPECT_EQ(view.row_at(0), before);
}

TEST(KeyedView, ARemovedItemDestroysOnlyItsRow) {
    // arrange
    UiNode parent;
    KeyedView<Recorded> view(&parent, 0);
    // act three rows are materialized (§18.1)
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "one"}, {.key = "b", .text = "two"}, {.key = "c", .text = "three"}}, &make_row, &update_row, &key_of);
    // act the middle item is removed, which is what a positional loop gets wrong (§18.1, AD-13)
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "one"}, {.key = "c", .text = "three"}}, &make_row, &update_row, &key_of);
    // assert the row count follows the item count, because identity decided what went (§18.1, AD-13)
    EXPECT_EQ(view.size(), 2U);
    EXPECT_EQ(parent.child_count(), 2U);
    // assert the surviving keys are the ones the items named (§18.1, AD-13)
    EXPECT_EQ(view.key_at(0).text(), "a");
    EXPECT_EQ(view.key_at(1).text(), "c");
}

TEST(KeyedView, AnItemMoveKeepsItsRowAndReordersTheParent) {
    // arrange
    UiNode parent;
    KeyedView<Recorded> view(&parent, 0);
    // act three rows are materialized (§18.1)
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "one"}, {.key = "b", .text = "two"}}, &make_row, &update_row, &key_of);
    // act the row of the first item is recorded, which is what a move must preserve (§18.1, AD-13)
    UiNode* moved = view.row_at(0);
    // act the items are reversed, which is what makes identity visible (§18.1, AD-13)
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "b", .text = "two"}, {.key = "a", .text = "one"}}, &make_row, &update_row, &key_of);
    // assert the recorded row is now last, because it moved rather than being rebuilt (§18.1, AD-13)
    EXPECT_EQ(view.row_at(1), moved);
}

TEST(KeyedView, RowsRespectTheOffsetTheirRepeatWasWritten) {
    // arrange a parent that already holds a sibling, because a repeat keeps its written place (§8.1, §18.1)
    UiNode parent;
    // the sibling is added first, which is what the offset counts (§8.1, §18.1)
    parent.add_child(std::make_unique<UiNode>());
    // the view is built at the offset one, because the sibling is before it (§18.1, AD-13)
    KeyedView<Recorded> view(&parent, 1);
    // act
    view.reconcile(nullptr, std::vector<Recorded>{{.key = "a", .text = "one"}}, &make_row, &update_row, &key_of);
    // assert the row sits after the sibling, because the offset is where the repeat was written (§8.1, §18.1)
    EXPECT_EQ(parent.child_count(), 2U);
    EXPECT_NE(parent.child_at(0), view.row_at(0));
}

TEST(ItemKey, AnIntKeyAndItsTextSpellingNameTheSameItem) {
    // arrange a key built from a number, which is what an integer field contributes (§18.1, AD-13)
    const ItemKey from_int = ItemKey::from_int(42);
    // act the same identity written as text (§18.1, AD-13)
    const ItemKey from_text("42");
    // assert the two name the same item, because a key is compared as text (§18.1, AD-13)
    EXPECT_TRUE(from_int == from_text);
}

TEST(ScrollableRowsChecks, OnlyTheWindowIsMaterialized) {
    // arrange a thousand rows, which is the scale the requirement names (§8.2, AD-13)
    std::vector<Recorded> items;
    items.reserve(1000);
    for (std::size_t index = 0; index < 1000; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    builds = 0;
    writes = 0;
    // act a window showing the top of the content
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 20000.0f}, .offset = 0.0f, .overscan = 0.0f});
    // assert the whole list is not built, which is the whole point of the requirement (§8.2, AD-13)
    EXPECT_EQ(rows.materialized(), 10u);
    EXPECT_EQ(rows.first_index(), 0u);
    EXPECT_EQ(builds, 10u);
    EXPECT_FLOAT_EQ(rows.content_extent().height, 20000.0f);
}

TEST(ScrollableRowsChecks, TheOverscanKeepsRowsPastTheViewportBuilt) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(1000);
    for (std::size_t index = 0; index < 1000; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    builds = 0;
    writes = 0;
    // act a window with a margin above and below the visible area
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 20000.0f}, .offset = 200.0f, .overscan = 100.0f});
    // assert the window starts before the offset and reaches past the viewport (§8.2, AD-13)
    EXPECT_EQ(rows.first_index(), 5u);
    EXPECT_EQ(rows.materialized(), 20u);
}

TEST(ScrollableRowsChecks, ScrollingOneRowRecyclesRatherThanRebuilds) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(1000);
    for (std::size_t index = 0; index < 1000; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 20000.0f}, .offset = 0.0f, .overscan = 0.0f});
    builds = 0;
    writes = 0;
    // act a scroll that moves the window by exactly one row
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 20000.0f}, .offset = 20.0f, .overscan = 0.0f});
    // assert the window moved one row (§8.2, AD-13)
    EXPECT_EQ(rows.first_index(), 1u);
    EXPECT_EQ(rows.materialized(), 10u);
    // assert nothing was allocated, because the row that left was recycled into the one that entered (§8.2, AD-13)
    EXPECT_EQ(builds, 0u);
    // assert every row in the window was rewritten, because its item moved to a different index (§8.2, AD-13)
    EXPECT_EQ(writes, 10u);
}

TEST(ScrollableRowsChecks, AScrollPastTheWindowAllocatesOnlyTheNewRows) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(1000);
    for (std::size_t index = 0; index < 1000; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 20000.0f}, .offset = 0.0f, .overscan = 0.0f});
    builds = 0;
    writes = 0;
    // act a scroll that moves the window past every row it holds
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 20000.0f}, .offset = 1000.0f, .overscan = 0.0f});
    // assert the window moved far past the rows it held (§8.2, AD-13)
    EXPECT_EQ(rows.first_index(), 50u);
    EXPECT_EQ(rows.materialized(), 10u);
    // assert nothing was allocated, because the ten rows that left are exactly the ten that entered (§8.2, AD-13)
    EXPECT_EQ(builds, 0u);
    // assert the ten rows that left are held ready for a later scroll (§8.2, AD-13)
    EXPECT_EQ(rows.recycled(), 0u);
}

TEST(ScrollableRowsChecks, EachRowSitsAtItsOwnPlaceInTheContent) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 200.0f, .overscan = 0.0f});
    Size measured{};
    rows.measure(SizeConstraints::at_most(Size{200.0f, 2000.0f}), measured);
    // act the viewport already moved the content, so the first built row starts a hundred in (§8.2)
    rows.arrange(Rect::from_origin_size(Point{0.0f, -200.0f}, Size{200.0f, 2000.0f}));
    // assert the content origin is already the scrolled place, so the tenth row lands at the viewport top (§8.2, AD-13)
    EXPECT_FLOAT_EQ(rows.child_at(0)->bounds().y, 0.0f);
    // assert the tenth built row is the nineteenth item, which is what puts it a hundred and eighty down (§8.2, AD-13)
    EXPECT_FLOAT_EQ(rows.child_at(9)->bounds().y, 180.0f);
    // assert a row is exactly as tall as the uniform height the view was built with (§8.2, AD-13)
    EXPECT_FLOAT_EQ(rows.child_at(9)->bounds().height, 20.0f);
}

TEST(ScrollableRowsChecks, ASurvivingRowIsUpdatedRatherThanRebuilt) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 0.0f, .overscan = 0.0f});
    builds = 0;
    writes = 0;
    // act the item a built row shows is renamed, which is a content change rather than a scroll (§18.1, AD-13)
    items.at(2).text = "rewritten";
    rows.set_items(items);
    // assert nothing was allocated, because every identity in the window matched (§18.1, AD-13)
    EXPECT_EQ(rows.materialized(), 10u);
    EXPECT_EQ(builds, 0u);
    EXPECT_EQ(writes, 10u);
}

TEST(ScrollableRowsChecks, AWindowThatReachesPastTheLastItemStopsThere) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(12);
    for (std::size_t index = 0; index < 12; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    builds = 0;
    writes = 0;
    // act a window that reaches well past the last item
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 1000.0f}, .content = Size{200.0f, 240.0f}, .offset = 0.0f, .overscan = 0.0f});
    // assert there is no row past the last item, because there is no item there (§8.2, AD-13)
    EXPECT_EQ(rows.materialized(), 12u);
    EXPECT_FLOAT_EQ(rows.content_extent().height, 240.0f);
}

TEST(ScrollableRowsChecks, AChangedBuilderDropsTheRowsItCannotRead) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 0.0f, .overscan = 0.0f});
    builds = 0;
    // act the view is swapped for a different updater, which cannot read rows the old one made (§18.1, AD-13)
    rows.set_view(&counting_row, &counting_update_2, &key_of, nullptr);
    // assert the rows are rebuilt rather than reused, because a new updater would cast them wrongly (§18.1, AD-13)
    EXPECT_EQ(rows.materialized(), 10u);
    // assert no replaced row was held back, because a pooled row carries the old updater's expectations (§18.1, AD-13)
    EXPECT_EQ(rows.recycled(), 0u);
    // assert all ten were rebuilt, because a pooled row carries the old updater's expectations (§18.1, AD-13)
    EXPECT_EQ(builds, 10u);
}

TEST(ScrollableRowsChecks, AnUnchangedBuilderKeepsTheRowsItCanRead) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 0.0f, .overscan = 0.0f});
    UiNode* before = rows.child_at(0);
    builds = 0;
    // act the same three functions are installed again, which changes nothing (§13.3)
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    // assert the rows survive, because the new updater is the one that made them (§18.1, AD-13)
    EXPECT_EQ(rows.child_at(0), before);
    EXPECT_EQ(builds, 0u);
}

TEST(ScrollableRowsChecks, EveryBuiltRowIsMeasuredAsItIsAdopted) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    // act the window is pushed with no measure pass before it, which is what arrange does (§8.2)
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 0.0f, .overscan = 0.0f});
    // assert each row was measured on adoption, because the frame that built it had already measured (§8.2)
    ASSERT_EQ(rows.materialized(), 10u);
    EXPECT_EQ(rows.child_at(0)->measured_size().height, 20.0f);
    EXPECT_FLOAT_EQ(rows.child_at(0)->measured_size().width, 200.0f);
}

TEST(ScrollableRowsChecks, ARowThatEntersOnALaterScrollIsMeasuredToo) {
    // arrange
    std::vector<Recorded> items;
    items.reserve(100);
    for (std::size_t index = 0; index < 100; index++)
        items.push_back(Recorded{.key = "row" + std::to_string(index), .text = "line " + std::to_string(index)});
    ScrollableRows<Recorded> rows{20.0f, 64};
    rows.set_items(items);
    rows.set_view(&counting_row, &counting_update, &key_of, nullptr);
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 0.0f, .overscan = 0.0f});
    // act a scroll past every row the window held, so every built row is a recycled one (§8.2, AD-13)
    rows.set_scroll_viewport(ScrollViewport{.viewport = Size{200.0f, 200.0f}, .content = Size{200.0f, 2000.0f}, .offset = 1000.0f, .overscan = 0.0f});
    // assert the rows that arrived were measured, because nothing measured them afterwards (§8.2)
    ASSERT_EQ(rows.materialized(), 10u);
    EXPECT_EQ(rows.first_index(), 50u);
    EXPECT_FLOAT_EQ(rows.child_at(0)->measured_size().height, 20.0f);
}
