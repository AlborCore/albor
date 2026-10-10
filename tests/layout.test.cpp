#include <memory>

#include <gtest/gtest.h>

#include "layout/column_layout.h"
#include "layout/grid_layout.h"
#include "layout/padding_layout.h"
#include "layout/row_layout.h"
#include "layout/stack_layout.h"

using namespace albor;

namespace
{
    // the node test double, which reports a fixed extent and counts the passes it received (§8.2)
    class LayoutTestNode final : public UiNode
    {
    public:
        // how many times the node was measured (§8.2)
        std::size_t measures = 0;

        // how many times the node was arranged (§8.2)
        std::size_t arranges = 0;

        // the extent this node reports when it measures itself (§8.2)
        Size intrinsic{10.0f, 20.0f};

        // the constraints the last measure was offered, which a test asserts on (§8.2)
        SizeConstraints offered;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the pass is counted, so a test proves the measure phase ran (§8.2)
            measures++;
            // the offer is kept, so a test can prove the container offered a ceiling (§8.2)
            offered = constraints;
            // the intrinsic extent is clamped into the offered constraints, which is what a node does (§8.2)
            out_size = constraints.constrain(intrinsic);
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            // the pass is counted, so a test proves the arrange phase ran (§8.2)
            arranges++;
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }
    };
} // namespace

