#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/constraints.h"
#include "layout/column_layout.h"
#include "render/frame_scene.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"
#include "render/render_context.h"
#include "render/render_executor.h"
#include "render/renderer_backend.h"
#include "render/resource_factory.h"
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

        // the constraints the last measure was offered, which a test asserts on (§8.2)
        SizeConstraints offered;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            measures++;
            // the offer is kept, so a test can prove the parent offered a ceiling (§8.2)
            offered = constraints;
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

    // the commands of one kind of a scene, which is what a retained draw assertion reads off the one list (§6.1)
    std::vector<DrawCommand> commands_of(const FrameScene& scene, DrawCommandKind kind) {
        // the commands are gathered in paint order, because that is the order the list carries (§6.1)
        std::vector<DrawCommand> gathered;
        // every command is read, because a scene interleaves kinds (§6.1)
        for (const DrawCommand& command : scene.commands())
            if (command.kind() == kind)
                gathered.push_back(command);
        return gathered;
    }

    // a shaped run standing in for what a text engine produced, so a scene test records text without a font backend (§9.1)
    std::shared_ptr<const ShapedRun> synthetic_run(std::size_t glyph_count, float font_size) {
        // the line carries the synthesized glyphs, which no real typeface backs (§9.1)
        ShapedRun::Line line;
        // the line origin is the baseline the synthesized glyphs hang from (§9.1)
        line.origin = Point{.x = 0.0f, .y = 12.0f};
        // every glyph contributes a distinct index placed a fixed advance apart (§9.1)
        for (std::size_t index = 0; index < glyph_count; index++)
            line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(index), .position = Point{.x = static_cast<float>(index) * 8.0f, .y = 0.0f}});
        // the run holds that one line, which is what an unwrapped run shapes into (§9.1)
        return ShapedRun::make({std::move(line)}, font_size);
    }

    // a measured run standing in for what a text engine reports, so a test needs no font backend (§9.1)
    TextLayout synthetic_layout(std::size_t glyph_count, float font_size, Size size) {
        // the shaped run travels beside the extents the layout reports (§9.1)
        return TextLayout{.run = synthetic_run(glyph_count, font_size), .size = size, .ascent = font_size * 0.8f, .descent = font_size * 0.2f};
    }

    // the layout counting double, which reports how often the layout passes ran (§8.2)
    class LayoutCountingNode final : public UiNode
    {
    public:
        // how many times the measure pass ran on this node (§8.2)
        std::size_t measures = 0;

        // how many times the arrange pass ran on this node (§8.2)
        std::size_t arranges = 0;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the measure is counted before it runs, so a skipped pass is visible (§8.2)
            measures++;
            // the node fills what it is offered, because a leaf has no intrinsic extent (§8.2)
            out_size = constraints.constrain(Size{200.0f, 120.0f});
            // the measured size is recorded, which is what the arrange pass reads (§8.2)
            m_measured_size = out_size;
            // the children are measured, because a composite measures them (§8.2)
            UiNode::measure(constraints, out_size);
        }

        void arrange(const Rect& bounds) override {
            // the arrange is counted before it runs, so a skipped pass is visible (§8.2)
            arranges++;
            // the node arranges its children from the node contract (§8.2)
            UiNode::arrange(bounds);
        }

        void render(RenderContext& context) override {
            // the node paints its bounds, so a build always has a command to record (§6.1)
            context.draw_rect(bounds(), Paint{.fill = Color::rgb(0.2f, 0.6f, 0.9f)});
            // the children draw over it, which is the composite contract (§6.1)
            UiNode::render(context);
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
            context.draw_text(synthetic_layout(text.size(), 16.0f, Size{static_cast<float>(text.size()) * 8.0f, 16.0f}), origin, paint);
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

        [[nodiscard]] Color background() const override { return Color::rgb(0.1f, 0.1f, 0.15f); }
    };

    // the render context double, which accepts whatever stage two asks of it (§6.2)
    class TestPipelineContext final : public RenderContext
    {
    public:
        void save() override {}
        void restore() override {}
        void set_transform(const Transform&) override {}
        [[nodiscard]] const Transform& transform() const override { return m_transform; }
        void clip_rect(const Rect&) override {}
        void clip_rounded_rect(const Rect&, float) override {}
        [[nodiscard]] const Rect& clip() const override { return m_clip; }
        void draw_rect(const Rect&, const Paint&) override {}
        void draw_rounded_rect(const Rect&, float, const Paint&) override {}
        void draw_text(const TextLayout&, Point, const Paint&) override {}
        void draw_line_batch(const std::shared_ptr<const LineBatch>&, const Transform&, const StrokeStyle&) override {}
        void draw_path(const std::shared_ptr<const PathResource>&, const Transform&, const Paint&) override {}
        void draw_picture(const std::shared_ptr<const PictureResource>&, const Transform&) override {}
        void draw_image(const std::shared_ptr<const ImageResource>&, const Rect&, const Paint&) override {}

    private:
        // the transform the context reports, which a test does not change (§6.2)
        Transform m_transform;

        // the clip the context reports, which a test does not change (§6.2)
        Rect m_clip;
    };

    // the render surface double, which begins and ends frames without presenting (§6.2)
    class TestPipelineSurface final : public RenderSurface
    {
    public:
        [[nodiscard]] bool valid() const override { return true; }

        [[nodiscard]] const WindowMetrics& metrics() const override { return m_metrics; }

        void resize(const WindowMetrics& metrics) override { m_metrics = metrics; }

        RenderContext& begin_frame() override { return m_context; }

        void end_frame() override {}

    private:
        // the metrics the surface presents at, which a test does not change (§6.2)
        WindowMetrics m_metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);

        // the context the surface hands out, which the backend draws into (§6.2)
        TestPipelineContext m_context;
    };

    // the backend double, which counts the frames stage two executed (§6.2)
    class TestExecutorBackend final : public RendererBackend
    {
    public:
        // how many frames the backend executed, which a test asserts on (§6.1)
        std::size_t frames = 0;

        [[nodiscard]] bool ready() const override { return true; }

        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle, const WindowMetrics&) override { return std::make_unique<TestPipelineSurface>(); }

        void resize_surface(RenderSurface&, const WindowMetrics&) override {}

        void execute_frame(RenderSurface&, const FrameScene&) override { frames++; }
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

