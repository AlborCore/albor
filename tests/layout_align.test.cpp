#include <memory>

#include <gtest/gtest.h>

#include "layout/column_layout.h"
#include "layout/grid_layout.h"
#include "layout/row_layout.h"

using namespace albor;

namespace
{
    // the node test double, which reports a fixed extent and carries the alignment the test set (§8.2)
    class AlignTestNode final : public UiNode
    {
    public:
        // the extent this node reports when it measures itself (§8.2)
        Size intrinsic{10.0f, 20.0f};

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the intrinsic extent is clamped into the offered constraints, which is what a node does (§8.2)
            out_size = constraints.constrain(intrinsic);
            m_measured_size = out_size;
        }
    };

    // build one child of a known extent, which a test then reads the arranged bounds of (§8.2)
    AlignTestNode* add_child_of(UiNode& parent, Size intrinsic) {
        // the child is built apart, because the parent takes ownership of it (§8.1)
        auto child = std::make_unique<AlignTestNode>();
        // the extent is set before the parent adopts it, so the first measure already reports it (§8.2)
        child->intrinsic = intrinsic;
        // the raw pointer is what a test asserts the arranged bounds through (§8.2)
        AlignTestNode* raw = child.get();
        parent.add_child(std::move(child));
        return raw;
    }

    // measure a container and arrange it into the given bounds, which is what a layout pass does (§8.2)
    void measure_and_arrange(UiNode& node, Rect bounds) {
        // the measure phase runs first, because the arrange pass reads the measured extents (§8.2)
        Size measured{};
        node.measure(SizeConstraints::unconstrained(), measured);
        // the arrange pass then places the children inside the assigned bounds (§8.2)
        node.arrange(bounds);
    }
} // namespace

TEST(RowAlignmentChecks, AnUnsetAlignmentKeepsTheLeadingEdge) {
    // arrange
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 60.0f}));
    // assert
    EXPECT_FLOAT_EQ(child->bounds().x, 0.0f);
}

TEST(RowAlignmentChecks, AnUnsetAlignmentKeepsTheFullCrossExtent) {
    // arrange
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 60.0f}));
    // assert the cross axis still stretches, which is what the previous layout did (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().height, 60.0f);
}

TEST(RowAlignmentChecks, ACentredChildSitsHalfTheLeftoverIn) {
    // arrange
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    child->set_horizontal_align(Align::Center);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 60.0f}));
    // assert the leftover is ninety and the child takes half of it (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().x, 45.0f);
}

TEST(RowAlignmentChecks, AnEndAlignedChildSitsAtTheTrailingEdge) {
    // arrange
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    child->set_horizontal_align(Align::End);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 60.0f}));
    // assert the child ends where the row ends (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().x, 90.0f);
}

TEST(RowAlignmentChecks, AStretchingChildTakesTheWholeLeftover) {
    // arrange
    RowLayout row;
    AlignTestNode* lead = add_child_of(row, Size{10.0f, 20.0f});
    AlignTestNode* filler = add_child_of(row, Size{10.0f, 20.0f});
    AlignTestNode* trail = add_child_of(row, Size{10.0f, 20.0f});
    filler->set_horizontal_align(Align::Stretch);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 60.0f}));
    // assert the filler absorbed the space the other two did not ask for (§8.2)
    EXPECT_FLOAT_EQ(lead->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(filler->bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(filler->bounds().width, 80.0f);
    // assert the spacer pushed what follows it to the far end, which is the toolbar idiom (§8.2)
    EXPECT_FLOAT_EQ(trail->bounds().x, 90.0f);
}

TEST(RowAlignmentChecks, TwoStretchingChildrenSplitTheLeftover) {
    // arrange
    RowLayout row;
    AlignTestNode* first = add_child_of(row, Size{10.0f, 20.0f});
    AlignTestNode* second = add_child_of(row, Size{10.0f, 20.0f});
    first->set_horizontal_align(Align::Stretch);
    second->set_horizontal_align(Align::Stretch);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 60.0f}));
    // assert each took an equal share of the eighty pixels neither asked for (§8.2)
    EXPECT_FLOAT_EQ(first->bounds().width, 50.0f);
    EXPECT_FLOAT_EQ(second->bounds().width, 50.0f);
}

TEST(RowAlignmentChecks, ACrossAlignedChildKeepsItsMeasuredHeight) {
    // arrange
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    child->set_vertical_align(Align::Center);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 60.0f}));
    // assert the child is centred in the row height rather than filling it (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(child->bounds().height, 20.0f);
}

TEST(RowAlignmentChecks, AVerticallyEndAlignedChildSitsAtTheBottom) {
    // arrange
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    child->set_vertical_align(Align::End);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 60.0f}));
    // assert the child rests on the row's bottom edge (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().y, 40.0f);
}

TEST(RowAlignmentChecks, ANegativeLeftoverKeepsTheMeasuredExtent) {
    // arrange a row narrower than its content, which is the overflow case (§8.2)
    RowLayout row;
    AlignTestNode* child = add_child_of(row, Size{80.0f, 20.0f});
    child->set_horizontal_align(Align::Stretch);
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 60.0f}));
    // assert the child keeps what it asked for rather than shrinking, because overflow is not absorbed (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().width, 80.0f);
}

TEST(RowAlignmentChecks, PaddingInsetsTheChildrenOnEverySide) {
    // arrange
    RowLayout row;
    row.set_padding(10.0f);
    AlignTestNode* child = add_child_of(row, Size{10.0f, 20.0f});
    // act
    measure_and_arrange(row, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 60.0f}));
    // assert the children start inside the inset on both axes (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(child->bounds().y, 10.0f);
}

