#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model/item_key.h"
#include "model/selection_model.h"

using namespace albor;
using namespace albor::model;

TEST(SelectionModel, ABareModelSelectsNothing) {
    // arrange, act
    const SelectionModel model;
    // assert a list starts with no row chosen, which is what the sentinel index reports (§18.3)
    EXPECT_TRUE(model.empty());
    EXPECT_EQ(model.selected_index(), SelectionModel::NO_INDEX);
    EXPECT_TRUE(model.selected_key().text().empty());
}

TEST(SelectionModel, SelectingStoresTheIdentityAndThePosition) {
    // arrange
    SelectionModel model;
    // act
    model.select(ItemKey{"alpha"}, 2);
    // assert both halves are stored, because a list reads the identity and the position (§18.1, §18.3)
    EXPECT_FALSE(model.empty());
    EXPECT_EQ(model.selected_index(), 2U);
    EXPECT_EQ(model.selected_key().text(), "alpha");
}

TEST(SelectionModel, SelectingReportsTheChangeOnce) {
    // arrange
    SelectionModel model;
    std::size_t reports = 0;
    model.set_on_change([&reports] { reports++; });
    // act
    model.select(ItemKey{"alpha"}, 2);
    // assert the installed work is told once, which is what a signal reports (§18.3)
    EXPECT_EQ(reports, 1U);
}

TEST(SelectionModel, ReselectingTheSameItemIsNotAChange) {
    // arrange
    SelectionModel model;
    model.select(ItemKey{"alpha"}, 2);
    std::size_t reports = 0;
    model.set_on_change([&reports] { reports++; });
    // act the same identity at the same position is written again (§18.3)
    model.select(ItemKey{"alpha"}, 2);
    // assert nothing is reported, because the selection did not change (§18.3)
    EXPECT_EQ(reports, 0U);
}

TEST(SelectionModel, AChangedIdentityAtTheSamePositionIsAChange) {
    // arrange
    SelectionModel model;
    model.select(ItemKey{"alpha"}, 2);
    std::size_t reports = 0;
    model.set_on_change([&reports] { reports++; });
    // act a different identity lands on the same position, which a reordered list produces (§18.1)
    model.select(ItemKey{"beta"}, 2);
    // assert the identity alone decides, so the change is reported (§18.1, AD-11)
    EXPECT_EQ(reports, 1U);
    EXPECT_EQ(model.selected_key().text(), "beta");
}

TEST(SelectionModel, AClearedSelectionIsReportedOnce) {
    // arrange
    SelectionModel model;
    model.select(ItemKey{"alpha"}, 2);
    std::size_t reports = 0;
    model.set_on_change([&reports] { reports++; });
    // act
    model.clear();
    // assert the model empties and the change is reported once (§18.3)
    EXPECT_EQ(reports, 1U);
    EXPECT_TRUE(model.empty());
    EXPECT_EQ(model.selected_index(), SelectionModel::NO_INDEX);
    EXPECT_TRUE(model.selected_key().text().empty());
}

TEST(SelectionModel, ClearingAnEmptyModelIsNotAChange) {
    // arrange
    SelectionModel model;
    std::size_t reports = 0;
    model.set_on_change([&reports] { reports++; });
    // act
    model.clear();
    // assert nothing is reported, because there was nothing to clear (§18.3)
    EXPECT_EQ(reports, 0U);
}

TEST(SelectionModel, TheSelectedIdentityIsMatchedByKey) {
    // arrange
    SelectionModel model;
    model.select(ItemKey{"alpha"}, 2);
    // act, assert only the chosen identity matches, because a row asks about itself (§18.1, AD-11)
    EXPECT_TRUE(model.is_selected(ItemKey{"alpha"}));
    EXPECT_FALSE(model.is_selected(ItemKey{"beta"}));
}

TEST(SelectionModel, NothingMatchesWhileNothingIsSelected) {
    // arrange
    const SelectionModel model;
    // act, assert an empty model matches no identity, because there is nothing to compare (§18.1)
    EXPECT_FALSE(model.is_selected(ItemKey{"alpha"}));
}

TEST(SelectionModel, AChangeWithNoWorkInstalledIsQuiet) {
    // arrange
    SelectionModel model;
    // act, assert a bare model reports through nothing without raising, which a list without a signal is (§18.3)
    model.select(ItemKey{"alpha"}, 1);
    model.clear();
    EXPECT_TRUE(model.empty());
}

TEST(SelectionModel, TheInstalledWorkIsTheOneTold) {
    // arrange
    SelectionModel model;
    std::size_t first = 0;
    std::size_t second = 0;
    model.set_on_change([&first] { first++; });
    // act the work is swapped for a later listener (§18.3)
    model.set_on_change([&second] { second++; });
    model.select(ItemKey{"alpha"}, 1);
    // assert only the current work is told, because the swapped one is gone (§18.3)
    EXPECT_EQ(first, 0U);
    EXPECT_EQ(second, 1U);
}