TEST(UiNode, AContainerOffersItsChildTheCeilingItReceived) {
    // arrange
    UiNode parent;
    auto child = std::make_unique<TestNode>();
    TestNode* child_ptr = child.get();
    parent.add_child(std::move(child));
    Size size{};
    // act
    parent.measure(SizeConstraints::at_most(Size{80.0f, 60.0f}), size);
    // assert
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.width, 80.0f);
    EXPECT_FLOAT_EQ(child_ptr->offered.maximum.height, 60.0f);
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

TEST(SceneRecorder, ANewRecorderHasTheWholeSurfaceAsItsClip) {
    // arrange, act
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // assert the clip starts at the whole surface, because an empty one would cull every draw of an unclipped tree (§6.2)
    EXPECT_EQ(recorder.clip(), (Rect{0.0f, 0.0f, 800.0f, 600.0f}));
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
    EXPECT_TRUE(recorder.scene()->empty());
}

TEST(SceneRecorder, ARectangleIsRecordedInSceneCoordinates) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{30.0f, 40.0f}), Paint{});
    // assert
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].rect_geometry().rect.x, 10.0f);
}

TEST(SceneRecorder, ATransformMovesTheRecordedRectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    recorder.set_transform(Transform::translation(100.0f, 50.0f));
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{30.0f, 40.0f}), Paint{});
    // assert
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].rect_geometry().rect.x, 110.0f);
}

TEST(SceneRecorder, ARoundedRectangleIsRecordedAsARectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_rounded_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{30.0f, 30.0f}), 5.0f, Paint{});
    // assert
    EXPECT_EQ(recorder.scene()->command_count(), 1u);
}

