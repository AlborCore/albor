#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/constraints.h"
#include "render/frame_scene.h"
#include "render/render_context.h"
#include "ui/component.h"
#include "ui/frame_pipeline.h"
#include "ui/scene_builder.h"
#include "ui/ui_node.h"

using namespace albor;

namespace
{
    // the node test double, which records what the tree asked of it (§8.1, §8.2)
    class TestNode final : public UiNode
    {
    public:
        // how many times the node was measured, which a test asserts on (§8.2)
        std::size_t measures = 0;

        // how many times the node was arranged (§8.2)
        std::size_t arranges = 0;

        // how many times the node recorded into a context (§6.1)
        std::size_t draws = 0;

        // the extent this node reports when it measures itself (§8.2)
        Size intrinsic{10.0f, 20.0f};

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            measures++;
            // the container measures as the largest of its children, which is the contract of §8.2
            Size total = intrinsic;
            for (const auto& child : children()) {
                Size child_size{};
                child->measure(SizeConstraints::unconstrained(), child_size);
                total.width = std::max(total.width, child_size.width);
                total.height = std::max(total.height, child_size.height);
            }
            // the result is clamped into what the parent offered (§8.2)
            out_size = constraints.constrain(total);
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            arranges++;
            // the assigned bounds are recorded before the children see them (§8.2)
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
            for (const auto& child : children())
                child->arrange(bounds);
        }

        void render(RenderContext& context) override {
            draws++;
            // the node paints its own bounds so a recorded frame has content (§6.1)
            Paint paint;
            paint.fill = Color::rgb(0.2f, 0.4f, 0.6f);
            context.draw_rect(bounds(), paint);
            for (const auto& child : children())
                child->render(context);
        }

        [[nodiscard]] bool hit_test(Point logical_point) const override {
            // the double is a hit wherever its assigned bounds are (§8.3)
            return logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height;
        }

        void dispatch_event(Event& event) override {
            // the double claims any event it is offered, which stops the walk (§8.3)
            event.handled = true;
        }
    };

    // the text node test double, which draws a run so a scene carries text (§6.1)
    class TestTextNode final : public UiNode
    {
    public:
        // the run the node draws (§6.1)
        std::string text = "albor";

        // where the run is drawn (§6.1)
        Point origin{10.0f, 40.0f};

        void arrange(const Rect& bounds) override {
            // the assigned bounds are recorded so hit tests and drawing agree (§8.2)
            m_bounds = bounds;
        }

        void render(RenderContext& context) override {
            Paint paint;
            paint.fill = Color::rgb(1.0f, 1.0f, 1.0f);
            context.draw_text(TextLayout::from_text(text, 16.0f), origin, paint);
        }
    };

    // the component test double, which registers a property and paints a colour (§8.1)
    class TestComponent final : public Component
    {
    public:
        explicit TestComponent(BindingEngine* engine, std::string id) :
            Component(engine, std::move(id)) {
            // the component registers its reactive state once, at construction (§7.2)
            register_property(count);
            register_property(label);
            register_property(accent);
        }

        // the reactive count a binding drives (§7)
        Property<int> count;

        // the reactive label a binding derives from the count (§7)
        Property<std::string> label;

        // the reactive accent a binding derives from the count (§7)
        Property<int> accent;

        void render(RenderContext& context) override {
            // the component paints its background and then its children (§6.1)
            paint_background(context);
            for (const auto& child : children())
                child->render(context);
        }

        [[nodiscard]] Color background() const override { return Color::rgb(0.1f, 0.1f, 0.15f); }
    };
} // namespace

TEST(SizeConstraints, UnconstrainedConstraintsAllowAnything) {
    // arrange, act
    const SizeConstraints constraints = SizeConstraints::unconstrained();
    // assert
    EXPECT_TRUE(constraints.allows(Size{5000.0f, 5000.0f}));
}

TEST(SizeConstraints, UnconstrainedConstraintsHaveNoMinimum) {
    // arrange, act
    const SizeConstraints constraints = SizeConstraints::unconstrained();
    // assert
    EXPECT_FLOAT_EQ(constraints.minimum.width, 0.0f);
    EXPECT_FLOAT_EQ(constraints.minimum.height, 0.0f);
}

TEST(SizeConstraints, TightConstraintsAllowOnlyTheirSize) {
    // arrange, act
    const SizeConstraints constraints = SizeConstraints::tight(Size{100.0f, 50.0f});
    // assert
    EXPECT_TRUE(constraints.allows(Size{100.0f, 50.0f}));
    EXPECT_FALSE(constraints.allows(Size{110.0f, 50.0f}));
}

TEST(SizeConstraints, TightConstraintsConstrainToTheirSize) {
    // arrange
    const SizeConstraints constraints = SizeConstraints::tight(Size{100.0f, 50.0f});
    // act
    const Size result = constraints.constrain(Size{300.0f, 10.0f});
    // assert
    EXPECT_FLOAT_EQ(result.width, 100.0f);
    EXPECT_FLOAT_EQ(result.height, 50.0f);
}