TEST(StackLayoutChecks, EmptyStackMeasuresAsNothing) {
    // arrange
    StackLayout stack;
    Size measured{};
    // act
    stack.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(StackLayoutChecks, MeasuresAsTheLargestChild) {
    // arrange
    StackLayout stack;
    auto small = std::make_unique<LayoutTestNode>();
    auto large = std::make_unique<LayoutTestNode>();
    small->intrinsic = Size{30.0f, 10.0f};
    large->intrinsic = Size{20.0f, 40.0f};
    stack.add_child(std::move(small));
    stack.add_child(std::move(large));
    Size measured{};
    // act
    stack.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(StackLayoutChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    StackLayout stack;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{100.0f, 80.0f};
    stack.add_child(std::move(child));
    Size measured{};
    // act
    stack.measure(SizeConstraints::at_most(Size{50.0f, 50.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 50.0f);
    EXPECT_FLOAT_EQ(measured.height, 50.0f);
}

TEST(StackLayoutChecks, AChildIsOfferedTheCeilingOfTheStack) {
    // arrange
    StackLayout stack;
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    stack.add_child(std::move(child));
    Size measured{};
    // act
    stack.measure(SizeConstraints::at_most(Size{90.0f, 70.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.width, 90.0f);
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.height, 70.0f);
}

TEST(StackLayoutChecks, ArrangesEveryChildToTheWholeBounds) {
    // arrange
    StackLayout stack;
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    stack.add_child(std::move(first));
    stack.add_child(std::move(second));
    const Rect bounds = Rect::from_origin_size(Point{5.0f, 6.0f}, Size{70.0f, 80.0f});
    // act
    stack.arrange(bounds);
    // assert
    EXPECT_EQ(first_ptr->bounds(), bounds);
    EXPECT_EQ(second_ptr->bounds(), bounds);
    EXPECT_EQ(stack.bounds(), bounds);
}

TEST(StackLayoutChecks, MeasureDoesNotArrange) {
    // arrange
    StackLayout stack;
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    stack.add_child(std::move(child));
    Size measured{};
    // act
    stack.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_EQ(child_ptr->measures, 1u);
    EXPECT_EQ(child_ptr->arranges, 0u);
}

TEST(RowLayoutChecks, DefaultsToNoGap) {
    // arrange
    RowLayout row;
    // act assert
    EXPECT_FLOAT_EQ(row.gap(), 0.0f);
}

TEST(RowLayoutChecks, EmptyRowMeasuresAsNothing) {
    // arrange
    RowLayout row;
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(RowLayoutChecks, SumsWidthsAndTakesTheTallestChild) {
    // arrange
    RowLayout row;
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{15.0f, 35.0f};
    row.add_child(std::move(first));
    row.add_child(std::move(second));
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 25.0f);
    EXPECT_FLOAT_EQ(measured.height, 35.0f);
}

TEST(RowLayoutChecks, AddsOneGapBetweenChildrenOnly) {
    // arrange
    RowLayout row;
    row.set_gap(4.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 20.0f};
    third->intrinsic = Size{10.0f, 20.0f};
    row.add_child(std::move(first));
    row.add_child(std::move(second));
    row.add_child(std::move(third));
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 38.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(RowLayoutChecks, HonoursTheMaximumConstraint) {
    // arrange
    RowLayout row;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{100.0f, 50.0f};
    row.add_child(std::move(child));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{40.0f, 40.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(RowLayoutChecks, AChildIsOfferedTheCeilingOfTheRow) {
    // arrange
    RowLayout row;
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    row.add_child(std::move(child));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{120.0f, 60.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.width, 120.0f);
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.height, 60.0f);
}

TEST(RowLayoutChecks, PlacesChildrenLeftToRightWithGaps) {
    // arrange
    RowLayout row;
    row.set_gap(5.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 20.0f};
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    row.add_child(std::move(first));
    row.add_child(std::move(second));
    Size measured{};
    row.measure(SizeConstraints::unconstrained(), measured);
    const Rect bounds = Rect::from_origin_size(Point{10.0f, 0.0f}, Size{100.0f, 40.0f});
    // act
    row.arrange(bounds);
    // assert
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().width, 10.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().height, 40.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 25.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().width, 10.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().height, 40.0f);
}

TEST(RowLayoutChecks, KeepsTheMeasuredWidthUnderTheAvailableHeight) {
    // arrange
    RowLayout row;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{25.0f, 5.0f};
    LayoutTestNode* child_ptr = child.get();
    row.add_child(std::move(child));
    Size measured{};
    row.measure(SizeConstraints::unconstrained(), measured);
    // act
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 60.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 25.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 60.0f);
}

TEST(RowLayoutChecks, GapChangeInvalidatesLayout) {
    // arrange
    RowLayout row;
    row.clear_dirty();
    // act
    row.set_gap(3.0f);
    // assert
    EXPECT_TRUE(row.layout_dirty());
    EXPECT_FLOAT_EQ(row.gap(), 3.0f);
}

TEST(RowLayoutChecks, UnchangedGapDoesNotInvalidate) {
    // arrange
    RowLayout row;
    row.set_gap(3.0f);
    row.clear_dirty();
    // act
    row.set_gap(3.0f);
    // assert
    EXPECT_FALSE(row.layout_dirty());
}

TEST(ColumnLayoutChecks, EmptyColumnMeasuresAsNothing) {
    // arrange
    ColumnLayout column;
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(ColumnLayoutChecks, SumsHeightsAndTakesTheWidestChild) {
    // arrange
    ColumnLayout column;
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{35.0f, 15.0f};
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 35.0f);
    EXPECT_FLOAT_EQ(measured.height, 35.0f);
}

TEST(ColumnLayoutChecks, AddsOneGapBetweenChildrenOnly) {
    // arrange
    ColumnLayout column;
    column.set_gap(6.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 20.0f};
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(measured.height, 46.0f);
}

TEST(ColumnLayoutChecks, PlacesChildrenTopToBottomWithGaps) {
    // arrange
    ColumnLayout column;
    column.set_gap(2.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 30.0f};
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    Size measured{};
    column.measure(SizeConstraints::unconstrained(), measured);
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 8.0f}, Size{80.0f, 200.0f});
    // act
    column.arrange(bounds);
    // assert
    EXPECT_FLOAT_EQ(first_ptr->bounds().y, 8.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().height, 20.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().width, 80.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 30.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().height, 30.0f);
}

TEST(ColumnLayoutChecks, GapChangeInvalidatesLayout) {
    // arrange
    ColumnLayout column;
    column.clear_dirty();
    // act
    column.set_gap(9.0f);
    // assert
    EXPECT_TRUE(column.layout_dirty());
    EXPECT_FLOAT_EQ(column.gap(), 9.0f);
}

TEST(ColumnLayoutChecks, UnchangedGapDoesNotInvalidate) {
    // arrange
    ColumnLayout column;
    column.set_gap(9.0f);
    column.clear_dirty();
    // act
    column.set_gap(9.0f);
    // assert
    EXPECT_FALSE(column.layout_dirty());
}

TEST(ColumnLayoutChecks, HonoursTheMaximumConstraint) {
    // arrange
    ColumnLayout column;
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 100.0f};
    second->intrinsic = Size{10.0f, 100.0f};
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{50.0f, 120.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.height, 120.0f);
}

TEST(ColumnLayoutChecks, AChildIsOfferedTheCeilingOfTheColumnMinusItsPadding) {
    // arrange
    ColumnLayout column;
    column.set_padding(10.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{200.0f, 300.0f};
    LayoutTestNode* child_ptr = child.get();
    column.add_child(std::move(child));
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{100.0f, 200.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.width, 80.0f);
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.height, 180.0f);
    EXPECT_FLOAT_EQ(measured.width, 100.0f);
    EXPECT_FLOAT_EQ(measured.height, 200.0f);
}

TEST(PaddingLayoutChecks, EmptyInsetsAreTheDefault) {
    // arrange
    const EdgeInsets insets{};
    // act assert
    EXPECT_TRUE(insets.empty());
}

TEST(PaddingLayoutChecks, InsetsCompareFieldByField) {
    // arrange
    const EdgeInsets lhs{.top = 1.0f, .right = 2.0f, .bottom = 3.0f, .left = 4.0f};
    const EdgeInsets rhs{.top = 1.0f, .right = 2.0f, .bottom = 3.0f, .left = 4.0f};
    const EdgeInsets other{.top = 1.0f, .right = 2.0f, .bottom = 3.0f, .left = 5.0f};
    // act assert
    EXPECT_EQ(lhs, rhs);
    EXPECT_NE(lhs, other);
    EXPECT_FALSE(lhs.empty());
}

TEST(PaddingLayoutChecks, WrapperWithoutChildMeasuresAsItsInsets) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(5.0f);
    Size measured{};
    // act
    padding.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
}

TEST(PaddingLayoutChecks, UniformPaddingAddsToTheChild) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(5.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{20.0f, 30.0f};
    padding.add_child(std::move(child));
    Size measured{};
    // act
    padding.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(PaddingLayoutChecks, PerEdgePaddingAddsEachEdge) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(1.0f, 2.0f, 3.0f, 4.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{20.0f, 30.0f};
    padding.add_child(std::move(child));
    Size measured{};
    // act
    padding.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 26.0f);
    EXPECT_FLOAT_EQ(measured.height, 34.0f);
}

TEST(PaddingLayoutChecks, ShrinksTheConstraintsOfferedToTheChild) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(10.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{400.0f, 400.0f};
    LayoutTestNode* child_ptr = child.get();
    padding.add_child(std::move(child));
    // act
    Size measured{};
    padding.measure(SizeConstraints::at_most(Size{100.0f, 100.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(child_ptr->measured_size().width, 80.0f);
    EXPECT_FLOAT_EQ(child_ptr->measured_size().height, 80.0f);
    EXPECT_FLOAT_EQ(measured.width, 100.0f);
    EXPECT_FLOAT_EQ(measured.height, 100.0f);
}

TEST(PaddingLayoutChecks, InsetsTheAssignedBoundsForTheChild) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(5.0f, 10.0f, 5.0f, 20.0f);
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    padding.add_child(std::move(child));
    // act
    padding.arrange(Rect::from_origin_size(Point{100.0f, 200.0f}, Size{200.0f, 100.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 120.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 205.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 170.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 90.0f);
}

TEST(PaddingLayoutChecks, CapsInsetsWiderThanTheWrapper) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(100.0f);
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    padding.add_child(std::move(child));
    // act
    padding.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 10.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 10.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 0.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 0.0f);
}

TEST(PaddingLayoutChecks, WrapperWithoutChildArrangesNothing) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(4.0f);
    // act
    padding.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    // assert
    EXPECT_EQ(padding.bounds(), (Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f})));
    EXPECT_EQ(padding.child_count(), 0u);
}

