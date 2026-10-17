#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "commands/command.h"
#include "core/constraints.h"
#include "core/scrollable_content.h"
#include "runtime/binding_engine.h"
#include "semantics/semantic_node.h"
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

        void set_opacity(float opacity) override { m_opacity = opacity; }

        [[nodiscard]] float opacity() const override { return m_opacity; }

        void draw_rect(const Rect& rect, const Paint&) override { rects.push_back(rect); }

        void draw_shadow(const Rect&, float, const Shadow&) override {}

        void draw_rounded_rect(const Rect& rect, float, const Paint&) override {
            // a rounded draw lands in both lists, because a thumb is a rounded rectangle (§6.1)
            rounded.push_back(rect);
            rects.push_back(rect);
        }

        void draw_polyline(const std::span<const Point>&, const StrokeStyle&) override {}

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

        // the opacity the last set wrote, which a test asserts against (§6.1)
        float m_opacity = 1.0f;
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

    // the content double whose extent is arithmetic over the row height it was given (§8.2, AD-13)
    class HeightDrivenContent final : public UiNode, public ScrollableContent
    {
    public:
        // how many items the content holds, which is what the row height multiplies (§8.2, AD-13)
        std::size_t items = 100;

        // the row height the viewport last applied, which the extent is computed over (§8.2, AD-13)
        float row_height = 0.0f;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the content is as tall as its items are rows, which is what a virtualized list measures (§8.2)
            out_size = constraints.constrain(content_extent());
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }

        [[nodiscard]] Size content_extent() const override {
            // the extent is arithmetic over the height the viewport applied, so a stale height is visible (§8.2, AD-13)
            return Size{m_measured_size.width, static_cast<float>(items) * row_height};
        }

        void set_row_extent(float row_extent) override { row_height = row_extent; }

        void set_scroll_viewport(const ScrollViewport&) override {}
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

    // the content double that reports the rows it built, which is what a list overlays its states onto (§8.2, AD-13)
    class RowReportingContent final : public UiNode, public ScrollableContent
    {
    public:
        // the extent the whole content would take if every row were built (§8.2)
        Size total{200.0f, 800.0f};

        // the rows the content has built right now, which a viewport reads by position (§8.2, AD-13)
        std::vector<std::unique_ptr<UiNode>> rows;

        // the item index the first built row shows, which the window can start past (§8.2, AD-13)
        std::size_t first = 0;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the reported extent wins, because only the content knows the rows it did not build (§8.2)
            out_size = constraints.constrain(total);
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }

        [[nodiscard]] Size content_extent() const override { return total; }

        void set_row_extent(float) override {}

        void set_scroll_viewport(const ScrollViewport&) override {}

        [[nodiscard]] std::size_t materialized_row_count() const override { return rows.size(); }

        [[nodiscard]] std::size_t first_row_index() const override { return first; }

        [[nodiscard]] UiNode* row_node(std::size_t position) override { return position < rows.size() ? rows.at(position).get() : nullptr; }
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

TEST(ScrollViewChecks, AChangedRowHeightReachesTheContentExtent) {
    // arrange a content whose extent is arithmetic over the height it was given (§8.2, AD-13)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<HeightDrivenContent>();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act the height is bound and settles twice, as a form resize does (§7.3, AD-13)
    view.set_row_height(20.0f);
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the hundred rows are two hundred tall at twenty each (§8.2, AD-13)
    ASSERT_FLOAT_EQ(view.content_extent().height, 2000.0f);
    // act the height settles again, which is what a later binding does (§7.3, AD-13)
    view.set_row_height(40.0f);
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the cached extent is the one the new height produces, which is the whole contract (§8.2)
    EXPECT_FLOAT_EQ(view.content_extent().height, 4000.0f);
    // assert the scroll range followed, because that is what a thumb is measured against (§8.2)
    EXPECT_FLOAT_EQ(view.maximum_offset(), 3700.0f);
}