TEST(SizeConstraints, AtMostConstraintsRefuseALargerExtent) {
    // arrange
    const SizeConstraints constraints = SizeConstraints::at_most(Size{200.0f, 100.0f});
    // act, assert
    EXPECT_TRUE(constraints.allows(Size{200.0f, 100.0f}));
    EXPECT_FALSE(constraints.allows(Size{201.0f, 100.0f}));
}

TEST(SizeConstraints, AConstraintRaisesAnExtentBelowTheMinimum) {
    // arrange
    const SizeConstraints constraints = SizeConstraints::tight(Size{100.0f, 50.0f});
    // act
    const Size result = constraints.constrain(Size{10.0f, 10.0f});
    // assert
    EXPECT_FLOAT_EQ(result.width, 100.0f);
    EXPECT_FLOAT_EQ(result.height, 50.0f);
}

TEST(SizeConstraints, AConstraintLetsAnInBoundsExtentThrough) {
    // arrange
    const SizeConstraints constraints = SizeConstraints::tight(Size{100.0f, 50.0f});
    // act
    const Size result = constraints.constrain(Size{80.0f, 40.0f});
    // assert
    EXPECT_FLOAT_EQ(result.width, 100.0f);
    EXPECT_FLOAT_EQ(result.height, 50.0f);
}

TEST(SizeConstraints, AnExtentTooNarrowIsRefused) {
    // arrange
    const SizeConstraints constraints = SizeConstraints::tight(Size{100.0f, 50.0f});
    // act, assert
    EXPECT_FALSE(constraints.allows(Size{99.0f, 50.0f}));
}

TEST(SizeConstraints, TwoConstraintSetsWithTheSameFieldsCompareEqual) {
    // arrange
    const SizeConstraints first = SizeConstraints::tight(Size{10.0f, 10.0f});
    const SizeConstraints second = SizeConstraints::tight(Size{10.0f, 10.0f});
    // act, assert
    EXPECT_EQ(first, second);
}

TEST(UiNode, ANewNodeHasNoParent) {
    // arrange, act
    TestNode node;
    // assert
    EXPECT_EQ(node.parent(), nullptr);
}

TEST(UiNode, ANewNodeHasNoChildren) {
    // arrange, act
    TestNode node;
    // assert
    EXPECT_EQ(node.child_count(), 0u);
}

TEST(UiNode, ANewNodeHasEmptyBounds) {
    // arrange, act
    TestNode node;
    // assert
    EXPECT_TRUE(node.bounds().empty());
}

TEST(UiNode, ANewNodeIsVisible) {
    // arrange, act
    TestNode node;
    // assert
    EXPECT_TRUE(node.visible());
}

TEST(UiNode, ANewNodeIsDirty) {
    // arrange, act
    TestNode node;
    // assert
    EXPECT_TRUE(node.render_dirty());
    EXPECT_TRUE(node.layout_dirty());
}

TEST(UiNode, AnAddedChildKnowsItsParent) {
    // arrange
    TestNode parent;
    // act
    parent.add_child(std::make_unique<TestNode>());
    // assert
    ASSERT_EQ(parent.child_count(), 1u);
    EXPECT_EQ(parent.child_at(0)->parent(), &parent);
    // add_child returns the parent it was called on, which is the node that took ownership (§8.1)
    EXPECT_EQ(&parent.add_child(std::make_unique<TestNode>()), &parent);
}

TEST(UiNode, AnAddedChildIsReachableByIndex) {
    // arrange
    TestNode parent;
    // act
    parent.add_child(std::make_unique<TestNode>());
    // assert
    EXPECT_NE(parent.child_at(0), nullptr);
}

TEST(UiNode, AnIndexPastTheEndNamesNoChild) {
    // arrange
    TestNode parent;
    // act, assert
    EXPECT_EQ(parent.child_at(4), nullptr);
}

TEST(UiNode, AddingANullChildChangesNothing) {
    // arrange
    TestNode parent;
    // act
    parent.add_child(nullptr);
    // assert
    EXPECT_EQ(parent.child_count(), 0u);
}

TEST(UiNode, InsertingANullChildChangesNothing) {
    // arrange
    TestNode parent;
    // act
    parent.insert_child(0, nullptr);
    // assert
    EXPECT_EQ(parent.child_count(), 0u);
}

TEST(UiNode, ChildrenAreReachableThroughTheAccessor) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    parent.add_child(std::make_unique<TestNode>());
    // act, assert
    EXPECT_EQ(parent.children().size(), 2u);
}

TEST(UiNode, InsertingAtZeroPlacesTheChildFirst) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    // act
    parent.insert_child(0, std::make_unique<TestNode>());
    // assert
    EXPECT_EQ(parent.child_count(), 2u);
    EXPECT_EQ(parent.child_at(0)->parent(), &parent);
}

