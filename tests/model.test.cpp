#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model/item_key.h"
#include "model/keyed_view.h"
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
