#include <memory>

#include <gtest/gtest.h>

#include "albor/layout/column_layout.h"
#include "albor/layout/grid_layout.h"
#include "albor/layout/padding_layout.h"
#include "albor/layout/row_layout.h"
#include "albor/layout/sized_box_layout.h"
#include "albor/layout/stack_layout.h"

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

TEST(StackLayoutChecks, AHiddenChildLeavesTheStack) {
    // arrange
    StackLayout stack;
    auto visible = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    visible->intrinsic = Size{30.0f, 10.0f};
    hidden->intrinsic = Size{100.0f, 100.0f};
    LayoutTestNode* hidden_ptr = hidden.get();
    stack.add_child(std::move(visible));
    stack.add_child(std::move(hidden));
    hidden_ptr->set_visible(false);
    Size measured{};
    const Rect bounds = Rect::from_origin_size(Point{2.0f, 3.0f}, Size{50.0f, 50.0f});
    // act
    stack.measure(SizeConstraints::unconstrained(), measured);
    stack.arrange(bounds);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
    EXPECT_EQ(hidden_ptr->measures, 0u);
    EXPECT_EQ(hidden_ptr->arranges, 0u);
}

TEST(StackLayoutChecks, AStackWithEveryChildHiddenMeasuresAsNothing) {
    // arrange
    StackLayout stack;
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    stack.add_child(std::move(child));
    child_ptr->set_visible(false);
    Size measured{};
    // act
    stack.measure(SizeConstraints::unconstrained(), measured);
    stack.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 50.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
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

TEST(RowLayoutChecks, AChildIsOfferedTheSpaceItsSiblingsLeft) {
    // arrange a fixed width sibling before a child that declares the main axis stretch (§8.2)
    RowLayout row;
    row.set_gap(10.0f);
    auto sidebar = std::make_unique<LayoutTestNode>();
    auto page = std::make_unique<LayoutTestNode>();
    sidebar->intrinsic = Size{120.0f, 40.0f};
    page->intrinsic = Size{500.0f, 40.0f};
    page->set_horizontal_align(Align::Stretch);
    LayoutTestNode* page_ptr = page.get();
    row.add_child(std::move(sidebar));
    row.add_child(std::move(page));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 60.0f}), measured);
    // assert the page is offered only what the sidebar and the gap left (§8.2)
    EXPECT_FLOAT_EQ(page_ptr->offered.maximum.width, 370.0f);
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
}

TEST(RowLayoutChecks, PaddingIsKeptOutOfWhatTheChildrenAreOffered) {
    // arrange a padded row whose one child must fit inside the inset (§8.2)
    RowLayout row;
    row.set_padding(10.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{500.0f, 40.0f};
    LayoutTestNode* child_ptr = child.get();
    row.add_child(std::move(child));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{200.0f, 80.0f}), measured);
    // assert the child never measures into the padding, which would overflow the row (§8.2)
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.width, 180.0f);
    EXPECT_FLOAT_EQ(measured.width, 200.0f);
}

TEST(RowLayoutChecks, AFlexibleChildDoesNotStarveItsLaterSibling) {
    // arrange a scroll view before a button, which the old sequential offer collapsed to zero (§8.2)
    RowLayout row;
    auto page = std::make_unique<LayoutTestNode>();
    auto action = std::make_unique<LayoutTestNode>();
    page->intrinsic = Size{500.0f, 40.0f};
    page->set_horizontal_align(Align::Stretch);
    action->intrinsic = Size{80.0f, 40.0f};
    LayoutTestNode* action_ptr = action.get();
    row.add_child(std::move(page));
    row.add_child(std::move(action));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 60.0f}), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 60.0f}));
    // assert the action keeps its measured width and sits inside the row (§8.2)
    EXPECT_FLOAT_EQ(action_ptr->measured_size().width, 80.0f);
    EXPECT_FLOAT_EQ(action_ptr->bounds().width, 80.0f);
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
}

TEST(RowLayoutChecks, PaddingAndGapsAreKeptOutOfTheFlexibleShare) {
    // arrange a padded and gapped row whose flexible child must take only the space left (§8.2)
    RowLayout row;
    row.set_padding(10.0f);
    row.set_gap(10.0f);
    auto sidebar = std::make_unique<LayoutTestNode>();
    auto page = std::make_unique<LayoutTestNode>();
    sidebar->intrinsic = Size{120.0f, 40.0f};
    page->intrinsic = Size{500.0f, 40.0f};
    page->set_horizontal_align(Align::Stretch);
    LayoutTestNode* page_ptr = page.get();
    row.add_child(std::move(sidebar));
    row.add_child(std::move(page));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 80.0f}), measured);
    // assert the flexible child is offered the content less the fixed child and the gap (§8.2)
    EXPECT_FLOAT_EQ(page_ptr->measured_size().width, 350.0f);
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
}