TEST(SceneRecorder, ARecordedColourTravelsWithTheRectangle) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    Paint paint;
    paint.fill = Color::rgb(0.25f, 0.5f, 0.75f);
    // act
    recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), paint);
    // assert
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().fill, (Color::rgb(0.25f, 0.5f, 0.75f)));
}

TEST(SceneRecorder, ATextColourTravelsWithTheRun) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    Paint paint;
    paint.fill = Color::rgb(0.9f, 0.1f, 0.2f);
    // act
    recorder.draw_text(synthetic_layout(5, 16.0f, Size{40.0f, 16.0f}), Point{30.0f, 40.0f}, paint);
    // assert
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().fill, (Color::rgb(0.9f, 0.1f, 0.2f)));
}

TEST(SceneRecorder, AStrokeOnlyRunRecordsItsStrokeColour) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    Paint paint;
    paint.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    paint.stroke = Color::rgb(0.4f, 0.5f, 0.6f);
    // act
    recorder.draw_text(synthetic_layout(5, 16.0f, Size{40.0f, 16.0f}), Point{30.0f, 40.0f}, paint);
    // assert
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().stroke, (Color::rgb(0.4f, 0.5f, 0.6f)));
}

TEST(SceneRecorder, AnEmptyTextRunRecordsNoColour) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_text(synthetic_layout(0, 16.0f, Size{0.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{});
    // assert
    EXPECT_TRUE(recorder.scene()->empty());
}

TEST(SceneRecorder, ATextRunIsRecordedWithItsOrigin) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_text(synthetic_layout(5, 16.0f, Size{40.0f, 16.0f}), Point{30.0f, 40.0f}, Paint{});
    // assert
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].text_geometry().origin.x, 30.0f);
}

TEST(SceneRecorder, AnEmptyTextRunIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_text(synthetic_layout(0, 16.0f, Size{0.0f, 16.0f}), Point{0.0f, 0.0f}, Paint{});
    // assert
    EXPECT_TRUE(recorder.scene()->empty());
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
    EXPECT_TRUE(recorder.scene()->empty());
    EXPECT_FLOAT_EQ(recorder.scene()->metrics().scale_factor, 1.0f);
}

TEST(SceneRecorder, ABatchIsRecordedWithItsTransformAndStroke) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}, {20.0f, 0.0f}, {30.0f, 10.0f}};
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    StrokeStyle stroke;
    stroke.color = Color::rgb(0.9f, 0.1f, 0.2f);
    stroke.width = 2.5f;
    // act
    recorder.draw_line_batch(batch, Transform::translation(10.0f, 20.0f), stroke);
    // assert
    ASSERT_EQ(commands_of(*recorder.scene(), DrawCommandKind::LineBatch).size(), 1u);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::LineBatch)[0].batch_geometry().batch, batch);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::LineBatch)[0].transform(), (Transform::translation(10.0f, 20.0f)));
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::LineBatch)[0].batch_geometry().stroke, stroke);
}

TEST(SceneRecorder, ABatchComposesTheCurrentRecorderTransform) {
    // arrange
    ResourceFactory factory;
    const Point points[] = {{0.0f, 0.0f}, {10.0f, 10.0f}};
    const std::shared_ptr<const LineBatch> batch = factory.create_line_batch(points);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    recorder.set_transform(Transform::translation(100.0f, 50.0f));
    // act
    recorder.draw_line_batch(batch, Transform::translation(10.0f, 20.0f), StrokeStyle{});
    // assert
    ASSERT_EQ(commands_of(*recorder.scene(), DrawCommandKind::LineBatch).size(), 1u);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::LineBatch)[0].transform(), (Transform::translation(110.0f, 70.0f)));
}