TEST(RowAlignmentChecks, PaddingIsMeasuredAsPartOfTheRow) {
    // arrange
    RowLayout row;
    row.set_padding(10.0f);
    add_child_of(row, Size{10.0f, 20.0f});
    // act
    Size measured{};
    row.measure(SizeConstraints::unconstrained(), measured);
    // assert the inset sits outside the content on both axes (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(ColumnAlignmentChecks, AnUnsetAlignmentKeepsTheTopEdgeAndTheFullWidth) {
    // arrange
    ColumnLayout column;
    AlignTestNode* child = add_child_of(column, Size{10.0f, 20.0f});
    // act
    measure_and_arrange(column, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 200.0f}));
    // assert the main axis starts at the top and the cross axis still fills (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(child->bounds().width, 100.0f);
}

TEST(ColumnAlignmentChecks, AVerticallyCentredChildSitsHalfTheLeftoverIn) {
    // arrange
    ColumnLayout column;
    AlignTestNode* child = add_child_of(column, Size{10.0f, 20.0f});
    child->set_vertical_align(Align::Center);
    // act
    measure_and_arrange(column, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 200.0f}));
    // assert the leftover is one hundred and eighty and the child takes half of it (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().y, 90.0f);
}

TEST(ColumnAlignmentChecks, AVerticallyEndAlignedChildSitsAtTheBottom) {
    // arrange
    ColumnLayout column;
    AlignTestNode* child = add_child_of(column, Size{10.0f, 20.0f});
    child->set_vertical_align(Align::End);
    // act
    measure_and_arrange(column, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 200.0f}));
    // assert the child rests on the column's bottom edge (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().y, 180.0f);
}

TEST(ColumnAlignmentChecks, AHorizontallyCentredChildSitsHalfTheLeftoverIn) {
    // arrange
    ColumnLayout column;
    AlignTestNode* child = add_child_of(column, Size{40.0f, 20.0f});
    child->set_horizontal_align(Align::Center);
    // act
    measure_and_arrange(column, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 200.0f}));
    // assert the child is centred across the column rather than filling it (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().x, 30.0f);
    EXPECT_FLOAT_EQ(child->bounds().width, 40.0f);
}

TEST(ColumnAlignmentChecks, AHorizontallyEndAlignedChildSitsAtTheRight) {
    // arrange
    ColumnLayout column;
    AlignTestNode* child = add_child_of(column, Size{40.0f, 20.0f});
    child->set_horizontal_align(Align::End);
    // act
    measure_and_arrange(column, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 200.0f}));
    // assert the child ends where the column ends (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().x, 60.0f);
}

TEST(ColumnAlignmentChecks, AVerticallyStretchingChildTakesTheWholeHeight) {
    // arrange
    ColumnLayout column;
    AlignTestNode* child = add_child_of(column, Size{40.0f, 20.0f});
    child->set_vertical_align(Align::Stretch);
    // act
    measure_and_arrange(column, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 200.0f}));
    // assert the child fills the column, which is what a stretch on the main axis means (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().height, 200.0f);
}

TEST(GridAlignmentChecks, ACentredChildSitsInTheMiddleOfItsCell) {
    // arrange
    GridLayout grid;
    grid.set_columns(1, 1);
    AlignTestNode* child = add_child_of(grid, Size{20.0f, 10.0f});
    child->set_horizontal_align(Align::Center);
    child->set_vertical_align(Align::Center);
    // act
    measure_and_arrange(grid, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // assert the child is centred inside the one hundred by one hundred cell (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().x, 40.0f);
    EXPECT_FLOAT_EQ(child->bounds().y, 45.0f);
}

TEST(GridAlignmentChecks, AnUnalignedChildStillFillsItsCell) {
    // arrange
    GridLayout grid;
    grid.set_columns(1, 1);
    AlignTestNode* child = add_child_of(grid, Size{20.0f, 10.0f});
    // act
    measure_and_arrange(grid, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // assert the default still stretches, which is what the grid did before alignment existed (§8.2)
    EXPECT_FLOAT_EQ(child->bounds().width, 100.0f);
    EXPECT_FLOAT_EQ(child->bounds().height, 100.0f);
}

TEST(AlignmentSettersChecks, AnUnchangedAlignmentInvalidatesNothing) {
    // arrange
    AlignTestNode node;
    node.clear_dirty();
    // act
    node.set_horizontal_align(Align::Center);
    node.clear_dirty();
    node.set_horizontal_align(Align::Center);
    // assert an unchanged value relayouts nothing, which is what the dirty flag is for (§8.2)
    EXPECT_FALSE(node.layout_dirty());
}

TEST(AlignmentSettersChecks, AChangedAlignmentInvalidatesTheLayout) {
    // arrange
    AlignTestNode node;
    node.clear_dirty();
    // act
    node.set_horizontal_align(Align::End);
    // assert the parent arranges from this, so a measure pass is requested (§8.2)
    EXPECT_TRUE(node.layout_dirty());
}

TEST(AlignmentResolveChecks, AnAuthoredAlignmentOverridesTheDefault) {
    // assert
    EXPECT_EQ(resolve_align(Align::Center, Align::Start), Align::Center);
}

TEST(AlignmentResolveChecks, AnUnsetAlignmentTakesTheDefault) {
    // assert
    EXPECT_EQ(resolve_align(Align::Inherit, Align::Stretch), Align::Stretch);
}