TEST(RowLayoutChecks, TwoFlexibleChildrenSplitTheContent) {
    // arrange two children that declare stretch, which the overflow must divide between them (§8.2)
    RowLayout row;
    row.set_gap(10.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{500.0f, 40.0f};
    second->intrinsic = Size{500.0f, 40.0f};
    first->set_horizontal_align(Align::Stretch);
    second->set_horizontal_align(Align::Stretch);
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    row.add_child(std::move(first));
    row.add_child(std::move(second));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 60.0f}), measured);
    // assert each child takes half the content less the gap, and the row fills the offer (§8.2)
    EXPECT_FLOAT_EQ(first_ptr->measured_size().width, 245.0f);
    EXPECT_FLOAT_EQ(second_ptr->measured_size().width, 245.0f);
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
}

TEST(RowLayoutChecks, AFullWidthChildWithoutStretchKeepsItsExtent) {
    // arrange a full width child that never declared stretch, which the row must not shrink (§8.2)
    RowLayout row;
    auto page = std::make_unique<LayoutTestNode>();
    auto action = std::make_unique<LayoutTestNode>();
    page->intrinsic = Size{500.0f, 40.0f};
    action->intrinsic = Size{80.0f, 40.0f};
    LayoutTestNode* page_ptr = page.get();
    LayoutTestNode* action_ptr = action.get();
    row.add_child(std::move(page));
    row.add_child(std::move(action));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 60.0f}), measured);
    // assert the authored extents stand, because only a declared stretch gives space up (§8.2)
    EXPECT_FLOAT_EQ(page_ptr->measured_size().width, 500.0f);
    EXPECT_FLOAT_EQ(action_ptr->measured_size().width, 80.0f);
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
}

TEST(RowLayoutChecks, AChildIsOfferedTheContentHeight) {
    // arrange a padded row whose cross axis offer must exclude the inset (§8.2)
    RowLayout row;
    row.set_padding(10.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{10.0f, 80.0f};
    LayoutTestNode* child_ptr = child.get();
    row.add_child(std::move(child));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{200.0f, 100.0f}), measured);
    // assert the child is offered only the height inside the padding (§8.2)
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.height, 80.0f);
    EXPECT_FLOAT_EQ(measured.height, 100.0f);
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

TEST(RowLayoutChecks, AHiddenChildTakesNoSlotOrGap) {
    // arrange
    RowLayout row;
    row.set_gap(5.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 20.0f};
    third->intrinsic = Size{10.0f, 20.0f};
    LayoutTestNode* second_ptr = second.get();
    LayoutTestNode* third_ptr = third.get();
    row.add_child(std::move(first));
    row.add_child(std::move(second));
    row.add_child(std::move(third));
    second_ptr->set_visible(false);
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 25.0f);
    EXPECT_EQ(second_ptr->measures, 0u);
    EXPECT_EQ(second_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(third_ptr->bounds().x, 15.0f);
}

TEST(RowLayoutChecks, AHiddenFirstChildStartsAtTheLeadingEdge) {
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
    first_ptr->set_visible(false);
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 0.0f);
    EXPECT_EQ(first_ptr->measures, 0u);
    EXPECT_EQ(first_ptr->arranges, 0u);
}

TEST(RowLayoutChecks, AHiddenStretchChildTakesNoShare) {
    // arrange
    RowLayout row;
    auto first = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    auto last = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 10.0f};
    hidden->intrinsic = Size{10.0f, 10.0f};
    last->intrinsic = Size{10.0f, 10.0f};
    last->set_horizontal_align(Align::Stretch);
    LayoutTestNode* hidden_ptr = hidden.get();
    LayoutTestNode* last_ptr = last.get();
    row.add_child(std::move(first));
    row.add_child(std::move(hidden));
    row.add_child(std::move(last));
    hidden_ptr->set_horizontal_align(Align::Stretch);
    hidden_ptr->set_visible(false);
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 10.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 20.0f);
    EXPECT_EQ(hidden_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(last_ptr->bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(last_ptr->bounds().width, 30.0f);
}

TEST(RowLayoutChecks, ARowWithEveryChildHiddenMeasuresAsNothing) {
    // arrange
    RowLayout row;
    row.set_gap(5.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    row.add_child(std::move(first));
    row.add_child(std::move(second));
    first_ptr->set_visible(false);
    second_ptr->set_visible(false);
    Size measured{};
    // act
    row.measure(SizeConstraints::unconstrained(), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
    EXPECT_EQ(first_ptr->measures, 0u);
    EXPECT_EQ(second_ptr->measures, 0u);
    EXPECT_EQ(first_ptr->arranges, 0u);
    EXPECT_EQ(second_ptr->arranges, 0u);
}

TEST(RowLayoutChecks, AShownChildReturnsToTheRow) {
    // arrange
    RowLayout row;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{10.0f, 20.0f};
    child->set_explicit_id("field");
    LayoutTestNode* child_ptr = child.get();
    row.add_child(std::move(child));
    child_ptr->set_visible(false);
    Size measured{};
    row.measure(SizeConstraints::unconstrained(), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 40.0f}));
    // act
    child_ptr->set_visible(true);
    row.measure(SizeConstraints::unconstrained(), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 40.0f}));
    // assert
    EXPECT_EQ(row.child_count(), 1u);
    EXPECT_EQ(child_ptr->identity().explicit_id, "field");
    EXPECT_TRUE(child_ptr->visible());
    EXPECT_EQ(child_ptr->measures, 1u);
    EXPECT_EQ(child_ptr->arranges, 1u);
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
}