TEST(PaddingLayoutChecks, MeasureDoesNotArrange) {
    // arrange
    PaddingLayout padding;
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    padding.add_child(std::move(child));
    Size measured{};
    // act
    padding.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_EQ(child_ptr->measures, 1u);
    EXPECT_EQ(child_ptr->arranges, 0u);
}

TEST(PaddingLayoutChecks, PaddingChangeInvalidatesLayout) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(1.0f);
    padding.clear_dirty();
    // act
    padding.set_padding(2.0f);
    // assert
    EXPECT_TRUE(padding.layout_dirty());
    EXPECT_EQ(padding.padding(), (EdgeInsets{.top = 2.0f, .right = 2.0f, .bottom = 2.0f, .left = 2.0f}));
}

TEST(PaddingLayoutChecks, UnchangedPaddingDoesNotInvalidate) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(1.0f, 2.0f, 3.0f, 4.0f);
    padding.clear_dirty();
    // act
    padding.set_padding(1.0f, 2.0f, 3.0f, 4.0f);
    // assert
    EXPECT_FALSE(padding.layout_dirty());
}

TEST(PaddingLayoutChecks, NestingComposesTheInsets) {
    // arrange
    PaddingLayout outer;
    outer.set_padding(5.0f);
    auto inner = std::make_unique<PaddingLayout>();
    inner->set_padding(5.0f);
    auto leaf = std::make_unique<LayoutTestNode>();
    leaf->intrinsic = Size{10.0f, 10.0f};
    inner->add_child(std::move(leaf));
    PaddingLayout* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    Size measured{};
    // act
    outer.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 30.0f);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{30.0f, 30.0f}));
    EXPECT_FLOAT_EQ(inner_ptr->bounds().x, 5.0f);
    EXPECT_FLOAT_EQ(inner_ptr->bounds().width, 20.0f);
}