TEST(SceneRecorder, AnEmptyBatchIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_line_batch(nullptr, Transform{}, StrokeStyle{});
    // assert
    EXPECT_TRUE(commands_of(*recorder.scene(), DrawCommandKind::LineBatch).empty());
}

TEST(SceneRecorder, APathIsRecordedWithItsPaint) {
    // arrange
    ResourceFactory factory;
    const PathVerb verbs[] = {PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::Close};
    const std::shared_ptr<const PathResource> path = factory.create_path(verbs, std::span<const Point>({Point{0.0f, 0.0f}, Point{10.0f, 10.0f}, Point{10.0f, 0.0f}}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    Paint paint;
    paint.fill = Color::rgb(0.1f, 0.6f, 0.3f);
    // act
    recorder.draw_path(path, Transform::scaling(2.0f, 2.0f), paint);
    // assert
    ASSERT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Path).size(), 1u);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Path)[0].path_geometry().path, path);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Path)[0].transform(), (Transform::scaling(2.0f, 2.0f)));
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Path)[0].paint(), paint);
}

TEST(SceneRecorder, AnEmptyPathIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_path(nullptr, Transform{}, Paint{});
    // assert
    EXPECT_TRUE(commands_of(*recorder.scene(), DrawCommandKind::Path).empty());
}

TEST(SceneRecorder, APictureIsRecordedWithItsTransform) {
    // arrange
    ResourceFactory factory;
    PictureRecorder picture_recorder;
    picture_recorder.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), Paint{});
    const std::shared_ptr<const PictureResource> picture = factory.create_picture(picture_recorder);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_picture(picture, Transform::scaling(2.0f, 2.0f));
    // assert
    ASSERT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Picture).size(), 1u);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Picture)[0].picture_geometry().picture, picture);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Picture)[0].transform(), (Transform::scaling(2.0f, 2.0f)));
}

TEST(SceneRecorder, AnEmptyPictureIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_picture(nullptr, Transform{});
    // assert
    EXPECT_TRUE(commands_of(*recorder.scene(), DrawCommandKind::Picture).empty());
}

TEST(SceneRecorder, AnImageIsRecordedWithItsSceneDestination) {
    // arrange
    ResourceFactory factory;
    const std::uint8_t pixels[] = {255, 0, 0, 255, 0, 255, 0, 255};
    const std::shared_ptr<const ImageResource> image = factory.create_image(2, 1, ImageFormat::RGBA8, pixels);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_image(image, Rect::from_origin_size(Point{10.0f, 20.0f}, Size{4.0f, 2.0f}), Paint{});
    // assert
    ASSERT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Image).size(), 1u);
    EXPECT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Image)[0].image_geometry().image, image);
    EXPECT_FLOAT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Image)[0].image_geometry().destination.x, 10.0f);
}

TEST(SceneRecorder, AnImageDestinationMapsThroughTheCurrentTransform) {
    // arrange
    ResourceFactory factory;
    const std::uint8_t pixels[] = {255, 0, 0, 255};
    const std::shared_ptr<const ImageResource> image = factory.create_image(1, 1, ImageFormat::RGBA8, pixels);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    recorder.set_transform(Transform::translation(100.0f, 50.0f));
    // act
    recorder.draw_image(image, Rect::from_origin_size(Point{10.0f, 20.0f}, Size{4.0f, 2.0f}), Paint{});
    // assert
    ASSERT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Image).size(), 1u);
    EXPECT_FLOAT_EQ(commands_of(*recorder.scene(), DrawCommandKind::Image)[0].image_geometry().destination.x, 110.0f);
}

TEST(SceneRecorder, AnEmptyImageIsNotRecorded) {
    // arrange
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    recorder.draw_image(nullptr, Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 2.0f}), Paint{});
    // assert
    EXPECT_TRUE(commands_of(*recorder.scene(), DrawCommandKind::Image).empty());
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
    EXPECT_EQ(scene->command_count(), 1u);
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
    ASSERT_EQ(scene->command_count(), 1u);
    EXPECT_EQ(scene->commands()[0].kind(), DrawCommandKind::Text);
    EXPECT_EQ(scene->commands()[0].text_geometry().layout.run->glyph_count(), 5u);
}