TEST(UiNode, InsertingPastTheEndAppends) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    // act
    parent.insert_child(99, std::make_unique<TestNode>());
    // assert
    EXPECT_EQ(parent.child_count(), 2u);
    EXPECT_NE(parent.child_at(1), nullptr);
}

TEST(UiNode, RemovingAChildHandsItBack) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    UiNode* child = parent.child_at(0);
    // act
    std::unique_ptr<UiNode> removed = parent.remove_child(child);
    // assert
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed.get(), child);
    EXPECT_EQ(parent.child_count(), 0u);
}

TEST(UiNode, ARemovedChildBecomesARoot) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    // act
    std::unique_ptr<UiNode> removed = parent.remove_child(parent.child_at(0));
    // assert
    EXPECT_EQ(removed->parent(), nullptr);
}

TEST(UiNode, RemovingAStrangerReportsNothingRemoved) {
    // arrange
    TestNode parent;
    TestNode stranger;
    // act, assert
    EXPECT_EQ(parent.remove_child(&stranger), nullptr);
}

TEST(UiNode, MeasureRecordsTheOfferedConstraints) {
    // arrange
    TestNode node;
    Size size{};
    // act
    node.measure(SizeConstraints::tight(Size{40.0f, 30.0f}), size);
    // assert
    EXPECT_EQ(node.measures, 1u);
    EXPECT_FLOAT_EQ(size.width, 40.0f);
}

TEST(UiNode, MeasureStoresTheResultAsTheMeasuredSize) {
    // arrange
    TestNode node;
    Size size{};
    // act
    node.measure(SizeConstraints::tight(Size{40.0f, 30.0f}), size);
    // assert
    EXPECT_FLOAT_EQ(node.measured_size().width, 40.0f);
}

TEST(UiNode, AContainerMeasuresAsItsLargestChild) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    static_cast<TestNode&>(*parent.child_at(0)).intrinsic = Size{200.0f, 50.0f};
    Size size{};
    // act
    parent.measure(SizeConstraints::unconstrained(), size);
    // assert
    EXPECT_FLOAT_EQ(size.width, 200.0f);
}

TEST(UiNode, ArrangingAssignsTheBounds) {
    // arrange
    TestNode node;
    const Rect bounds = Rect::from_origin_size(Point{5.0f, 6.0f}, Size{70.0f, 80.0f});
    // act
    node.arrange(bounds);
    // assert
    EXPECT_EQ(node.bounds(), bounds);
    EXPECT_EQ(node.arranges, 1u);
}

TEST(UiNode, ArrangingFollowsTheMeasuredSize) {
    // arrange
    TestNode node;
    // act
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{70.0f, 80.0f}));
    // assert
    EXPECT_FLOAT_EQ(node.measured_size().height, 80.0f);
}

TEST(UiNode, ArrangingPushesTheSameBoundsIntoChildren) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    TestNode& child = static_cast<TestNode&>(*parent.child_at(0));
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f});
    // act
    parent.arrange(bounds);
    // assert
    EXPECT_EQ(child.bounds(), bounds);
    EXPECT_EQ(child.arranges, 1u);
}

TEST(UiNode, HitTestingReportsAPointInside) {
    // arrange
    TestNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act, assert
    EXPECT_TRUE(node.hit_test(Point{50.0f, 50.0f}));
}

TEST(UiNode, HitTestingRejectsAPointOutside) {
    // arrange
    TestNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act, assert
    EXPECT_FALSE(node.hit_test(Point{150.0f, 50.0f}));
}

TEST(UiNode, HitTestingRejectsTheTrailingEdge) {
    // arrange
    TestNode node;
    node.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act, assert
    EXPECT_FALSE(node.hit_test(Point{100.0f, 50.0f}));
}

TEST(UiNode, AContainerHitsThroughItsChild) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    parent.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act, assert
    EXPECT_TRUE(parent.hit_test(Point{10.0f, 10.0f}));
}

TEST(UiNode, AClaimedEventStopsTheWalk) {
    // arrange
    TestNode parent;
    parent.add_child(std::make_unique<TestNode>());
    Event event;
    // act
    parent.dispatch_event(event);
    // assert
    EXPECT_TRUE(event.handled);
}

TEST(UiNode, InvalidateMarksTheNodeDirty) {
    // arrange
    TestNode node;
    node.clear_dirty();
    // act
    node.invalidate();
    // assert
    EXPECT_TRUE(node.render_dirty());
}

TEST(UiNode, InvalidateDoesNotMarkLayoutDirty) {
    // arrange
    TestNode node;
    node.clear_dirty();
    // act
    node.invalidate();
    // assert
    EXPECT_FALSE(node.layout_dirty());
}

TEST(UiNode, InvalidateLayoutMarksLayoutDirty) {
    // arrange
    TestNode node;
    node.clear_dirty();
    // act
    node.invalidate_layout();
    // assert
    EXPECT_TRUE(node.layout_dirty());
}