TEST(RowLayoutChecks, HidingAChildMarksTheRowDirty) {
    // arrange
    RowLayout row;
    auto child = std::make_unique<LayoutTestNode>();
    LayoutTestNode* child_ptr = child.get();
    row.add_child(std::move(child));
    row.clear_dirty();
    // act
    child_ptr->set_visible(false);
    // assert
    EXPECT_TRUE(child_ptr->layout_dirty());
    EXPECT_TRUE(row.layout_dirty());
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

TEST(ColumnLayoutChecks, AChildIsOfferedTheSpaceItsSiblingsLeft) {
    // arrange a stacked heading above a child that declares the main axis stretch (§8.2)
    ColumnLayout column;
    column.set_gap(10.0f);
    auto heading = std::make_unique<LayoutTestNode>();
    auto page = std::make_unique<LayoutTestNode>();
    heading->intrinsic = Size{80.0f, 120.0f};
    page->intrinsic = Size{80.0f, 600.0f};
    page->set_vertical_align(Align::Stretch);
    LayoutTestNode* page_ptr = page.get();
    column.add_child(std::move(heading));
    column.add_child(std::move(page));
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{200.0f, 500.0f}), measured);
    // assert the page is offered only what the heading and the gap left (§8.2)
    EXPECT_FLOAT_EQ(page_ptr->offered.maximum.height, 370.0f);
    EXPECT_FLOAT_EQ(measured.height, 500.0f);
}

TEST(ColumnLayoutChecks, AFlexibleFirstChildDoesNotStarveItsLaterSibling) {
    // arrange a scroll view above an action row, which the old sequential offer collapsed to zero (§8.2)
    ColumnLayout column;
    auto page = std::make_unique<LayoutTestNode>();
    auto action = std::make_unique<LayoutTestNode>();
    page->intrinsic = Size{200.0f, 400.0f};
    page->set_vertical_align(Align::Stretch);
    action->intrinsic = Size{80.0f, 32.0f};
    LayoutTestNode* action_ptr = action.get();
    column.add_child(std::move(page));
    column.add_child(std::move(action));
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    column.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 400.0f}));
    // assert the action keeps its measured height and sits inside the column (§8.2)
    EXPECT_FLOAT_EQ(action_ptr->measured_size().height, 32.0f);
    EXPECT_FLOAT_EQ(action_ptr->bounds().height, 32.0f);
    EXPECT_FLOAT_EQ(measured.height, 400.0f);
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

TEST(ColumnLayoutChecks, AHiddenChildTakesNoSlotOrGap) {
    // arrange
    ColumnLayout column;
    column.set_gap(8.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 20.0f};
    second->intrinsic = Size{10.0f, 20.0f};
    third->intrinsic = Size{10.0f, 20.0f};
    LayoutTestNode* second_ptr = second.get();
    LayoutTestNode* third_ptr = third.get();
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    column.add_child(std::move(third));
    second_ptr->set_visible(false);
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    column.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 200.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.height, 48.0f);
    EXPECT_EQ(second_ptr->measures, 0u);
    EXPECT_EQ(second_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(third_ptr->bounds().y, 28.0f);
}

TEST(ColumnLayoutChecks, AHiddenStretchChildTakesNoShare) {
    // arrange
    ColumnLayout column;
    auto first = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    auto last = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 10.0f};
    hidden->intrinsic = Size{10.0f, 10.0f};
    last->intrinsic = Size{10.0f, 10.0f};
    last->set_vertical_align(Align::Stretch);
    LayoutTestNode* hidden_ptr = hidden.get();
    LayoutTestNode* last_ptr = last.get();
    column.add_child(std::move(first));
    column.add_child(std::move(hidden));
    column.add_child(std::move(last));
    hidden_ptr->set_vertical_align(Align::Stretch);
    hidden_ptr->set_visible(false);
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    column.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 60.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
    EXPECT_EQ(hidden_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(last_ptr->bounds().y, 10.0f);
    EXPECT_FLOAT_EQ(last_ptr->bounds().height, 50.0f);
}

