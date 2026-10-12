#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "core/constraints.h"
#include "core/scrollable_content.h"
#include "runtime/binding_engine.h"
#include "semantics/semantic_node.h"
#include "ui/component.h"
#include "ui/ui_event.h"
#include "ui/ui_node.h"
#include "widgets/scroll_view.h"

using namespace albor;

namespace
{
    // the context double that records what a scroll view asked to draw, so a test reads the clip off the command (§6.2)
    class RecordingContext final : public RenderContext
    {
    public:
        // every axis aligned clip the surface was asked to adopt, in order (§6.2)
        std::vector<Rect> clips;

        // every rectangle the surface was asked to draw, in order (§6.1)
        std::vector<Rect> rects;

        void save() override {}

        void restore() override {}

        void set_transform(const Transform& transform) override { m_transform = transform; }

        [[nodiscard]] const Transform& transform() const override { return m_transform; }

        void clip_rect(const Rect& rect) override { clips.push_back(rect); }

        void clip_rounded_rect(const Rect& rect, float) override {
            clips.push_back(rect);
            rounded.push_back(rect);
        }

        [[nodiscard]] const Rect& clip() const override { return m_clip; }

        void draw_rect(const Rect& rect, const Paint&) override { rects.push_back(rect); }

        void draw_rounded_rect(const Rect& rect, float, const Paint&) override {
            // a rounded draw lands in both lists, because a thumb is a rounded rectangle (§6.1)
            rounded.push_back(rect);
            rects.push_back(rect);
        }

        void draw_text(const TextLayout&, Point, const Paint&) override {}

        void draw_line_batch(const std::shared_ptr<const LineBatch>&, const Transform&, const StrokeStyle&) override {}

        void draw_path(const std::shared_ptr<const PathResource>&, const Transform&, const Paint&) override {}

        void draw_picture(const std::shared_ptr<const PictureResource>&, const Transform&) override {}

        void draw_image(const std::shared_ptr<const ImageResource>&, const Rect&, const Paint&) override {}

        // the rounded rectangles the surface was asked to adopt, which is how a test finds the thumb (§6.1)
        std::vector<Rect> rounded;

        // the transform the surface stands at (§6.2)
        Transform m_transform;

        // the clip the surface reads back (§6.2)
        Rect m_clip;
    };

    // the content double that reports the extent it was built with, which is what a scroll view measures (§8.2)
    class ContentNode final : public UiNode
    {
    public:
        // the extent the content claims, which a test sets to model content taller than the viewport (§8.2)
        Size intrinsic{0.0f, 0.0f};

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the content takes the width it is offered and the height it declares, because a scrolled column does (§8.2)
            out_size = constraints.constrain(Size{.width = constraints.maximum.width, .height = intrinsic.height});
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }

        void render(RenderContext& context) override {
            // the content paints its own bounds, so a test can read where the scroll put it (§6.1)
            context.draw_rect(bounds(), {.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
        }
    };

    // the content double that builds itself against a window, which is the virtualizing contract (§8.2, AD-13)
    class WindowedContent final : public UiNode, public ScrollableContent
    {
    public:
        // the extent the whole content would take if all of it were built (§8.2)
        Size total{0.0f, 0.0f};

        // the row height the viewport pushed, which a test reads back (§8.2, AD-13)
        float row_height = 0.0f;

        // the window the viewport last pushed, which a test reads back (§8.2)
        ScrollViewport window;

        // how many times the window was pushed, which proves the reconcile ran (§8.2)
        std::size_t pushes = 0;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the built rows stand in for the whole content, so the measured height is the window it covers (§8.2)
            out_size = constraints.constrain(total);
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }

        [[nodiscard]] Size content_extent() const override { return total; }

        void set_row_extent(float row_extent) override { row_height = row_extent; }

        void set_scroll_viewport(const ScrollViewport& viewport) override {
            window = viewport;
            pushes++;
        }
    };
} // namespace

TEST(ScrollViewChecks, TheContentIsMeasuredWithoutAHeightCeiling) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the content keeps its natural height, because that height is what makes a scrollbar appear (§8.2)
    EXPECT_FLOAT_EQ(content_ptr->measured_size().height, 900.0f);
}