TEST(UiNode, InvalidateLayoutAlsoMarksRenderDirty) {
    // arrange
    TestNode node;
    node.clear_dirty();
    // act
    node.invalidate_layout();
    // assert
    EXPECT_TRUE(node.render_dirty());
}

TEST(UiNode, LayoutInvalidationBubblesToTheAncestors) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    TestNode& middle = static_cast<TestNode&>(*root.child_at(0));
    middle.add_child(std::make_unique<TestNode>());
    TestNode& leaf = static_cast<TestNode&>(*middle.child_at(0));
    root.clear_dirty();
    middle.clear_dirty();
    leaf.clear_dirty();
    // act
    leaf.invalidate_layout();
    // assert
    EXPECT_TRUE(leaf.layout_dirty());
    EXPECT_TRUE(middle.layout_dirty());
    EXPECT_TRUE(root.layout_dirty());
}

TEST(UiNode, RenderInvalidationBubblesToTheAncestors) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    TestNode& leaf = static_cast<TestNode&>(*root.child_at(0));
    root.clear_dirty();
    leaf.clear_dirty();
    // act
    leaf.invalidate();
    // assert
    EXPECT_TRUE(leaf.render_dirty());
    EXPECT_TRUE(root.render_dirty());
}

TEST(UiNode, OnlyTheRootReachesTheInvalidationHandler) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    TestNode& leaf = static_cast<TestNode&>(*root.child_at(0));
    std::size_t requests = 0;
    root.set_invalidation_handler([&requests] { requests++; });
    // act
    leaf.invalidate();
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(UiNode, ADeeperInvalidationStillRequestsOneFrame) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    TestNode& middle = static_cast<TestNode&>(*root.child_at(0));
    middle.add_child(std::make_unique<TestNode>());
    TestNode& leaf = static_cast<TestNode&>(*middle.child_at(0));
    std::size_t requests = 0;
    root.set_invalidation_handler([&requests] { requests++; });
    // act
    leaf.invalidate_layout();
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(UiNode, ANodeWithoutAHandlerInvalidatesQuietly) {
    // arrange
    TestNode node;
    // act
    node.invalidate();
    // assert
    EXPECT_TRUE(node.render_dirty());
}

TEST(UiNode, ClearingTheDirtyFlagsClearsTheWholeSubtree) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    TestNode& child = static_cast<TestNode&>(*root.child_at(0));
    root.invalidate_layout();
    // act
    root.clear_dirty();
    // assert
    EXPECT_FALSE(root.layout_dirty());
    EXPECT_FALSE(child.layout_dirty());
}

TEST(UiNode, HidingANodeInvalidatesIt) {
    // arrange
    TestNode node;
    node.clear_dirty();
    // act
    node.set_visible(false);
    // assert
    EXPECT_FALSE(node.visible());
    EXPECT_TRUE(node.render_dirty());
}

TEST(UiNode, ShowingANodeAgainInvalidatesIt) {
    // arrange
    TestNode node;
    node.set_visible(false);
    node.clear_dirty();
    // act
    node.set_visible(true);
    // assert
    EXPECT_TRUE(node.render_dirty());
}

TEST(UiNode, SettingTheSameVisibilityChangesNothing) {
    // arrange
    TestNode node;
    node.clear_dirty();
    // act
    node.set_visible(true);
    // assert
    EXPECT_FALSE(node.render_dirty());
}

TEST(UiNode, ANewNodeCarriesNoAuthorIdentity) {
    // arrange, act
    TestNode node;
    // assert
    EXPECT_FALSE(node.identity().identified());
    EXPECT_TRUE(node.identity().explicit_id.empty());
}

TEST(UiNode, AnAuthorIdentityIsStoredAsWritten) {
    // arrange
    TestNode node;
    // act
    node.set_explicit_id("action_button");
    // assert
    EXPECT_TRUE(node.identity().identified());
    EXPECT_EQ(node.identity().explicit_id, "action_button");
}

TEST(UiNode, TheInTreeIdentifierIsACorrelationHintOnly) {
    // arrange
    TestNode node;
    // act
    node.set_node_id(42);
    // assert
    EXPECT_EQ(node.identity().node, 42u);
    EXPECT_FALSE(node.identity().identified());
}

TEST(UiNode, AVIsitReachesTheNodeAndItsDescendants) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    static_cast<TestNode&>(*root.child_at(0)).add_child(std::make_unique<TestNode>());
    std::size_t visited = 0;
    // act
    root.visit([&visited](const UiNode&) { visited++; });
    // assert
    EXPECT_EQ(visited, 3u);
}

TEST(UiNode, AVisitReachesTheParentBeforeItsChild) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    std::size_t root_first = 0;
    std::size_t index = 0;
    // act
    root.visit([&](const UiNode& node) {
        if (node.parent() == nullptr)
            root_first = index;
        index++;
    });
    // assert
    EXPECT_EQ(root_first, 0u);
}