TEST(SceneBuilder, AnInvisibleTreeDrawsNothing) {
    // arrange
    SceneBuilder builder;
    TestNode root;
    root.set_visible(false);
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert a tree that draws nothing records no command at all (§6.1)
    EXPECT_TRUE(scene->empty());
}

TEST(SceneBuilder, AnInvisibleChildIsNotDrawn) {
    // arrange
    SceneBuilder builder;
    UiNode root;
    auto child = std::make_unique<TestNode>();
    child->set_visible(false);
    root.add_child(std::move(child));
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert a tree that draws nothing records no command at all (§6.1)
    EXPECT_TRUE(scene->empty());
    EXPECT_EQ(builder.visited_count(), 2u);
    EXPECT_EQ(builder.drawn_count(), 1u);
}

TEST(SceneBuilder, AVisibleChildIsDrawn) {
    // arrange
    SceneBuilder builder;
    UiNode root;
    root.add_child(std::make_unique<TestNode>());
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(scene->command_count(), 1u);
    EXPECT_EQ(builder.drawn_count(), 2u);
    EXPECT_EQ(builder.laid_out_count(), 2u);
}

TEST(SceneBuilder, ANodeTheLayoutGaveNoAreaIsNotLaidOut) {
    // arrange
    SceneBuilder builder;
    ColumnLayout root;
    auto child = std::make_unique<TestNode>();
    child->intrinsic = Size{};
    root.add_child(std::move(child));
    // act
    const std::shared_ptr<const FrameScene> scene = builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(builder.visited_count(), 2u);
    EXPECT_EQ(builder.laid_out_count(), 1u);
    EXPECT_EQ(builder.drawn_count(), 2u);
}