TEST(ScrollViewChecks, TheViewportIsWhatTheViewMeasures) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert a scroll container never grows with the content behind it, or it could not scroll (§8.2)
    EXPECT_FLOAT_EQ(measured.height, 300.0f);
    EXPECT_FLOAT_EQ(measured.width, 200.0f);
}

TEST(ScrollViewChecks, ContentThatFitsHasNowhereToScroll) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 200.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act, assert
    EXPECT_FLOAT_EQ(view.maximum_offset(), 0.0f);
    EXPECT_FALSE(view.scrollable());
}

TEST(ScrollViewChecks, TheContentSitsAtTheOffset) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    // act
    view.scroll_to(100.0f);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the offset is the whole of what scrolling moves, so the content sits a hundred above the viewport (§8.2)
    EXPECT_FLOAT_EQ(content_ptr->bounds().y, -100.0f);
    EXPECT_FLOAT_EQ(content_ptr->bounds().height, 900.0f);
}

TEST(ScrollViewChecks, ATrackpadWheelMovesByItsOwnDelta) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 25.0f, .precision = ScrollPrecision::Point};
    // act
    view.dispatch_event(wheel);
    // assert a trackpad already counts points, so the delta is the distance and no second momentum is applied (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 25.0f);
    EXPECT_TRUE(wheel.handled);
}

TEST(ScrollViewChecks, ANotchedWheelMovesByTheThemedLineExtent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(wheel);
    // assert the container owns the conversion, because inheriting the platform policy a second time is the defect (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 40.0f);
}

TEST(ScrollViewChecks, TheOffsetIsClampedToTheContent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act
    view.scroll_to(100000.0f);
    // assert the range is the content extent less the viewport, so a fling cannot scroll past the end (§8.2)
    EXPECT_FLOAT_EQ(view.maximum_offset(), 600.0f);
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    // act a wheel past the leading edge
    view.scroll_by(-100000.0f);
    // assert
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, TheBarFollowsTheAsNeededPolicy) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act, assert the content exceeds the viewport, so the bar appears (§8)
    EXPECT_TRUE(view.scrollbar_visible());
}

TEST(ScrollViewChecks, TheBarFollowsTheAlwaysOffPolicyWhileTheContentStillScrolls) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act
    view.scroll_by(40.0f);
    // assert the bar is gone but the content moved, which is what a log view that pins itself needs (§8)
    EXPECT_FALSE(view.scrollbar_visible());
    EXPECT_FLOAT_EQ(view.offset(), 40.0f);
}

TEST(ScrollViewChecks, TheBarFollowsTheAlwaysOnPolicyEvenWhenTheContentFits) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 200.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act, assert the bar is drawn even though nothing overflows, because the policy asked for it (§8)
    EXPECT_TRUE(view.scrollbar_visible());
}

TEST(ScrollViewChecks, TheContentIsClippedToTheViewport) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    const Rect bounds = Rect::from_origin_size(Point{10.0f, 20.0f}, Size{200.0f, 300.0f});
    view.arrange(bounds);
    RecordingContext context;
    // act
    view.render(context);
    // assert the viewport is the clip, which is what keeps scrolled content out of the surface (§6.2)
    ASSERT_EQ(context.clips.size(), 1u);
    EXPECT_EQ(context.clips.at(0), bounds);
}

TEST(ScrollViewChecks, APageKeyMovesAWholeViewport) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event page{.kind = EventKind::KeyDown, .key = Key::PageDown};
    // act
    view.dispatch_event(page);
    // assert a page key is named for the viewport it moves, which is the difference from a line key (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 300.0f);
    EXPECT_TRUE(page.handled);
}