TEST(ScrollViewChecks, ARowHeightSettlesTheContentExtentInTheSameFrame) {
    // arrange a viewport whose content extent is arithmetic over the row height (§8.2, AD-13)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<HeightDrivenContent>();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_row_height(20.0f);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the hundred rows are two hundred tall at twenty each (§8.2, AD-13)
    ASSERT_FLOAT_EQ(view.content_extent().height, 2000.0f);
    ASSERT_FLOAT_EQ(view.maximum_offset(), 1700.0f);
    // act a bound row height settles again, which is what a form resize does (§7.3, AD-13)
    view.set_row_height(40.0f);
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the extent followed inside the measure that cached it, because arrange comes too late (§8.2, AD-13)
    EXPECT_FLOAT_EQ(view.content_extent().height, 4000.0f);
    // assert the scroll range followed with it, because that is what maximum_offset reads (§8.2)
    EXPECT_FLOAT_EQ(view.maximum_offset(), 3700.0f);
}

TEST(ScrollViewChecks, AnUnchangedRowHeightDoesNotReachTheContent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<HeightDrivenContent>();
    HeightDrivenContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_row_height(20.0f);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    content_ptr->row_height = 0.0f;
    // act the height is written again with the value it already holds (§8.2, AD-13)
    view.set_row_height(20.0f);
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert an unchanged height reaches the content anyway, because the measure is where the extent is read (§8.2)
    EXPECT_FLOAT_EQ(content_ptr->row_height, 20.0f);
    EXPECT_FLOAT_EQ(view.content_extent().height, 2000.0f);
}

TEST(ScrollViewChecks, AnUnarrangedViewClipsNothing) {
    // arrange a view that was never placed, so it has no viewport to clip to
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    RecordingContext context;
    // act
    view.render(context);
    // assert a viewport with no area clips nothing, because there is nothing to scissor to (§6.2)
    EXPECT_TRUE(context.clips.empty());
}

TEST(ScrollViewChecks, AHiddenViewTakesNoEventAndIsNotAHit) {
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
    view.set_visible(false);
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 40.0f, .precision = ScrollPrecision::Point};
    // act
    view.dispatch_event(wheel);
    // assert a hidden view draws nothing to aim at, so it neither claims nor hits (§8.3)
    EXPECT_FALSE(wheel.handled);
    EXPECT_FALSE(view.hit_test(Point{10.0f, 10.0f}));
}

TEST(ScrollViewChecks, ADisabledViewRefusesTheWheelTheKeysAndTheHitTest) {
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
    view.set_disabled(true);
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 40.0f, .precision = ScrollPrecision::Point};
    Event page{.kind = EventKind::KeyDown, .key = Key::PageDown};
    // act
    view.dispatch_event(wheel);
    view.dispatch_event(page);
    // assert a disabled viewport refuses input, so it claims none of it (§8.3)
    EXPECT_FALSE(wheel.handled);
    EXPECT_FALSE(page.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
    EXPECT_FALSE(view.hit_test(Point{10.0f, 10.0f}));
}