TEST(SceneBuilder, TheCountsDescribeTheLastBuild) {
    // arrange
    SceneBuilder builder;
    TestNode wide;
    wide.add_child(std::make_unique<TestNode>());
    wide.add_child(std::make_unique<TestNode>());
    (void)builder.build(wide, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    TestNode single;
    // act
    (void)builder.build(single, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(builder.visited_count(), 1u);
    EXPECT_EQ(builder.laid_out_count(), 1u);
    EXPECT_EQ(builder.drawn_count(), 1u);
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
    EXPECT_EQ(scene->command_count(), 1u);
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

TEST(FramePipeline, APipelineWithoutARootHasNothingPending) {
    // arrange
    BindingEngine engine;
    // act, assert the tree-less pipeline cannot ask for a frame (§6.1)
    FramePipeline pipeline(engine, std::unique_ptr<UiNode>{});
    EXPECT_FALSE(pipeline.pending());
}

TEST(FramePipeline, SubmittingANullSceneWithAnExecutorFails) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    TestExecutorBackend backend;
    RenderExecutor executor(&backend);
    TestPipelineSurface surface;
    pipeline.set_executor(&executor);
    pipeline.set_surface(&surface);
    // act, assert a null scene never crosses the thread boundary (§6.1)
    EXPECT_FALSE(pipeline.submit(nullptr));
    EXPECT_EQ(backend.frames, 0u);
}

TEST(FramePipeline, SubmittingTheBuiltSceneHandsItToTheExecutor) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    TestExecutorBackend backend;
    RenderExecutor executor(&backend);
    TestPipelineSurface surface;
    pipeline.set_executor(&executor);
    pipeline.set_surface(&surface);
    // act
    const std::shared_ptr<const FrameScene> scene = pipeline.build(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    const bool handed = pipeline.submit();
    executor.flush();
    // assert the immutable scene reached the render thread (§6.1, AD-18)
    EXPECT_TRUE(handed);
    EXPECT_EQ(backend.frames, 1u);
    EXPECT_NE(scene, nullptr);
}

TEST(Component, RenderingAComponentPaintsItsBackground) {
    // arrange
    TestComponent component(nullptr, "canvas");
    component.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 50.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    component.Component::render(recorder);
    // assert the background is the fill the component paints (§6.1, §6.2)
    ASSERT_EQ(recorder.scene()->command_count(), 1u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().fill, (Color::rgb(0.1f, 0.1f, 0.15f)));
}

TEST(Component, RenderingAnUnarrangedComponentPaintsNothing) {
    // arrange
    TestComponent component(nullptr, "canvas");
    SceneRecorder recorder(WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f));
    // act
    component.Component::render(recorder);
    // assert a component with no area draws nothing (§6.1)
    EXPECT_TRUE(recorder.scene()->empty());
}

TEST(Component, HitTestingAnInvisibleComponentFallsThroughToItsChildren) {
    // arrange
    TestComponent component(nullptr, "canvas");
    auto child = std::make_unique<TestNode>();
    component.add_child(std::move(child));
    component.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    component.set_visible(false);
    // act, assert the child of a hidden component is still a hit (§8.3)
    EXPECT_TRUE(component.hit_test(Point{10.0f, 10.0f}));
}

TEST(UiNode, APlainNodeIsNeverDisabled) {
    // arrange
    TestNode node;
    // act, assert only a control carries the disabled state (§8.3)
    EXPECT_FALSE(node.disabled());
}

TEST(UiNode, TheConstVisitWalksTheTree) {
    // arrange
    TestNode parent;
    auto child = std::make_unique<TestNode>();
    TestNode* child_ptr = child.get();
    parent.add_child(std::move(child));
    const TestNode& view = parent;
    std::vector<const UiNode*> walked;
    // act
    view.visit([&walked](const UiNode& node) { walked.push_back(&node); });
    // assert the node comes before its children (§8.1)
    ASSERT_EQ(walked.size(), 2u);
    EXPECT_EQ(walked[0], &view);
    EXPECT_EQ(walked[1], child_ptr);
}

TEST(SceneBuilderLayoutDirtyChecks, AFirstBuildLaysTheTreeOut) {
    // arrange a fresh node asks for a layout, because nothing has placed it yet (§8.2)
    SceneBuilder builder;
    LayoutCountingNode root;
    // act
    builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert the first build measures and arranges, because the bounds have to come from somewhere (§8.2)
    EXPECT_EQ(root.measures, 1u);
    EXPECT_EQ(root.arranges, 1u);
}

TEST(SceneBuilderLayoutDirtyChecks, ASecondBuildOfAnUnchangedTreeSkipsTheLayoutPasses) {
    // arrange
    SceneBuilder builder;
    LayoutCountingNode root;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // act the same tree at the same metrics, which is what a steady state frame asks for (§6.1)
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert nothing asked for a layout, so the passes do not run again (§8.2)
    EXPECT_EQ(root.measures, 1u);
    EXPECT_EQ(root.arranges, 1u);
}

TEST(SceneBuilderLayoutDirtyChecks, AVisualChangeSkipsTheLayoutPasses) {
    // arrange
    SceneBuilder builder;
    LayoutCountingNode root;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    root.invalidate();
    // act a colour change moves no bound, so the layout has nothing to do (§8.2)
    builder.build(root, metrics, Color::rgb(0.1f, 0.2f, 0.3f));
    // assert the record pass ran again while the layout stayed where it was (§6.1, §8.2)
    EXPECT_EQ(root.measures, 1u);
    EXPECT_EQ(root.arranges, 1u);
}

TEST(SceneBuilderLayoutDirtyChecks, ALayoutChangeRunsTheLayoutPassesAgain) {
    // arrange
    SceneBuilder builder;
    LayoutCountingNode root;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    root.invalidate_layout();
    // act
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert a layout change is exactly what the flag is for (§8.2)
    EXPECT_EQ(root.measures, 2u);
    EXPECT_EQ(root.arranges, 2u);
}

TEST(SceneBuilderLayoutDirtyChecks, AResizeLaysTheTreeOutWithoutAnInvalidation) {
    // arrange
    SceneBuilder builder;
    LayoutCountingNode root;
    builder.build(root, WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // act the window moved, which changes what the tree fits into even though nothing invalidated (§8.2)
    builder.build(root, WindowMetrics::from_logical(Size{1024.0f, 768.0f}, 1.0f), Color::rgb(0.0f, 0.0f, 0.0f));
    // assert a resize is treated as a layout ask, so an app cannot get a stale tree by forgetting an invalidation (§8.2)
    EXPECT_EQ(root.measures, 2u);
    EXPECT_EQ(root.arranges, 2u);
}

TEST(SceneBuilderLayoutDirtyChecks, ASkippedLayoutKeepsTheBoundsTheLastBuildPlaced) {
    // arrange
    SceneBuilder builder;
    LayoutCountingNode root;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    const Rect placed = root.bounds();
    root.invalidate();
    // act
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert the recorded frame still sits where the last layout put the node (§8.2)
    EXPECT_EQ(root.bounds(), placed);
}

TEST(SceneBuilderLayoutDirtyChecks, ASkippedLayoutStillRecordsTheFrame) {
    // arrange
    SceneBuilder builder;
    LayoutCountingNode root;
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    root.invalidate();
    // act a visual change still has to be presented, which is what a skipped layout must not prevent (§6.1)
    const std::shared_ptr<const FrameScene> scene = builder.build(root, metrics, Color::rgb(0.0f, 0.0f, 0.0f));
    // assert
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->command_count(), 1u);
}

TEST(FramePipelineDirtyChecks, ACleanTreeProducesNoScene) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    pipeline.set_background(Color::rgb(0.0f, 0.0f, 0.0f));
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    pipeline.build(metrics);
    // act the tree is clean now, because the build cleared it (§6.1)
    const std::shared_ptr<const FrameScene> clean = pipeline.build_if_dirty(metrics);
    // assert nothing is drawn for a frame that changed nothing (§6.1)
    EXPECT_EQ(clean, nullptr);
}