TEST(GridLayoutChecks, AGridWithNoCellsMeasuresAsNothing) {
    // arrange
    GridLayout grid;
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    // assert an empty grid has no cell to place a child in (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(GridLayoutChecks, DefaultsToASingleCell) {
    // arrange
    GridLayout grid;
    // act / assert the default divides into one cell, which places a child at the whole of the bounds (§8.2)
    EXPECT_EQ(grid.columns(), 1u);
    EXPECT_EQ(grid.rows(), 1u);
}

TEST(GridLayoutChecks, AGridWithNoChildMeasuresAsNothing) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    // assert no child means no cell size to report (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(GridLayoutChecks, DividesTheCellsEqually) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 10.0f};
    second->intrinsic = Size{20.0f, 30.0f};
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    // assert the largest child sets the cell, so every cell is that size (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 60.0f);
}

TEST(GridLayoutChecks, AddsTheGapsBetweenCellsOnly) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.set_gap(5.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{10.0f, 10.0f};
    grid.add_child(std::move(child));
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    // assert two columns carry one gap and two rows carry one gap, so the size gains two of them (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 25.0f);
    EXPECT_FLOAT_EQ(measured.height, 25.0f);
}

TEST(GridLayoutChecks, HonoursTheMaximumConstraint) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{100.0f, 100.0f};
    grid.add_child(std::move(child));
    Size measured{};
    // act
    grid.measure(SizeConstraints::at_most(Size{40.0f, 40.0f}), measured);
    // assert
    EXPECT_LE(measured.width, 40.0f);
    EXPECT_LE(measured.height, 40.0f);
}

TEST(GridLayoutChecks, PlacesChildrenAcrossTheCells) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    first->intrinsic = Size{10.0f, 10.0f};
    second->intrinsic = Size{10.0f, 10.0f};
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    Size measured{};
    grid.measure(SizeConstraints::unconstrained(), measured);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // assert the first child takes the top left cell and the second the one beside it (§8.2)
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().width, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 0.0f);
}

TEST(GridLayoutChecks, WrapsToTheNextRowWhenACellFills) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* second_ptr = second.get();
    first->intrinsic = Size{10.0f, 10.0f};
    second->intrinsic = Size{10.0f, 10.0f};
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // assert the third child of a two column grid lands in the second row, which is what makes a grid fill (§8.2)
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 0.0f);
}

TEST(GridLayoutChecks, TheGapsMoveTheCells) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 1);
    grid.set_gap(8.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* second_ptr = second.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{48.0f, 20.0f}));
    // assert the gap sits between the two cells, so the second one starts after it (§8.2)
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 28.0f);
}

TEST(GridLayoutChecks, ASpanCoversSeveralColumns) {
    // arrange
    GridLayout grid;
    grid.set_columns(3, 1);
    auto wide = std::make_unique<LayoutTestNode>();
    auto next = std::make_unique<LayoutTestNode>();
    LayoutTestNode* wide_ptr = wide.get();
    LayoutTestNode* next_ptr = next.get();
    grid.add_child(std::move(wide));
    grid.add_child(std::move(next));
    ASSERT_TRUE(grid.set_span(0, GridSpan{.columns = 2, .rows = 1}));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    // assert the wide child takes two cells and the next one starts in the third (§8.2)
    EXPECT_FLOAT_EQ(wide_ptr->bounds().width, 40.0f);
    EXPECT_FLOAT_EQ(next_ptr->bounds().x, 40.0f);
    EXPECT_FLOAT_EQ(next_ptr->bounds().width, 20.0f);
}