TEST(ScrollViewChecks, AModifiedKeyIsLeftToTheApplication) {
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
    Event shortcut{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    shortcut.modifiers.command = true;
    // act
    view.dispatch_event(shortcut);
    // assert a modified key belongs to the application, which the standards reserve first (§8.3)
    EXPECT_FALSE(shortcut.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, AKeyTheViewportDoesNotAnswerIsLeftAlone) {
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
    Event letter{.kind = EventKind::KeyDown, .key = Key::A};
    // act
    view.dispatch_event(letter);
    // assert only the paging keys are the viewport's business (§8.3)
    EXPECT_FALSE(letter.handled);
}

TEST(ScrollViewChecks, AnArrowKeyMovesOneLine) {
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
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    view.dispatch_event(down);
    // assert a line key moves one line, which is the themed distance a notch moves (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 40.0f);
    // act the other arrow moves the same distance the other way
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    view.dispatch_event(up);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, APageUpKeyMovesBackOneViewport) {
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
    view.scroll_to(500.0f);
    // act
    Event page_up{.kind = EventKind::KeyDown, .key = Key::PageUp};
    view.dispatch_event(page_up);
    // assert the paging key is named for the viewport it moves (§8.3)
    EXPECT_TRUE(page_up.handled);
    EXPECT_FLOAT_EQ(view.offset(), 200.0f);
}

TEST(ScrollViewChecks, DraggingTheThumbMovesTheOffset) {
    // arrange a viewport whose thumb sits at the leading edge (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act the press lands on the thumb, which is the hundred points at the trailing edge (§6.1)
    Event press{.kind = EventKind::PointerDown, .position = Point{195.0f, 50.0f}};
    view.dispatch_event(press);
    // assert the drag begins, because only the thumb is draggable (§8.3)
    EXPECT_TRUE(press.handled);
    // act the pointer moves to the trailing end of the travel
    Event move{.kind = EventKind::PointerMove, .position = Point{195.0f, 250.0f}};
    view.dispatch_event(move);
    // assert the offset follows the thumb, which is the whole of what the bar reports (§8.3)
    EXPECT_TRUE(move.handled);
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    // act the release ends the drag, leaving the offset where it was put
    Event release{.kind = EventKind::PointerUp, .position = Point{195.0f, 250.0f}};
    view.dispatch_event(release);
    EXPECT_TRUE(release.handled);
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
}

TEST(ScrollViewChecks, APressOnTheGrooveOrOutsideTheBarIsNotAClaim) {
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
    // act a press on the groove below the thumb, which has no draggable part (§8.3)
    Event groove{.kind = EventKind::PointerDown, .position = Point{195.0f, 200.0f}};
    view.dispatch_event(groove);
    // act a press past the trailing edge, which is not this viewport at all (§8.3)
    Event outside{.kind = EventKind::PointerDown, .position = Point{400.0f, 50.0f}};
    view.dispatch_event(outside);
    // assert neither is claimed, because only the thumb drags (§8.3)
    EXPECT_FALSE(groove.handled);
    EXPECT_FALSE(outside.handled);
}

TEST(ScrollViewChecks, APressInARowBesideTheThumbIsNotAScrollbarDrag) {
    // arrange a viewport whose thumb spans the top of the trailing strip (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act a press lands at the thumb's height but well inside the viewport, where a row sits (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{50.0f, 50.0f}};
    view.dispatch_event(press);
    // assert the press is not claimed, because only the trailing strip drags the bar (§8.3)
    EXPECT_FALSE(press.handled);
    // act a move after the press must not scroll, because no drag began (§8.3)
    Event move{.kind = EventKind::PointerMove, .position = Point{50.0f, 250.0f}};
    view.dispatch_event(move);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, ADragOnAViewportThatCannotScrollMovesNothing) {
    // arrange a viewport whose content fits, so there is no thumb to drag (§8.2)
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
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{195.0f, 50.0f}};
    view.dispatch_event(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{195.0f, 250.0f}};
    view.dispatch_event(move);
    // assert nothing moved, because a viewport with no travel has no draggable thumb (§8.2)
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, TheAccessorsReadBackWhatWasWritten) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    view.add_child(std::move(content));
    // act, assert every written value has a reader, because the codegen path reads them back (§11.3)
    view.set_viewport_extent(Size{320.0f, 240.0f});
    EXPECT_EQ(view.viewport_extent(), (Size{320.0f, 240.0f}));
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    EXPECT_EQ(view.scrollbar_visibility(), ScrollbarVisibility::AlwaysOn);
    view.set_overscan(48.0f);
    EXPECT_FLOAT_EQ(view.overscan(), 48.0f);
    view.set_row_height(24.0f);
    EXPECT_FLOAT_EQ(view.row_height(), 24.0f);
}

TEST(ScrollViewChecks, AnUnwrittenChangeChangesNothing) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{320.0f, 240.0f});
    view.set_overscan(48.0f);
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    view.set_disabled(true);
    view.set_row_height(24.0f);
    view.clear_dirty();
    // act every setter is given the value it already holds (§8.2, §8.3)
    view.set_viewport_extent(Size{320.0f, 240.0f});
    view.set_overscan(48.0f);
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    view.set_disabled(true);
    view.set_row_height(24.0f);
    // assert nothing asked for a frame, because nothing about the viewport changed (§8.2)
    EXPECT_FALSE(view.render_dirty());
    EXPECT_FALSE(view.layout_dirty());
}