TEST(ColumnLayoutChecks, AColumnWithEveryChildHiddenMeasuresAsNothing) {
    // arrange
    ColumnLayout column;
    column.set_gap(8.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    column.add_child(std::move(first));
    column.add_child(std::move(second));
    first_ptr->set_visible(false);
    second_ptr->set_visible(false);
    Size measured{};
    // act
    column.measure(SizeConstraints::unconstrained(), measured);
    column.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 200.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
    EXPECT_EQ(first_ptr->measures, 0u);
    EXPECT_EQ(second_ptr->measures, 0u);
    EXPECT_EQ(first_ptr->arranges, 0u);
    EXPECT_EQ(second_ptr->arranges, 0u);
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
    const Insets insets{};
    // act assert
    EXPECT_TRUE(insets.empty());
}

TEST(PaddingLayoutChecks, InsetsCompareFieldByField) {
    // arrange
    const Insets lhs{.top = 1.0f, .right = 2.0f, .bottom = 3.0f, .left = 4.0f};
    const Insets rhs{.top = 1.0f, .right = 2.0f, .bottom = 3.0f, .left = 4.0f};
    const Insets other{.top = 1.0f, .right = 2.0f, .bottom = 3.0f, .left = 5.0f};
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

TEST(PaddingLayoutChecks, AHiddenChildLeavesThePadding) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(5.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{100.0f, 100.0f};
    LayoutTestNode* child_ptr = child.get();
    padding.add_child(std::move(child));
    child_ptr->set_visible(false);
    Size measured{};
    // act
    padding.measure(SizeConstraints::unconstrained(), measured);
    padding.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
    EXPECT_EQ(child_ptr->measures, 0u);
    EXPECT_EQ(child_ptr->arranges, 0u);
}

TEST(PaddingLayoutChecks, AShownChildReturnsToThePadding) {
    // arrange
    PaddingLayout padding;
    padding.set_padding(5.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{100.0f, 100.0f};
    LayoutTestNode* child_ptr = child.get();
    padding.add_child(std::move(child));
    child_ptr->set_visible(false);
    Size measured{};
    padding.measure(SizeConstraints::unconstrained(), measured);
    // act
    child_ptr->set_visible(true);
    padding.measure(SizeConstraints::unconstrained(), measured);
    padding.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 110.0f);
    EXPECT_FLOAT_EQ(measured.height, 110.0f);
    EXPECT_EQ(child_ptr->measures, 1u);
    EXPECT_EQ(child_ptr->arranges, 1u);
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 5.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 5.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 10.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 10.0f);
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
    EXPECT_EQ(padding.padding(), (Insets{.top = 2.0f, .right = 2.0f, .bottom = 2.0f, .left = 2.0f}));
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

TEST(PaddingLayoutChecks, InsetsOverloadSetsPadding) {
    // arrange
    PaddingLayout padding;
    padding.clear_dirty();
    const Insets insets{.top = 10.0f, .right = 20.0f, .bottom = 30.0f, .left = 40.0f};
    // act
    padding.set_padding(insets);
    // assert
    EXPECT_TRUE(padding.layout_dirty());
    EXPECT_EQ(padding.padding(), insets);
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

TEST(GridLayoutChecks, AnEmptyGridIsASingleCell) {
    // arrange
    GridLayout grid;
    // act / assert an empty grid divides into one cell, so a measure has a divisor (§8.2)
    EXPECT_EQ(grid.columns(), 1u);
    EXPECT_EQ(grid.rows(), 1u);
}

TEST(GridLayoutChecks, ADerivedColumnCountStacksTheChildren) {
    // arrange a grid whose author wrote no shape, which is the derived default (§8.2)
    GridLayout grid;
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* second_ptr = second.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 40.0f}));
    // assert the children stack in one column of two rows, which is what a shape the author did not write means (§8.2)
    EXPECT_EQ(grid.columns(), 1u);
    EXPECT_EQ(grid.rows(), 2u);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 20.0f);
}

TEST(GridLayoutChecks, ATwoColumnShapePlacesChildrenSideBySide) {
    // arrange a two column grid whose rows are derived from the children (§8.2)
    GridLayout grid;
    grid.set_columns(2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 20.0f}));
    // assert both children land in adjacent cells of the single derived row (§8.2)
    EXPECT_EQ(grid.rows(), 1u);
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().width, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 0.0f);
}

TEST(GridLayoutChecks, AColumnCountDerivesTheRows) {
    // arrange a two column grid holding five children (§8.2)
    GridLayout grid;
    grid.set_columns(2);
    for (int index = 0; index < 5; index++)
        grid.add_child(std::make_unique<LayoutTestNode>());
    // act / assert three rows are what two columns need for five children (§8.2)
    EXPECT_EQ(grid.columns(), 2u);
    EXPECT_EQ(grid.rows(), 3u);
}

TEST(GridLayoutChecks, ARowCountDerivesTheColumns) {
    // arrange a two row grid holding five children (§8.2)
    GridLayout grid;
    grid.set_rows(2);
    for (int index = 0; index < 5; index++)
        grid.add_child(std::make_unique<LayoutTestNode>());
    // act / assert three columns are what two rows need for five children (§8.2)
    EXPECT_EQ(grid.columns(), 3u);
    EXPECT_EQ(grid.rows(), 2u);
}

TEST(GridLayoutChecks, AShapeWrittenInEitherOrderIsTheSame) {
    // arrange the same explicit shape written columns first and rows first (§8.2)
    GridLayout columns_first;
    columns_first.set_columns(2);
    columns_first.set_rows(3);
    GridLayout rows_first;
    rows_first.set_rows(3);
    rows_first.set_columns(2);
    // act / assert the two declaration orders resolve to the same grid (§8.2)
    EXPECT_EQ(columns_first.columns(), 2u);
    EXPECT_EQ(columns_first.rows(), 3u);
    EXPECT_EQ(rows_first.columns(), 2u);
    EXPECT_EQ(rows_first.rows(), 3u);
}