TEST(GridLayoutChecks, ASpanCoversSeveralRows) {
    // arrange
    GridLayout grid;
    grid.set_columns(1, 3);
    auto tall = std::make_unique<LayoutTestNode>();
    auto next = std::make_unique<LayoutTestNode>();
    LayoutTestNode* tall_ptr = tall.get();
    LayoutTestNode* next_ptr = next.get();
    grid.add_child(std::move(tall));
    grid.add_child(std::move(next));
    ASSERT_TRUE(grid.set_span(0, GridSpan{.columns = 1, .rows = 2}));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 60.0f}));
    // assert the tall child takes two rows and the next one lands in the third (§8.2)
    EXPECT_FLOAT_EQ(tall_ptr->bounds().height, 40.0f);
    EXPECT_FLOAT_EQ(next_ptr->bounds().y, 40.0f);
    EXPECT_FLOAT_EQ(next_ptr->bounds().height, 20.0f);
}

TEST(GridLayoutChecks, ASpanCoversEveryCell) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    grid.add_child(std::move(child));
    ASSERT_TRUE(grid.set_span(0, GridSpan{.columns = 2, .rows = 2}));
    // act
    grid.arrange(Rect::from_origin_size(Point{5.0f, 6.0f}, Size{40.0f, 40.0f}));
    // assert a child spanning every cell fills the grid, which is what a spanning child means (§8.2)
    EXPECT_EQ(child_ptr->bounds(), (Rect::from_origin_size(Point{5.0f, 6.0f}, Size{40.0f, 40.0f})));
}

TEST(GridLayoutChecks, ASpanLargerThanTheGridIsCapped) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 1);
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    grid.add_child(std::move(child));
    // the span is set while the grid still has one column, so arrange has to cap what it cannot honour (§8.2)
    grid.set_span(0, GridSpan{.columns = 2, .rows = 1});
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 20.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 40.0f);
}

TEST(GridLayoutChecks, AChildWithNoSpanIsASingleCell) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    grid.add_child(std::move(child));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // assert a plain grid gives every child one cell (§8.2)
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 20.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 20.0f);
}

TEST(GridLayoutChecks, SpanDefaultsToASingleCell) {
    // arrange / act / assert
    const GridSpan span{};
    EXPECT_TRUE(span.single_cell());
    EXPECT_EQ(span.columns, 1u);
    EXPECT_EQ(span.rows, 1u);
}

TEST(GridLayoutChecks, SpansCompareFieldByField) {
    // arrange
    const GridSpan lhs{.columns = 2, .rows = 1};
    const GridSpan rhs{.columns = 2, .rows = 1};
    const GridSpan other{.columns = 1, .rows = 2};
    // act / assert
    EXPECT_EQ(lhs, rhs);
    EXPECT_NE(lhs, other);
    EXPECT_FALSE(lhs.single_cell());
    EXPECT_FALSE(other.single_cell());
}

TEST(GridLayoutChecks, SpanAtAnUnknownIndexIsASingleCell) {
    // arrange
    GridLayout grid;
    // act
    const GridSpan span = grid.span_at(4);
    // assert an index past the end names no child, so the default answers (§8.2)
    EXPECT_TRUE(span.single_cell());
}

TEST(GridLayoutChecks, SpanRecordsWhatWasSet) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.add_child(std::make_unique<LayoutTestNode>());
    // act
    const bool set = grid.set_span(0, GridSpan{.columns = 2, .rows = 1});
    // assert
    EXPECT_TRUE(set);
    EXPECT_EQ(grid.span_at(0), (GridSpan{.columns = 2, .rows = 1}));
}

TEST(GridLayoutChecks, SpanAtAPastTheEndIndexIsRefused) {
    // arrange
    GridLayout grid;
    // act
    const bool set = grid.set_span(3, GridSpan{.columns = 1, .rows = 1});
    // assert there is no child to span, so the request is refused rather than stored (§8.2)
    EXPECT_FALSE(set);
}

TEST(GridLayoutChecks, AZeroSpanIsRefused) {
    // arrange
    GridLayout grid;
    grid.add_child(std::make_unique<LayoutTestNode>());
    // act
    const bool no_columns = grid.set_span(0, GridSpan{.columns = 0, .rows = 1});
    const bool no_rows = grid.set_span(0, GridSpan{.columns = 1, .rows = 0});
    // assert a span of no cells would place nothing, so both forms are refused (§8.2)
    EXPECT_FALSE(no_columns);
    EXPECT_FALSE(no_rows);
}