TEST(ScrollViewChecks, AnUnwatchedScrollViewReportsNoHandler) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    // act, assert the handler is absent until one is installed, which is what the signal is (§8.3)
    EXPECT_FALSE(view.watches());
    view.set_on_scroll([](float) {});
    EXPECT_TRUE(view.watches());
}

TEST(ScrollViewChecks, TheOffsetSetterIsTheSameWriteAUserScrollMakes) {
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
    // act the setter writes through the same clamped path a user scroll takes (§7.3, AD-24)
    view.set_offset(100000.0f);
    // assert
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    EXPECT_TRUE(view.user_scrolled());
    // act an unchanged destination writes nothing, which is what the guard is for (§8.2)
    view.clear_dirty();
    view.set_offset(600.0f);
    EXPECT_FALSE(view.render_dirty());
}

TEST(ScrollViewChecks, AThemeResolvesTheScrollbarPaintAndTheLineStep) {
    // arrange a viewport whose bar is drawn through an installed theme (§6.1)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(14.0f);
    theme.set_scrollbar_thumb_minimum(30.0f);
    theme.set_scroll_line_extent(25.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    RecordingContext context;
    // act
    view.render(context);
    // assert the track spans the viewport height at the themed width, against the trailing edge (§6.1)
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.height == 300.0f && rect.width == 14.0f)
            track = rect;
    EXPECT_FLOAT_EQ(track.x, 186.0f);
    // assert the thumb is the band narrower than the viewport, because it is the draggable part (§6.1)
    Rect thumb{};
    for (const Rect& rect : context.rounded)
        if (rect.height < 300.0f)
            thumb = rect;
    // assert the thumb takes the viewport share of the content, which is what the ratio means (§8.2)
    EXPECT_FLOAT_EQ(thumb.height, 100.0f);
    // act the themed line step moves a key press, because the policy is the theme's (§8.3)
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    view.dispatch_event(down);
    EXPECT_FLOAT_EQ(view.offset(), 25.0f);
}

TEST(ScrollViewChecks, AThemedActionScrollsByTheThemedStep) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scroll_line_extent(15.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act the assistive technology action moves down by the themed line (§10.1)
    const SemanticNode node = view.semantic_node();
    EXPECT_TRUE(node.invoke("scroll_up"));
    EXPECT_TRUE(node.invoke("scroll_down"));
    // assert the two steps cancel, because the themed step is the same either way (§10.1)
    EXPECT_FLOAT_EQ(view.offset(), 15.0f);
}

TEST(ScrollViewChecks, ADisabledPressOnTheBarIsNotAClaim) {
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
    view.set_disabled(true);
    Event press{.kind = EventKind::PointerDown, .position = Point{195.0f, 50.0f}};
    // act
    view.dispatch_event(press);
    // assert a disabled viewport claims no press, so no ancestor reacts to one either (§8.3)
    EXPECT_FALSE(press.handled);
}

TEST(ScrollViewChecks, ABoundCommandOwnsTheDisabledState) {
    // arrange a viewport bound to a command, which is what makes one applicable (§18.7)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Command command{"log.scroll", "Log"};
    command.set_enabled(false);
    view.set_command(&command);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act a write the command does not own is recorded but not applied, because the command is the answer (§18.7)
    view.set_disabled(true);
    EXPECT_TRUE(view.disabled());
    // assert the command is the single answer, whatever the widget's own flag says (§18.7)
    view.set_disabled(false);
    EXPECT_TRUE(view.disabled());
    // act the command becomes applicable again
    command.set_enabled(true);
    EXPECT_FALSE(view.disabled());
}

TEST(ScrollViewChecks, APlainContentIsToldNoRowHeight) {
    // arrange a scroll view over a container that does not build itself (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_row_height(20.0f);
    Size measured{};
    // act, assert a container with no window still measures as its own extent (§8.2)
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    EXPECT_FLOAT_EQ(view.content_extent().height, 900.0f);
}