TEST(GridLayoutChecks, ADerivedShapeFollowsTheChildCount) {
    // arrange a two column grid with two children, which need one row (§8.2)
    GridLayout grid;
    grid.set_columns(2);
    grid.add_child(std::make_unique<LayoutTestNode>());
    grid.add_child(std::make_unique<LayoutTestNode>());
    EXPECT_EQ(grid.rows(), 1u);
    // act
    grid.add_child(std::make_unique<LayoutTestNode>());
    // assert the derived row count grew with the child, which is what a dynamic body needs (§8.2, AD-13)
    EXPECT_EQ(grid.rows(), 2u);
}

TEST(GridLayoutChecks, ANegativeShapeIsRefused) {
    // arrange a grid with a written column count (§8.2)
    GridLayout grid;
    grid.set_columns(2);
    // act
    grid.set_rows(-1);
    // assert the derived row count stands, because a negative row count names no shape (§8.2)
    EXPECT_EQ(grid.columns(), 2u);
    EXPECT_EQ(grid.rows(), 1u);
}

TEST(GridLayoutChecks, AWrittenZeroShapeReturnsToTheDerivedCounts) {
    // arrange an explicit shape over five children (§8.2)
    GridLayout grid;
    grid.set_columns(3);
    grid.set_rows(4);
    for (int index = 0; index < 5; index++)
        grid.add_child(std::make_unique<LayoutTestNode>());
    EXPECT_EQ(grid.columns(), 3u);
    EXPECT_EQ(grid.rows(), 4u);
    // act a zero on each axis, which asks the grid to derive again (§8.2)
    grid.set_columns(0);
    grid.set_rows(0);
    // assert the grid is one derived column again, which is the default shape (§8.2)
    EXPECT_EQ(grid.columns(), 1u);
    EXPECT_EQ(grid.rows(), 5u);
}

TEST(GridLayoutChecks, AnUnchangedWrittenShapeDoesNotInvalidate) {
    // arrange a grid whose explicit shape is settled, with no pending layout work (§8.2)
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.clear_dirty();
    // act writing the same column count again (§8.2)
    grid.set_columns(2);
    // assert nothing moved, so no frame is requested for a shape that did not change (§8.2)
    EXPECT_FALSE(grid.layout_dirty());
    EXPECT_EQ(grid.columns(), 2u);
    EXPECT_EQ(grid.rows(), 2u);
}

TEST(GridLayoutChecks, ASpanOnADerivedAxisDoesNotOverlap) {
    // arrange a default grid whose row count is derived from two children (§8.2)
    GridLayout grid;
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    // act a span the derived row count must grow around (§8.2)
    ASSERT_TRUE(grid.set_span(0, GridSpan{.columns = 1, .rows = 2}));
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 60.0f}));
    // assert the derived rows account for the span, so the second child does not land under it (§8.2)
    EXPECT_EQ(grid.rows(), 3u);
    EXPECT_FLOAT_EQ(first_ptr->bounds().height, 40.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 40.0f);
}