TEST(UiNode, AMutableVisitReachesTheSameNodes) {
    // arrange
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    std::size_t visited = 0;
    // act
    root.visit([&visited](UiNode&) { visited++; });
    // assert
    EXPECT_EQ(visited, 2u);
}

TEST(Component, AComponentRegistersWithTheEngine) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent component(&engine, "root");
    // assert
    EXPECT_EQ(component.engine(), &engine);
    EXPECT_TRUE(component.identity().identified());
}

TEST(Component, AComponentCarriesItsAuthorIdentity) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent component(&engine, "counter");
    // assert
    EXPECT_EQ(component.identity().explicit_id, "counter");
}

TEST(Component, AComponentRegistersItsProperties) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent component(&engine, "counter");
    // assert
    EXPECT_EQ(component.property_count(), 3u);
}

TEST(Component, RegisteredPropertiesCarryTheComponentBase) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent component(&engine, "counter");
    // assert
    EXPECT_EQ(component.count.id().base, component.property_base());
    EXPECT_EQ(component.label.id().base, component.property_base());
}

TEST(Component, RegisteredPropertiesGetDistinctSlots) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent component(&engine, "counter");
    // assert
    EXPECT_NE(component.count.id().slot, component.label.id().slot);
}

TEST(Component, TwoComponentsGetDifferentPropertyBases) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent first(&engine, "first");
    const TestComponent second(&engine, "second");
    // assert
    EXPECT_NE(first.property_base(), second.property_base());
}

TEST(Component, ADetachedComponentRegistersNoProperties) {
    // arrange, act
    TestComponent component(nullptr, "detached");
    // assert
    EXPECT_EQ(component.engine(), nullptr);
    EXPECT_EQ(component.property_count(), 0u);
}

TEST(Component, ADetachedComponentCarriesAnInvalidPropertyIdentity) {
    // arrange, act
    TestComponent component(nullptr, "detached");
    // assert
    EXPECT_FALSE(is_valid(component.count.id()));
}

TEST(Component, ArrangingAComponentSetsItsPreferredSize) {
    // arrange
    BindingEngine engine;
    TestComponent component(&engine, "counter");
    // act
    component.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 90.0f}));
    // assert
    EXPECT_FLOAT_EQ(component.preferred_size().width, 120.0f);
}

TEST(Component, ANewComponentPrefersNoSize) {
    // arrange
    BindingEngine engine;
    // act
    const TestComponent component(&engine, "counter");
    // assert
    EXPECT_FLOAT_EQ(component.preferred_size().width, 0.0f);
}

TEST(Component, AComponentIsTransparentByDefault) {
    // arrange, act
    const Component component(nullptr, "plain");
    // assert
    EXPECT_FLOAT_EQ(component.background().alpha, 0.0f);
}

TEST(Component, AComponentIsAHitWhereItHasArea) {
    // arrange
    BindingEngine engine;
    TestComponent component(&engine, "counter");
    component.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act, assert
    EXPECT_TRUE(component.hit_test(Point{50.0f, 50.0f}));
}

TEST(Component, AComponentIsNotAHitOutsideItsBounds) {
    // arrange
    BindingEngine engine;
    TestComponent component(&engine, "counter");
    component.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    // act, assert
    EXPECT_FALSE(component.hit_test(Point{150.0f, 50.0f}));
}

TEST(Component, AnUnarrangedComponentHasNoAreaToHit) {
    // arrange
    BindingEngine engine;
    TestComponent component(&engine, "counter");
    // act, assert
    EXPECT_FALSE(component.hit_test(Point{0.0f, 0.0f}));
}

TEST(Component, AComponentPropagatesABoundChangeToItsParent) {
    // arrange
    BindingEngine engine;
    TestNode root;
    root.add_child(std::make_unique<TestComponent>(&engine, "child"));
    root.clear_dirty();
    // act
    root.child_at(0)->invalidate();
    // assert
    EXPECT_TRUE(root.render_dirty());
}

TEST(SceneRecorder, ANewRecorderHasNoTransform) {
    // arrange, act
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    EXPECT_EQ(recorder.transform(), Transform{});
}

TEST(SceneRecorder, ANewRecorderHasAnEmptyClip) {
    // arrange, act
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    EXPECT_TRUE(recorder.clip().empty());
}

TEST(SceneRecorder, ANewRecorderHasAnEmptyStack) {
    // arrange, act
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    EXPECT_EQ(recorder.depth(), 0u);
}

TEST(SceneRecorder, ASavePushesTheStateStack) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.save();
    // assert
    EXPECT_EQ(recorder.depth(), 1u);
}

TEST(SceneRecorder, ARestorePopsTheStateStack) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    recorder.save();
    // act
    recorder.restore();
    // assert
    EXPECT_EQ(recorder.depth(), 0u);
}