TEST(ScrollViewChecks, AnUnplacedViewportDrawsNoBar) {
    // arrange a viewport measured but placed with no area (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{0.0f, 0.0f}));
    RecordingContext context;
    // act
    view.render(context);
    // assert no band, because a viewport with no area has nowhere to put one (§6.2)
    for (const Rect& rect : context.rects)
        EXPECT_NE(rect.width, 10.0f);
    for (const Rect& rect : context.rounded)
        EXPECT_NE(rect.width, 10.0f);
}

TEST(ScrollViewChecks, AViewportThatFitsDrawsNoBarUnderTheAutomaticPolicy) {
    // arrange content that fits, so there is no travel to show (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 200.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    RecordingContext context;
    // act, assert the automatic policy draws nothing, because a bar would advertise a travel that is not there (§8)
    view.render(context);
    for (const Rect& rect : context.rects)
        EXPECT_NE(rect.width, 10.0f);
    for (const Rect& rect : context.rounded)
        EXPECT_NE(rect.width, 10.0f);
}

TEST(ScrollViewChecks, AForcedBarOverFittingContentDrawsATrackWithoutAThumb) {
    // arrange the always-on policy over content that fits (§8)
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
    RecordingContext context;
    // act
    view.render(context);
    // assert the track is drawn at the trailing edge, because the policy insists on a bar (§8)
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.width == 10.0f)
            track = rect;
    EXPECT_FLOAT_EQ(track.x, 190.0f);
    EXPECT_FLOAT_EQ(track.height, 300.0f);
    // assert the thumb is left off, because a viewport with no travel has no position to reach (§6.2)
    for (const Rect& rect : context.rounded)
        EXPECT_NE(rect.width, 10.0f);
}

TEST(ScrollViewChecks, AThumbAsTallAsItsViewportDoesNotDrag) {
    // arrange a themed floor that swallows the whole track, so no travel is left (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_thumb_minimum(400.0f);
    theme.set_scrollbar_extent(14.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act the press lands on the thumb and the pointer moves along it
    Event press{.kind = EventKind::PointerDown, .position = Point{193.0f, 100.0f}};
    view.dispatch_event(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{193.0f, 260.0f}};
    view.dispatch_event(move);
    // assert the drag moves nothing, because the thumb already fills the track (§8.2)
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, TheViewportReportsTheRowsItsContentBuilt) {
    // arrange a content that built two rows into a window starting past the first item (§8.2, AD-13)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<RowReportingContent>();
    RowReportingContent* content_ptr = content.get();
    content_ptr->rows.push_back(std::make_unique<UiNode>());
    content_ptr->rows.push_back(std::make_unique<UiNode>());
    content_ptr->first = 5;
    view.add_child(std::move(content));
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the built window and its first item are reported, because a keyed child is addressed by index (§18.1, AD-13)
    EXPECT_EQ(view.materialized_row_count(), 2U);
    EXPECT_EQ(view.first_row_index(), 5U);
    EXPECT_EQ(view.row_node(0), content_ptr->rows.at(0).get());
    EXPECT_EQ(view.row_node(1), content_ptr->rows.at(1).get());
    // assert a position past the built rows names nothing, which ends a lazy walk (§10.1)
    EXPECT_EQ(view.row_node(2), nullptr);
}

TEST(ScrollViewChecks, AViewWithPlainContentReportsNoRows) {
    // arrange a content that is not scrollable, so it declares no row window at all (§8.2, AD-13)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    // act, assert a plain content reports no rows, because only a scrollable content enumerates them (§8.2, AD-13)
    EXPECT_EQ(view.materialized_row_count(), 0U);
    EXPECT_EQ(view.first_row_index(), 0U);
    EXPECT_EQ(view.row_node(0), nullptr);
}

TEST(ScrollViewChecks, AViewWithNoContentReportsNoRows) {
    // arrange a view that was never given a child, so there is no content to ask (§8.2, AD-13)
    BindingEngine engine;
    ScrollView view(&engine);
    // act, assert every passthrough is quiet, because the null content is guarded rather than assumed (§8.2, AD-13)
    EXPECT_EQ(view.materialized_row_count(), 0U);
    EXPECT_EQ(view.first_row_index(), 0U);
    EXPECT_EQ(view.row_node(0), nullptr);
}