TEST(GridLayoutChecks, ATwoColumnShapePlacesFourChildrenInTwoRows) {
    // arrange four children in a two column grid, which needs two derived rows (§8.2)
    GridLayout grid;
    grid.set_columns(2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    auto fourth = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    LayoutTestNode* third_ptr = third.get();
    LayoutTestNode* fourth_ptr = fourth.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    grid.add_child(std::move(third));
    grid.add_child(std::move(fourth));
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // assert every child lands in its own cell, in source order (§8.2)
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(first_ptr->bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(third_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(third_ptr->bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(fourth_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(fourth_ptr->bounds().y, 20.0f);
}

TEST(GridLayoutChecks, AHiddenChildDoesNotConsumeACell) {
    // arrange
    GridLayout grid;
    grid.set_columns(2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    LayoutTestNode* third_ptr = third.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    grid.add_child(std::move(third));
    second_ptr->set_visible(false);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 20.0f}));
    // assert
    EXPECT_EQ(grid.rows(), 1u);
    EXPECT_EQ(second_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(third_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(third_ptr->bounds().y, 0.0f);
}

TEST(GridLayoutChecks, AHiddenFirstChildDoesNotShiftTheVisibleOnes) {
    // arrange
    GridLayout grid;
    grid.set_columns(2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    LayoutTestNode* third_ptr = third.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    grid.add_child(std::move(third));
    first_ptr->set_visible(false);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // assert
    EXPECT_EQ(grid.rows(), 1u);
    EXPECT_EQ(first_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(second_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(third_ptr->bounds().x, 20.0f);
}

TEST(GridLayoutChecks, AHiddenChildWithASpanClaimsNoCell) {
    // arrange
    GridLayout grid;
    grid.set_columns(3, 1);
    auto first = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    auto last = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* hidden_ptr = hidden.get();
    LayoutTestNode* last_ptr = last.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(hidden));
    grid.add_child(std::move(last));
    ASSERT_TRUE(grid.set_span(1, GridSpan{.columns = 2, .rows = 1}));
    hidden_ptr->set_visible(false);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    // assert
    EXPECT_EQ(hidden_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(first_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(last_ptr->bounds().x, 20.0f);
}

TEST(GridLayoutChecks, AVisibleSpanSurvivesHiddenSiblings) {
    // arrange
    GridLayout grid;
    grid.set_columns(3);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    auto fourth = std::make_unique<LayoutTestNode>();
    auto fifth = std::make_unique<LayoutTestNode>();
    auto sixth = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    LayoutTestNode* third_ptr = third.get();
    LayoutTestNode* fourth_ptr = fourth.get();
    LayoutTestNode* fifth_ptr = fifth.get();
    LayoutTestNode* sixth_ptr = sixth.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    grid.add_child(std::move(third));
    grid.add_child(std::move(fourth));
    grid.add_child(std::move(fifth));
    grid.add_child(std::move(sixth));
    ASSERT_TRUE(grid.set_span(0, GridSpan{.columns = 1, .rows = 2}));
    third_ptr->set_visible(false);
    fourth_ptr->set_visible(false);
    fifth_ptr->set_visible(false);
    sixth_ptr->set_visible(false);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 40.0f}));
    // assert
    EXPECT_EQ(grid.rows(), 2u);
    EXPECT_FLOAT_EQ(first_ptr->bounds().height, 40.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(second_ptr->bounds().height, 20.0f);
}

TEST(GridLayoutChecks, AVisibleColumnSpanSurvivesAHiddenSibling) {
    // arrange
    GridLayout grid;
    grid.set_rows(2);
    auto first = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    auto last = std::make_unique<LayoutTestNode>();
    LayoutTestNode* hidden_ptr = hidden.get();
    LayoutTestNode* last_ptr = last.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(hidden));
    grid.add_child(std::move(last));
    ASSERT_TRUE(grid.set_span(2, GridSpan{.columns = 2, .rows = 1}));
    hidden_ptr->set_visible(false);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 40.0f}));
    // assert
    EXPECT_EQ(grid.columns(), 3u);
    EXPECT_EQ(grid.rows(), 2u);
    EXPECT_EQ(hidden_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(last_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(last_ptr->bounds().y, 0.0f);
    EXPECT_FLOAT_EQ(last_ptr->bounds().width, 40.0f);
}

TEST(GridLayoutChecks, AReturnedDerivedShapeUsesTheWidenedColumns) {
    // arrange
    GridLayout grid;
    grid.set_rows(2);
    auto span = std::make_unique<LayoutTestNode>();
    grid.add_child(std::move(span));
    for (int index = 0; index < 4; index++)
        grid.add_child(std::make_unique<LayoutTestNode>());
    ASSERT_TRUE(grid.set_span(0, GridSpan{.columns = 2, .rows = 1}));
    // act
    grid.set_rows(0);
    // assert
    EXPECT_EQ(grid.columns(), 2u);
    EXPECT_EQ(grid.rows(), 3u);
}

TEST(GridLayoutChecks, AMixedMeasureExcludesAHiddenChild) {
    // arrange
    GridLayout grid;
    grid.set_columns(2);
    grid.set_gap(4.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    auto third = std::make_unique<LayoutTestNode>();
    first->intrinsic = Size{10.0f, 6.0f};
    hidden->intrinsic = Size{100.0f, 100.0f};
    third->intrinsic = Size{10.0f, 8.0f};
    LayoutTestNode* hidden_ptr = hidden.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(hidden));
    grid.add_child(std::move(third));
    hidden_ptr->set_visible(false);
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 24.0f);
    EXPECT_FLOAT_EQ(measured.height, 8.0f);
    EXPECT_EQ(hidden_ptr->measures, 0u);
}

TEST(GridLayoutChecks, AHiddenChildLowersTheDerivedRowCount) {
    // arrange
    GridLayout grid;
    grid.set_columns(2);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    auto hidden = std::make_unique<LayoutTestNode>();
    auto fourth = std::make_unique<LayoutTestNode>();
    auto fifth = std::make_unique<LayoutTestNode>();
    LayoutTestNode* hidden_ptr = hidden.get();
    LayoutTestNode* fourth_ptr = fourth.get();
    LayoutTestNode* fifth_ptr = fifth.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    grid.add_child(std::move(hidden));
    grid.add_child(std::move(fourth));
    grid.add_child(std::move(fifth));
    hidden_ptr->set_visible(false);
    // act
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}));
    // assert
    EXPECT_EQ(grid.rows(), 2u);
    EXPECT_EQ(hidden_ptr->arranges, 0u);
    EXPECT_FLOAT_EQ(fourth_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(fourth_ptr->bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(fifth_ptr->bounds().x, 20.0f);
    EXPECT_FLOAT_EQ(fifth_ptr->bounds().y, 20.0f);
}

TEST(GridLayoutChecks, AGridWithEveryChildHiddenMeasuresAsNothing) {
    // arrange
    GridLayout grid;
    grid.set_columns(2, 2);
    grid.set_gap(5.0f);
    auto first = std::make_unique<LayoutTestNode>();
    auto second = std::make_unique<LayoutTestNode>();
    LayoutTestNode* first_ptr = first.get();
    LayoutTestNode* second_ptr = second.get();
    grid.add_child(std::move(first));
    grid.add_child(std::move(second));
    first_ptr->set_visible(false);
    second_ptr->set_visible(false);
    Size measured{};
    // act
    grid.measure(SizeConstraints::unconstrained(), measured);
    grid.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 20.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
    EXPECT_EQ(first_ptr->arranges, 0u);
    EXPECT_EQ(second_ptr->arranges, 0u);
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

TEST(SizedBoxLayoutChecks, EmptyBoxWithoutChildMeasuresAsZero) {
    // arrange
    SizedBoxLayout box;
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(SizedBoxLayoutChecks, EmptyBoxWithExactDimensionsMeasuresAsThoseDimensions) {
    // arrange
    SizedBoxLayout box;
    box.set_width(120.0f);
    box.set_height(40.0f);
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(SizedBoxLayoutChecks, EmptyBoxWithMinDimensionsMeasuresAsFloor) {
    // arrange
    SizedBoxLayout box;
    box.set_min_width(80.0f);
    box.set_min_height(30.0f);
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 80.0f);
    EXPECT_FLOAT_EQ(measured.height, 30.0f);
}

TEST(SizedBoxLayoutChecks, EmptyBoxWithMaxDimensionsCapsFloor) {
    // arrange
    SizedBoxLayout box;
    box.set_min_width(100.0f);
    box.set_max_width(60.0f);
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 60.0f);
}

TEST(SizedBoxLayoutChecks, ChildSmallerThanExactWidthExpandsToWidth) {
    // arrange
    SizedBoxLayout box;
    box.set_width(120.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{40.0f, 20.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(SizedBoxLayoutChecks, ChildLargerThanExactWidthClampsToWidth) {
    // arrange
    SizedBoxLayout box;
    box.set_width(100.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{160.0f, 25.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 100.0f);
    EXPECT_FLOAT_EQ(measured.height, 25.0f);
}

TEST(SizedBoxLayoutChecks, ExactHeightForcesHeightOnChild) {
    // arrange
    SizedBoxLayout box;
    box.set_height(50.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{30.0f, 80.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 50.0f);
}

TEST(SizedBoxLayoutChecks, BothWidthAndHeightForceDimensions) {
    // arrange
    SizedBoxLayout box;
    box.set_width(200.0f);
    box.set_height(100.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{50.0f, 50.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 200.0f);
    EXPECT_FLOAT_EQ(measured.height, 100.0f);
}

TEST(SizedBoxLayoutChecks, FloorWidthExpandsSmallerChild) {
    // arrange
    SizedBoxLayout box;
    box.set_min_width(120.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{40.0f, 20.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(SizedBoxLayoutChecks, FloorWidthLeavesLargerChildUnchanged) {
    // arrange
    SizedBoxLayout box;
    box.set_min_width(120.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{160.0f, 20.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 160.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(SizedBoxLayoutChecks, CeilingWidthCapsLargerChild) {
    // arrange
    SizedBoxLayout box;
    box.set_max_width(150.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{200.0f, 30.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 150.0f);
    EXPECT_FLOAT_EQ(measured.height, 30.0f);
}

TEST(SizedBoxLayoutChecks, ExactWidthOverridesFloorAndCeiling) {
    // arrange
    SizedBoxLayout box;
    box.set_min_width(50.0f);
    box.set_max_width(200.0f);
    box.set_width(100.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{10.0f, 10.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 100.0f);
}

TEST(SizedBoxLayoutChecks, ClampsIntoParentConstraints) {
    // arrange
    SizedBoxLayout box;
    box.set_width(200.0f);
    Size measured{};
    // act
    box.measure(SizeConstraints::at_most(Size{150.0f, 100.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 150.0f);
}

TEST(SizedBoxLayoutChecks, HiddenChildIsIgnoredLikeEmptyBox) {
    // arrange
    SizedBoxLayout box;
    box.set_width(100.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{50.0f, 50.0f};
    child->set_visible(false);
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 100.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(SizedBoxLayoutChecks, ArrangesChildWithStretchAlignment) {
    // arrange
    SizedBoxLayout box;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{50.0f, 30.0f};
    child->set_horizontal_align(Align::Stretch);
    child->set_vertical_align(Align::Stretch);
    LayoutTestNode* child_ptr = child.get();
    box.add_child(std::move(child));
    // act
    box.arrange(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{120.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 120.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 40.0f);
}

TEST(SizedBoxLayoutChecks, ArrangesChildWithStartAlignment) {
    // arrange
    SizedBoxLayout box;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{50.0f, 30.0f};
    child->set_horizontal_align(Align::Start);
    child->set_vertical_align(Align::Start);
    LayoutTestNode* child_ptr = child.get();
    box.add_child(std::move(child));
    Size measured{};
    box.measure(SizeConstraints::unconstrained(), measured);
    // act
    box.arrange(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{120.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 10.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 20.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 50.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 30.0f);
}

TEST(SizedBoxLayoutChecks, ArrangesChildWithCenterAlignment) {
    // arrange
    SizedBoxLayout box;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{60.0f, 20.0f};
    child->set_horizontal_align(Align::Center);
    child->set_vertical_align(Align::Center);
    LayoutTestNode* child_ptr = child.get();
    box.add_child(std::move(child));
    Size measured{};
    box.measure(SizeConstraints::unconstrained(), measured);
    // act
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 30.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 10.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 60.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 20.0f);
}

TEST(SizedBoxLayoutChecks, ArrangesChildWithEndAlignment) {
    // arrange
    SizedBoxLayout box;
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{40.0f, 20.0f};
    child->set_horizontal_align(Align::End);
    child->set_vertical_align(Align::End);
    LayoutTestNode* child_ptr = child.get();
    box.add_child(std::move(child));
    Size measured{};
    box.measure(SizeConstraints::unconstrained(), measured);
    // act
    box.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{100.0f, 50.0f}));
    // assert
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 70.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 40.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 40.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 20.0f);
}

TEST(SizedBoxLayoutChecks, PropertyChangesInvalidateLayout) {
    // arrange
    SizedBoxLayout box;
    box.clear_dirty();
    // act
    box.set_width(100.0f);
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_EQ(box.width(), 100.0f);
    // act
    box.clear_dirty();
    box.set_height(50.0f);
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_EQ(box.height(), 50.0f);
    // act
    box.clear_dirty();
    box.set_min_width(80.0f);
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_EQ(box.min_width(), 80.0f);
    // act
    box.clear_dirty();
    box.set_min_height(40.0f);
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_EQ(box.min_height(), 40.0f);
    // act
    box.clear_dirty();
    box.set_max_width(200.0f);
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_EQ(box.max_width(), 200.0f);
    // act
    box.clear_dirty();
    box.set_max_height(100.0f);
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_EQ(box.max_height(), 100.0f);
}

TEST(SizedBoxLayoutChecks, UnchangedPropertyDoesNotInvalidate) {
    // arrange
    SizedBoxLayout box;
    box.set_width(100.0f);
    box.clear_dirty();
    // act
    box.set_width(100.0f);
    // assert
    EXPECT_FALSE(box.layout_dirty());
}

TEST(SizedBoxLayoutChecks, ArrangesAlignedChildInsideExplicitDimensions) {
    // arrange
    SizedBoxLayout box;
    box.set_width(120.0f);
    box.set_height(40.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{40.0f, 20.0f};
    child->set_horizontal_align(Align::Center);
    child->set_vertical_align(Align::Center);
    LayoutTestNode* child_ptr = child.get();
    box.add_child(std::move(child));
    Size measured{};
    box.measure(SizeConstraints::unconstrained(), measured);
    // act
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 40.0f}));
    // assert
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().x, 40.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().y, 10.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().width, 40.0f);
    EXPECT_FLOAT_EQ(child_ptr->bounds().height, 20.0f);
}

TEST(SizedBoxLayoutChecks, FloorHeightExpandsSmallerChild) {
    // arrange
    SizedBoxLayout box;
    box.set_min_height(60.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{40.0f, 20.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 60.0f);
}

TEST(SizedBoxLayoutChecks, CeilingHeightCapsTallerChild) {
    // arrange
    SizedBoxLayout box;
    box.set_max_height(50.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{40.0f, 80.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 50.0f);
}

TEST(SizedBoxLayoutChecks, InvertedParentCeilingIsHonoured) {
    // arrange
    SizedBoxLayout box;
    box.set_min_width(100.0f);
    auto child = std::make_unique<LayoutTestNode>();
    child->intrinsic = Size{100.0f, 20.0f};
    box.add_child(std::move(child));
    Size measured{};
    // act
    box.measure(SizeConstraints::at_most(Size{50.0f, 50.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 50.0f);
}

TEST(SizedBoxLayoutChecks, ClearingPropertiesRestoresUnconstrained) {
    // arrange
    SizedBoxLayout box;
    box.set_width(100.0f);
    box.set_height(50.0f);
    box.set_min_width(80.0f);
    box.set_min_height(40.0f);
    box.set_max_width(200.0f);
    box.set_max_height(100.0f);
    box.clear_dirty();
    // act
    box.clear_width();
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_FALSE(box.width().has_value());
    // act
    box.clear_dirty();
    box.clear_height();
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_FALSE(box.height().has_value());
    // act
    box.clear_dirty();
    box.clear_min_width();
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_FALSE(box.min_width().has_value());
    // act
    box.clear_dirty();
    box.clear_min_height();
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_FALSE(box.min_height().has_value());
    // act
    box.clear_dirty();
    box.clear_max_width();
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_FALSE(box.max_width().has_value());
    // act
    box.clear_dirty();
    box.clear_max_height();
    // assert
    EXPECT_TRUE(box.layout_dirty());
    EXPECT_FALSE(box.max_height().has_value());
}