TEST(SceneRecorder, ARestoreWithNothingPushedLeavesTheStackEmpty) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.restore();
    // assert
    EXPECT_EQ(recorder.depth(), 0u);
}

TEST(SceneRecorder, ARestoreBringsBackTheTransform) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    recorder.save();
    recorder.set_transform(Transform::translation(50.0f, 60.0f));
    // act
    recorder.restore();
    // assert
    EXPECT_EQ(recorder.transform(), Transform{});
}

TEST(SceneRecorder, ATransformIsReadBack) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.set_transform(Transform::translation(15.0f, 25.0f));
    // assert
    EXPECT_FLOAT_EQ(recorder.transform().translate_x, 15.0f);
}

TEST(SceneRecorder, AClipIsRecordedForReading) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 50.0f}));
    // assert
    EXPECT_FLOAT_EQ(recorder.clip().width, 100.0f);
}

TEST(SceneRecorder, ARoundedClipRecordsItsRectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.clip_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 40.0f}), 8.0f);
    // assert
    EXPECT_FLOAT_EQ(recorder.clip().height, 40.0f);
}

TEST(SceneRecorder, AnEmptyRectangleIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{0.0f, 10.0f}), Paint{});
    // assert
    EXPECT_TRUE(recorder.scene()->rects().empty());
}

TEST(SceneRecorder, ARectangleIsRecordedInSceneCoordinates) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{30.0f, 40.0f}), Paint{});
    // assert
    ASSERT_EQ(recorder.scene()->rects().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->rects()[0].x, 10.0f);
}

TEST(SceneRecorder, ATransformMovesTheRecordedRectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    recorder.set_transform(Transform::translation(100.0f, 50.0f));
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{30.0f, 40.0f}), Paint{});
    // assert
    ASSERT_EQ(recorder.scene()->rects().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->rects()[0].x, 110.0f);
}

TEST(SceneRecorder, ARoundedRectangleIsRecordedAsARectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{30.0f, 30.0f}), 5.0f, Paint{});
    // assert
    EXPECT_EQ(recorder.scene()->rects().size(), 1u);
}

TEST(SceneRecorder, ARecordedColourTravelsWithTheRectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    Paint paint;
    paint.fill = Color::rgb(0.25f, 0.5f, 0.75f);
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), paint);
    // assert
    ASSERT_EQ(recorder.scene()->rect_colors().size(), 1u);
    EXPECT_EQ(recorder.scene()->rect_colors()[0], (Color::rgb(0.25f, 0.5f, 0.75f)));
}

TEST(SceneRecorder, ATextRunIsRecordedWithItsOrigin) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_text(TextLayout::from_text("albor", 16.0f), Point{30.0f, 40.0f}, Paint{});
    // assert
    ASSERT_EQ(recorder.scene()->texts().size(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->text_origins()[0].x, 30.0f);
}

TEST(SceneRecorder, AnEmptyTextRunIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_text(TextLayout::from_text("", 16.0f), Point{0.0f, 0.0f}, Paint{});
    // assert
    EXPECT_TRUE(recorder.scene()->texts().empty());
}

TEST(SceneRecorder, TheSceneCarriesTheMetricsItWasBuiltFor) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 2.0f));
    // act, assert
    EXPECT_FLOAT_EQ(recorder.scene()->metrics().logical_size.width, 1024.0f);
}

TEST(SceneRecorder, TheSceneCarriesTheBackgroundItWasGiven) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.set_background(Color::rgb(0.1f, 0.2f, 0.3f));
    // assert
    EXPECT_EQ(recorder.scene()->background(), (Color::rgb(0.1f, 0.2f, 0.3f)));
}

TEST(SceneRecorder, AnEmptySceneStillCarriesTheMetrics) {
    // arrange, act
    SceneRecorder recorder(WindowMetrics::from_logical(Size{640.0f, 480.0f}, 1.0f));
    // assert
    EXPECT_TRUE(recorder.scene()->rects().empty());
    EXPECT_FLOAT_EQ(recorder.scene()->metrics().scale_factor, 1.0f);
}

TEST(SceneBuilder, ANewBuilderHasCountedNothing) {
    // arrange, act
    SceneBuilder builder;
    // assert
    EXPECT_EQ(builder.visited_count(), 0u);
    EXPECT_EQ(builder.drawn_count(), 0u);
    EXPECT_EQ(builder.last_node_id(), 0u);
}

TEST(SceneBuilder, BuildingProducesAnImmutableScene) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_FLOAT_EQ(scene->metrics().logical_size.width, 800.0f);
}

TEST(SceneBuilder, BuildingNumbersTheRootFirst) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    // act
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(root.identity().node, 1u);
    EXPECT_EQ(builder.last_node_id(), 1u);
    EXPECT_NE(built_1, nullptr);
}