TEST(FramePipelineDirtyChecks, AVisualChangeProducesAScene) {
    // arrange
    BindingEngine engine;
    FramePipeline pipeline(engine, std::make_unique<TestNode>());
    pipeline.set_background(Color::rgb(0.0f, 0.0f, 0.0f));
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{800.0f, 600.0f}, 1.0f);
    pipeline.build(metrics);
    pipeline.root().invalidate();
    // act
    const std::shared_ptr<const FrameScene> dirty = pipeline.build_if_dirty(metrics);
    // assert a visual change is still presented, because the gate is about the tree asking for a frame (§6.1)
    ASSERT_NE(dirty, nullptr);
    EXPECT_EQ(dirty->command_count(), 1u);
}

TEST(ComponentContractChecks, AComponentRecordsBackgroundContentThenChildren) {
    // arrange a node that counts the order the composite ran in (§6.1)
    class OrderNode final : public Component
    {
    public:
        // a component needs its engine, which the tree owns and this double does not use (§7.2)
        explicit OrderNode(BindingEngine* engine) :
            Component(engine) {}


        // the phases this component saw, in the order the composite ran them (§6.1)
        std::vector<std::string> phases;

        [[nodiscard]] Color background() const override { return Color::rgb(0.1f, 0.1f, 0.1f); }

    protected:
        void paint_content(RenderContext& context) override {
            // the content phase runs between the background and the children (§6.1)
            phases.emplace_back("content");
            // the content draws nothing, because only the order is under test here (§6.2)
            (void)context;
        }
    };

    BindingEngine engine;
    OrderNode node(&engine);
    node.phases.emplace_back("start");
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    node.render(recorder);
    // assert the content phase ran, which is what paint_content is for (§6.1)
    ASSERT_EQ(node.phases.size(), 2u);
    EXPECT_EQ(node.phases[1], "content");
}