TEST(GridLayoutChecks, ASpanWiderThanTheGridIsRefused) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.add_child(std::make_unique<LayoutTestNode>());
    // act
    const bool too_wide = grid.set_span(0, GridSpan{.columns = 3, .rows = 1});
    const bool too_tall = grid.set_span(0, GridSpan{.columns = 1, .rows = 3});
    // assert a span the grid cannot honour is refused rather than silently clamped (§8.2)
    EXPECT_FALSE(too_wide);
    EXPECT_FALSE(too_tall);
}

TEST(GridLayoutChecks, SpanChangeInvalidatesLayout) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.add_child(std::make_unique<LayoutTestNode>());
    grid.clear_dirty();
    // act
    grid.set_span(0, GridSpan{.columns = 2, .rows = 1});
    // assert a span moves the children that follow it (§8.2)
    EXPECT_TRUE(grid.layout_dirty());
}

TEST(GridLayoutChecks, ColumnChangeInvalidatesLayout) {
    // arrange
    GridLayout grid;
    grid.clear_dirty();
    // act
    grid.set_columns(3, 2);
    // assert
    EXPECT_TRUE(grid.layout_dirty());
    EXPECT_EQ(grid.columns(), 3u);
    EXPECT_EQ(grid.rows(), 2u);
}

TEST(GridLayoutChecks, AZeroColumnCountIsRefused) {
    // arrange
    GridLayout grid;
    // act
    grid.set_columns(0, 2);
    grid.set_columns(2, 0);
    // assert a grid with no cells cannot place a child (§8.2)
    EXPECT_EQ(grid.columns(), 1u);
    EXPECT_EQ(grid.rows(), 1u);
}

TEST(GridLayoutChecks, AnUnchangedColumnCountDoesNotInvalidate) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.clear_dirty();
    // act
    grid.set_columns(2, 2);
    // assert
    EXPECT_FALSE(grid.layout_dirty());
}

TEST(GridLayoutChecks, GapChangeInvalidatesLayout) {
    // arrange
    GridLayout grid;
    grid.clear_dirty();
    // act
    grid.set_gap(4.0f);
    // assert
    EXPECT_TRUE(grid.layout_dirty());
    EXPECT_FLOAT_EQ(grid.gap(), 4.0f);
}

TEST(GridLayoutChecks, ANegativeGapIsRefused) {
    // arrange
    GridLayout grid;
    // act
    grid.set_gap(-2.0f);
    // assert a negative gap would overlap the cells, which is not what a gap means (§8.2)
    EXPECT_FLOAT_EQ(grid.gap(), 0.0f);
}

TEST(GridLayoutChecks, AnUnchangedGapDoesNotInvalidate) {
    // arrange
    GridLayout grid;
    grid.set_gap(4.0f);
    grid.clear_dirty();
    // act
    grid.set_gap(4.0f);
    // assert
    EXPECT_FALSE(grid.layout_dirty());
}

TEST(GridLayoutChecks, MeasureDoesNotArrange) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 1);
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    grid.add_child(std::move(child));
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_EQ(child_ptr->measures, 1u);
    EXPECT_EQ(child_ptr->arranges, 0u);
}

TEST(ColumnLayoutChecks, AddsThePaddingOnBothAxes) {
    // arrange
    ColumnLayout column;
    column.set_padding(8.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{10.0f, 20.0f};
    column.add_child(std::move(child));
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 26.0f);
    EXPECT_FLOAT_EQ(measured.height, 36.0f);
}

TEST(ColumnLayoutChecks, ArrangesTheChildrenInsideThePadding) {
    // arrange
    ColumnLayout column;
    column.set_gap(2.0f);
    column.set_padding(6.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 30.0f};
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    Size measured{};
    column.measure(SizeConstraints::unconstrained(), measured);
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 200.0f});
    // act
    column.arrange(bounds);
    // assert
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 6.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().y, 6.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().width, 68.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 28.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().width, 68.0f);
}

TEST(ColumnLayoutChecks, PaddingChangeInvalidatesLayout) {
    // arrange
    ColumnLayout column;
    column.clear_dirty();
    // act
    column.set_padding(5.0f);
    // assert
    EXPECT_TRUE(column.layout_dirty());
    EXPECT_FLOAT_EQ(column.padding(), 5.0f);
}

TEST(ColumnLayoutChecks, UnchangedPaddingDoesNotInvalidate) {
    // arrange
    ColumnLayout column;
    column.set_padding(5.0f);
    column.clear_dirty();
    // act
    column.set_padding(5.0f);
    // assert
    EXPECT_FALSE(column.layout_dirty());
}