TEST(SceneBuilder, BuildingNumbersTheDescendantsToo) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    // act
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(root.identity().node, 1u);
    EXPECT_EQ(root.child_at(0)->identity().node, 2u);
    EXPECT_NE(built_1, nullptr);
}

TEST(SceneBuilder, BuildingVisitsEveryNode) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    root.add_child(std::make_unique<TestNode>());
    static_cast<TestNode&>(*root.child_at(0)).add_child(std::make_unique<TestNode>());
    // act
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(builder.visited_count(), 3u);
    EXPECT_NE(built_1, nullptr);
}

TEST(SceneBuilder, BuildingMeasuresTheRootOnce) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    // act
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(builder.laid_out_count(), 1u);
    EXPECT_EQ(root.measures, 1u);
    EXPECT_NE(built_1, nullptr);
}

TEST(SceneBuilder, BuildingArrangesTheRootToTheWindow) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    // act
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_FLOAT_EQ(root.bounds().width, 800.0f);
    EXPECT_EQ(root.arranges, 1u);
    EXPECT_NE(built_1, nullptr);
}

TEST(SceneBuilder, BuildingRecordsTheTreeContent) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(scene->rects().size(), 1u);
    EXPECT_EQ(root.draws, 1u);
}

TEST(SceneBuilder, BuildingCarriesTheBackground) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.4f, 0.5f, 0.6f));
    // assert
    EXPECT_EQ(scene->background(), (Color::rgb(0.4f, 0.5f, 0.6f)));
}

TEST(SceneBuilder, BuildingRecordsTextIntoTheScene) {
    // arrange
    SceneBuilder builder;
    TestTextNode root;
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    ASSERT_EQ(scene->texts().size(), 1u);
    EXPECT_EQ(scene->texts()[0].text, "albor");
}

TEST(SceneBuilder, AnInvisibleTreeDrawsNothing) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    root.set_visible(false);
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_TRUE(scene->rects().empty());
}

TEST(SceneBuilder, BuildingClearsTheDirtyFlags) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    root.invalidate_layout();
    // act
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_FALSE(root.layout_dirty());
    EXPECT_FALSE(root.render_dirty());
    EXPECT_NE(built_1, nullptr);
}

TEST(SceneBuilder, BuildingRenumbersTheTreeFromOne) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    const std::shared_ptr<const FrameScene> built_1 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    const std::shared_ptr<const FrameScene> built_2 = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(builder.last_node_id(), 1u);
    EXPECT_NE(built_1, nullptr);
    EXPECT_NE(built_2, nullptr);
}

TEST(FramePipeline, APipelineExposesItsRoot) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // act, assert
    EXPECT_NE(&pipeline.root(), nullptr);
}

TEST(FramePipeline, APipelineHasBuiltNothingYet) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // assert
    EXPECT_EQ(pipeline.frame_count(), 0u);
}

TEST(FramePipeline, BuildingProducesAFrame) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // act
    const std::shared_ptr<const FrameScene> scene = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(pipeline.frame_count(), 1u);
}

TEST(FramePipeline, AnInvalidationRequestsAFrame) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // act
    pipeline.root().invalidate();
    // assert
    EXPECT_EQ(pipeline.request_count(), 1u);
}

TEST(FramePipeline, AFrameRequestReachesTheInstalledCallback) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    std::size_t requests = 0;
    pipeline.set_frame_request([&requests] { requests++; });
    // act
    pipeline.root().invalidate();
    // assert
    EXPECT_EQ(requests, 1u);
}

TEST(FramePipeline, ACleanTreeBuildsNothingWhenAskedForADirtyBuild) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    const std::shared_ptr<const FrameScene> built_1 = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    const std::shared_ptr<const FrameScene> scene = pipeline.build_if_dirty(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    EXPECT_EQ(scene, nullptr);
    EXPECT_EQ(pipeline.frame_count(), 1u);
    EXPECT_NE(built_1, nullptr);
}

TEST(FramePipeline, ADirtyTreeBuildsWhenAskedForADirtyBuild) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    const std::shared_ptr<const FrameScene> built_1 = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    pipeline.root().invalidate();
    // act
    const std::shared_ptr<const FrameScene> scene = pipeline.build_if_dirty(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(pipeline.frame_count(), 2u);
    EXPECT_NE(built_1, nullptr);
}

TEST(FramePipeline, APendingTreeReportsItselfPending) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // act, assert
    EXPECT_TRUE(pipeline.pending());
}

TEST(FramePipeline, ABuiltTreeReportsItselfClean) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // act
    const std::shared_ptr<const FrameScene> built_1 = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    EXPECT_FALSE(pipeline.pending());
    EXPECT_NE(built_1, nullptr);
}

TEST(FramePipeline, SettingTheBackgroundRequestsAFrame) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    const std::shared_ptr<const FrameScene> built_1 = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    pipeline.set_background(Color::rgb(0.1f, 0.1f, 0.1f));
    // assert
    EXPECT_EQ(pipeline.request_count(), 1u);
    EXPECT_NE(built_1, nullptr);
}