TEST(ScrollViewChecks, TheEndKeyGoesToTheFurthestOffset) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    // act
    view.dispatch_event(end);
    // assert
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    // act the home key returns to the leading edge
    Event home{.kind = EventKind::KeyDown, .key = Key::Home};
    view.dispatch_event(home);
    // assert
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, TheViewportPushesItsWindowToScrollableContent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WindowedContent>();
    content->total = Size{200.0f, 900.0f};
    WindowedContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_overscan(60.0f);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    // act
    view.scroll_to(120.0f);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the window carries the viewport, the content extent, the offset and the overscan (§8.2, AD-13)
    EXPECT_EQ(content_ptr->pushes, 1u);
    EXPECT_FLOAT_EQ(content_ptr->window.viewport.height, 300.0f);
    EXPECT_FLOAT_EQ(content_ptr->window.content.height, 900.0f);
    EXPECT_FLOAT_EQ(content_ptr->window.offset, 120.0f);
    EXPECT_FLOAT_EQ(content_ptr->window.overscan, 60.0f);
}

TEST(ScrollViewChecks, ASemanticsActionScrollsTheContent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_explicit_id("log");
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act
    const SemanticNode node = view.semantic_node();
    const bool ran = node.invoke("scroll_down");
    // assert the node announces a viewport that assistive technology can drive (§10.1)
    EXPECT_TRUE(ran);
    EXPECT_FLOAT_EQ(view.offset(), 40.0f);
    EXPECT_EQ(node.role(), SemanticRole::ScrollView);
}
TEST(ScrollViewChecks, AUserScrollDetachesTheBindingUntilTheContentChanges) {
    // arrange the binding over a context that reports where a pinned log view wants to sit (§7.3)
    BindingEngine engine;
    ScrollView view(&engine);
    struct Pin
    {
        // the offset the expression produces, which stands for content extent less viewport extent (§7.3)
        float target = 600.0f;
    };
    Pin pin;
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.bind_offset(
        [](void* context) {
            // the evaluator reads its own context, which is what keeps the binding allocation free (§13.3)
            return static_cast<Pin*>(context)->target;
        },
        &pin, {});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    (void)engine.settle();
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the binding owns the offset, which is what pins a log view to the bottom (§7.3, AD-24)
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    EXPECT_FALSE(view.user_scrolled());
    // act the user scrolls away from the pinned end
    view.scroll_by(-500.0f);
    // assert the user takes precedence over the binding, without either losing its place (§7.3, AD-24)
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
    EXPECT_TRUE(view.user_scrolled());
    // act the content grows, which is what a log view appending a line does
    pin.target = 900.0f;
    content_ptr->intrinsic = Size{100.0f, 1200.0f};
    (void)engine.settle();
    view.measure(SizeConstraints::unconstrained(), measured);
    (void)engine.settle();
    // assert the binding speaks again, because a changed content is what re-attaches it (§7.3, AD-24)
    EXPECT_FLOAT_EQ(view.offset(), 900.0f);
    EXPECT_FALSE(view.user_scrolled());
}

TEST(ScrollViewChecks, AScrollReportsEveryChangeToItsHandler) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    std::vector<float> reported;
    view.set_on_scroll([&reported](float offset) {
        // the handler receives the offset the viewport moved to, which is what the signal reports (§8.3)
        reported.push_back(offset);
    });
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act
    view.scroll_by(40.0f);
    view.scroll_by(40.0f);
    // assert an unchanged destination reports nothing, because a move that did not happen is not a change (§8.3)
    ASSERT_EQ(reported.size(), 2u);
    EXPECT_FLOAT_EQ(reported.at(0), 40.0f);
    EXPECT_FLOAT_EQ(reported.at(1), 80.0f);
}

TEST(ScrollViewChecks, NoActionIsOfferedThatTakesNoTarget) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_explicit_id("log");
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act
    const SemanticNode node = view.semantic_node();
    // assert a step action is offered, and an absolute jump is not, because a semantic action carries no target (§10.1)
    EXPECT_TRUE(node.has_action("scroll_down"));
    EXPECT_TRUE(node.has_action("scroll_up"));
    EXPECT_FALSE(node.has_action("scroll_to"));
}

TEST(ScrollViewChecks, AChangedRowHeightReachesTheContent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WindowedContent>();
    content->total = Size{200.0f, 2000.0f};
    WindowedContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act the row height is bound and settles twice, as a binding does (§7.3, AD-13)
    view.set_row_height(20.0f);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.set_row_height(40.0f);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the content is told twice, because the container owns the height the rows divide by (§8.2, AD-13)
    EXPECT_FLOAT_EQ(content_ptr->row_height, 40.0f);
}