TEST(FramePipeline, TheBuiltSceneCarriesTheBackground) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    pipeline.set_background(Color::rgb(0.2f, 0.3f, 0.4f));
    // act
    const std::shared_ptr<const FrameScene> scene = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->background(), (Color::rgb(0.2f, 0.3f, 0.4f)));
}

TEST(FramePipeline, SubmittingWithoutAnExecutorFails) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    const std::shared_ptr<const FrameScene> built_1 = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act, assert
    EXPECT_FALSE(pipeline.submit());
    EXPECT_NE(built_1, nullptr);
}

TEST(FramePipeline, SubmittingANullSceneFails) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    // act, assert
    EXPECT_FALSE(pipeline.submit(nullptr));
}

TEST(FramePipeline, AReactiveChangeReachesTheNextFrame) {
    // arrange
    BindingEngine engine;
    auto component = std::make_unique<TestComponent>(&engine, "counter");
    const PropertyId count_id = component->count.id();
    component->accent.bind([live = component.get()] { return live->count.get() + 1; }, {count_id});
    component->count.set(4);
    FramePipeline pipeline(engine, std::move(component));
    pipeline.set_background(Color::rgb(0.0f, 0.0f, 0.0f));
    // act
    const std::shared_ptr<const FrameScene> scene = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->rects().size(), 1u);
}

TEST(FramePipeline, ABoundLabelFollowsItsCount) {
    // arrange
    BindingEngine engine;
    auto component = std::make_unique<TestComponent>(&engine, "counter");
    const PropertyId count_id = component->count.id();
    component->label.bind([count = component.get()] { return "count " + std::to_string(count->count.get()); }, {count_id});
    // act
    component->count.set(4);
    engine.settle();
    // assert
    EXPECT_EQ(component->label.get(), "count 4");
}

TEST(FramePipeline, ABuildSettlesTheReactiveGraphFirst) {
    // arrange
    BindingEngine engine;
    auto component = std::make_unique<TestComponent>(&engine, "counter");
    TestComponent* live = component.get();
    const PropertyId count_id = component->count.id();
    component->accent.bind([live] { return live->count.get() > 0 ? 7 : 0; }, {count_id});
    component->count.set(1);
    FramePipeline pipeline(engine, std::move(component));
    // act
    const std::shared_ptr<const FrameScene> built_1 = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert
    EXPECT_EQ(live->accent.get(), 7);
    EXPECT_NE(built_1, nullptr);
}

TEST(Event, ANewEventIsAPointerDown) {
    // arrange, act
    const Event event;
    // assert
    EXPECT_EQ(event.kind, EventKind::PointerDown);
}

TEST(Event, ANewEventIsNotHandled) {
    // arrange, act
    const Event event;
    // assert
    EXPECT_FALSE(event.handled);
}

TEST(Event, AnEventCarriesItsPosition) {
    // arrange
    Event event;
    // act
    event.position = Point{12.0f, 34.0f};
    // assert
    EXPECT_FLOAT_EQ(event.position.x, 12.0f);
}

TEST(Event, AnEventCarriesNoTextByDefault) {
    // arrange, act
    const Event event;
    // assert
    EXPECT_TRUE(event.text.empty());
}

TEST(Event, AnEventCarriesNoScrollByDefault) {
    // arrange, act
    const Event event;
    // assert
    EXPECT_FLOAT_EQ(event.scroll, 0.0f);
}

TEST(NodeIdentity, ADefaultIdentityIsNotIdentified) {
    // arrange, act
    const NodeIdentity identity;
    // assert
    EXPECT_FALSE(identity.identified());
}

TEST(NodeIdentity, AnIdentityWithAnExplicitIdIsIdentified) {
    // arrange
    NodeIdentity identity;
    // act
    identity.explicit_id = "action_button";
    // assert
    EXPECT_TRUE(identity.identified());
}

TEST(NodeIdentity, AnInTreeIdentifierAloneIsNotAnIdentity) {
    // arrange
    NodeIdentity identity;
    // act
    identity.node = 7;
    // assert
    EXPECT_FALSE(identity.identified());
    EXPECT_EQ(identity.node, 7u);
}

TEST(NodeIdentity, TwoIdentitiesWithTheSameFieldsCompareEqual) {
    // arrange
    NodeIdentity first;
    first.node = 3;
    first.explicit_id = "a";
    NodeIdentity second;
    second.node = 3;
    second.explicit_id = "a";
    // act, assert
    EXPECT_EQ(first, second);
}

TEST(NodeIdentity, TwoIdentitiesWithDifferentIdsDiffer) {
    // arrange
    NodeIdentity first;
    first.explicit_id = "a";
    NodeIdentity second;
    second.explicit_id = "b";
    // act, assert
    EXPECT_NE(first, second);
}
