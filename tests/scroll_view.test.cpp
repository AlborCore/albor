#include <cmath>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "albor/commands/command.h"
#include "albor/core/constraints.h"
#include "albor/core/scrollable_content.h"
#include "albor/input/input_dispatcher.h"
#include "albor/layout/column_layout.h"
#include "albor/layout/row_layout.h"
#include "albor/layout/stack_layout.h"
#include "albor/runtime/binding_engine.h"
#include "albor/semantics/semantic_node.h"
#include "albor/style/theme.h"
#include "albor/style/theme_builder.h"
#include "albor/ui/ui_event.h"
#include "albor/ui/ui_node.h"
#include "albor/widgets/scroll_view.h"

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

        void push_layer(float) override {}

        void pop_layer() override {}

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

    // the leaf double that keeps the extent it was built with, which is what makes a column shrink-wrap (§8.2)
    class IntrinsicNode final : public UiNode
    {
    public:
        // the natural extent the leaf reports, which a test sets to model a row narrower than its page (§8.2)
        Size intrinsic{0.0f, 0.0f};

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the leaf takes only what it asked for, because a row without a stretch declaration keeps its extent (§8.2)
            out_size = constraints.constrain(intrinsic);
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
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

    // the content double that records every width it is measured against, which is what the gutter probe reads (§8.2)
    class WidthRecordingContent final : public UiNode, public ScrollableContent
    {
    public:
        // the width each measure offered, in call order, which proves how many passes ran (§8.2)
        std::vector<float> widths;

        // the height the content claims, which is what decides whether the bar is needed (§8.2)
        float height = 900.0f;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the offered ceiling is recorded before it is applied, because that is the width the probe named (§8.2)
            widths.push_back(constraints.maximum.width);
            // the content takes the width it was offered and the height it claims, which is what a page does (§8.2)
            out_size = constraints.constrain(Size{.width = constraints.maximum.width, .height = height});
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }

        [[nodiscard]] Size content_extent() const override { return Size{m_measured_size.width, height}; }

        void set_row_extent(float) override {}

        void set_scroll_viewport(const ScrollViewport&) override {}
    };

    // the content double whose height follows the width, which is what a narrowed remeasure exposes (§8.2)
    class WidthDrivenContent final : public UiNode, public ScrollableContent
    {
    public:
        // the width each measure offered, in call order, which proves how many passes ran (§8.2)
        std::vector<float> widths;

        // the width the slope is measured against, which models an aspect ratio (§8.2)
        float reference_width = 200.0f;

        // the height at the reference width, which is what the content claims before narrowing (§8.2)
        float base_height = 900.0f;

        // the height each missing pixel changes by, which can grow or shrink the narrowed extent (§8.2)
        float slope = 2.0f;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the offered ceiling is recorded before it is applied, because that is the width the probe named (§8.2)
            widths.push_back(constraints.maximum.width);
            // the height follows the width, so a narrowed second pass reports a different extent (§8.2)
            m_height = base_height + (reference_width - constraints.maximum.width) * slope;
            // the content takes the width it was offered and the height the slope produced (§8.2)
            out_size = constraints.constrain(Size{.width = constraints.maximum.width, .height = m_height});
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        }

        [[nodiscard]] Size content_extent() const override { return Size{m_measured_size.width, m_height}; }

        void set_row_extent(float) override {}

        void set_scroll_viewport(const ScrollViewport&) override {}

    private:
        // the height the last measure produced, which is what the extent reports (§8.2)
        float m_height = 0.0f;
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

TEST(ScrollViewChecks, AnUndeclaredWidthTakesTheOfferedWidth) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // assert the declared height wins while the undeclared width keeps the offer, and the bar takes its gutter (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
    EXPECT_FLOAT_EQ(measured.height, 300.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 490.0f);
}

TEST(ScrollViewChecks, AnUndeclaredWidthFillsTheSpaceARowLeavesIt) {
    // arrange a fixed width sidebar and a page whose width is left to the row (§8.2)
    BindingEngine engine;
    RowLayout row;
    auto sidebar = std::make_unique<ScrollView>(&engine);
    sidebar->set_viewport_extent(Size{120.0f, 40.0f});
    auto page = std::make_unique<ScrollView>(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{0.0f, 900.0f};
    page->add_child(std::move(content));
    page->set_horizontal_align(Align::Stretch);
    ScrollView* page_ptr = page.get();
    row.add_child(std::move(sidebar));
    row.add_child(std::move(page));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert the page measures and sits in exactly the width the sidebar left (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
    EXPECT_FLOAT_EQ(page_ptr->measured_size().width, 380.0f);
    EXPECT_FLOAT_EQ(page_ptr->bounds().x, 120.0f);
    EXPECT_FLOAT_EQ(page_ptr->bounds().width, 380.0f);
}

TEST(ScrollViewChecks, AColumnOfNarrowRowsFillsTheViewportWidth) {
    // arrange a page whose rows are narrower than the viewport, which is what a column of cards reports (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    IntrinsicNode* row_ptr = row.get();
    column->add_child(std::move(row));
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert the page fills the cross axis, because the content of a vertical scroll defaults to stretch (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, view.content_viewport().width);
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 500.0f);
    // assert the row stretches with the page, because the column resolves the same cross axis default (§8.2)
    EXPECT_FLOAT_EQ(row_ptr->bounds().width, 500.0f);
    // assert the measure keeps the natural extent, because the stretch is a placement and not a new measurement (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->measured_size().width, 120.0f);
    EXPECT_FLOAT_EQ(view.content_extent().width, 120.0f);
}

TEST(ScrollViewChecks, AStretchedContentFillsOnlyTheContentViewportBesideAGutter) {
    // arrange a bar that reserves a gutter, because the content is taller than the viewport (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{100.0f, 900.0f};
    column->add_child(std::move(row));
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the stretch stops at the gutter, because a reserved bar is not content area (§8.2)
    ASSERT_TRUE(view.scrollbar_visible());
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 190.0f);
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, view.content_viewport().width);
    EXPECT_FLOAT_EQ(column_ptr->bounds().x, 0.0f);
}

TEST(ScrollViewChecks, AStartAlignedContentKeepsItsMeasuredWidth) {
    // arrange a page that asks to keep its measured width on the cross axis (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    column->set_horizontal_align(Align::Start);
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert an authored alignment outranks the cross axis default, so the width is the measured one (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 120.0f);
}

TEST(ScrollViewChecks, ACenterAlignedContentSitsInTheMiddleOfAGutterViewport) {
    // arrange a page placed at an origin and narrowed by a gutter, whose centre is measured from both (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    column->set_horizontal_align(Align::Center);
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{40.0f, 20.0f}, Size{200.0f, 300.0f}));
    // assert the leftover width is split inside the content viewport, which is the origin plus the gutter (§8.2)
    EXPECT_FLOAT_EQ(view.content_viewport().x, 40.0f);
    EXPECT_FLOAT_EQ(column_ptr->bounds().x, 75.0f);
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 120.0f);
}

TEST(ScrollViewChecks, AEndAlignedContentMeetsTheTrailingEdgeOfAGutterViewport) {
    // arrange a page placed at an origin and narrowed by a gutter, whose far edge is the sum of both (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    column->set_horizontal_align(Align::End);
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{40.0f, 20.0f}, Size{200.0f, 300.0f}));
    // assert the content keeps its measured width and meets the trailing edge of the content viewport (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->bounds().x, 110.0f);
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 120.0f);
}

TEST(ScrollViewChecks, AnExplicitStretchFillsTheViewportWidth) {
    // arrange a page that authors the default, which must resolve to the same cross axis placement (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    column->set_horizontal_align(Align::Stretch);
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert the authored stretch takes the content viewport rather than the measured width (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 500.0f);
}

TEST(ScrollViewChecks, AnAlignedContentKeepsTheScrollOffset) {
    // arrange a centred page that is scrolled, because the cross axis must not disturb the offset (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    column->set_horizontal_align(Align::Center);
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // act
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    view.scroll_to(100.0f);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert the alignment moved only x, because the offset owns the place on the scroll axis (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->bounds().x, 190.0f);
    EXPECT_FLOAT_EQ(column_ptr->bounds().y, -100.0f);
}

TEST(ScrollViewChecks, AVirtualizedContentWithANarrowExtentFillsTheViewportWidth) {
    // arrange a virtualized content whose reported extent is narrower than the viewport (§8.2, AD-13)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WindowedContent>();
    content->total = Size{100.0f, 900.0f};
    WindowedContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the reported extent does not hold the content at its natural width, because the viewport places it (§8.2)
    EXPECT_FLOAT_EQ(view.content_extent().width, 100.0f);
    EXPECT_FLOAT_EQ(content_ptr->bounds().width, 200.0f);
}

TEST(ScrollViewChecks, AnAlignmentChangeRearrangesACleanContent) {
    // arrange a laid out and clean page whose alignment is about to change (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    view.clear_dirty();
    // act an authored alignment change must re-run both layout passes (§8.2)
    column_ptr->set_horizontal_align(Align::End);
    // assert the placement follows the new alignment, which an arrange only invalidation would miss (§8.2)
    EXPECT_TRUE(view.layout_dirty());
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    EXPECT_FLOAT_EQ(column_ptr->bounds().x, 380.0f);
}

TEST(ScrollViewChecks, APolicySwitchRearrangesTheCrossAxis) {
    // arrange a forced bar over a narrow page, so the content viewport is narrowed (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto row = std::make_unique<IntrinsicNode>();
    row->intrinsic = Size{120.0f, 900.0f};
    column->add_child(std::move(row));
    ColumnLayout* column_ptr = column.get();
    view.add_child(std::move(column));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    Size measured{};
    view.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 190.0f);
    // act a policy that leaves the whole width to the content (§8)
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    view.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the stretch follows the wider content viewport, because the gutter it kept is gone (§8.2)
    EXPECT_FLOAT_EQ(column_ptr->bounds().width, 200.0f);
}

TEST(ScrollViewChecks, AnUndeclaredHeightFillsTheSpaceAColumnLeavesIt) {
    // arrange a stacked heading above a page whose height is left to the column (§8.2)
    BindingEngine engine;
    ColumnLayout column;
    auto heading = std::make_unique<ScrollView>(&engine);
    heading->set_viewport_extent(Size{200.0f, 100.0f});
    auto page = std::make_unique<ScrollView>(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{0.0f, 900.0f};
    page->add_child(std::move(content));
    page->set_vertical_align(Align::Stretch);
    ScrollView* page_ptr = page.get();
    column.add_child(std::move(heading));
    column.add_child(std::move(page));
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{200.0f, 400.0f}), measured);
    column.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 400.0f}));
    // assert the page measures and sits in exactly the height the heading left (§8.2)
    EXPECT_FLOAT_EQ(measured.height, 400.0f);
    EXPECT_FLOAT_EQ(page_ptr->measured_size().height, 300.0f);
    EXPECT_FLOAT_EQ(page_ptr->bounds().y, 100.0f);
    EXPECT_FLOAT_EQ(page_ptr->bounds().height, 300.0f);
}

TEST(ScrollViewChecks, AnActionRowBelowAFlexiblePageKeepsItsHeight) {
    // arrange the issue's dialog shape, where the page scrolls and the actions sit below it (§8.2)
    BindingEngine engine;
    ColumnLayout column;
    auto body = std::make_unique<RowLayout>();
    body->set_vertical_align(Align::Stretch);
    auto sidebar = std::make_unique<ScrollView>(&engine);
    sidebar->set_viewport_extent(Size{120.0f, 40.0f});
    auto page = std::make_unique<ScrollView>(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{0.0f, 900.0f};
    page->add_child(std::move(content));
    page->set_horizontal_align(Align::Stretch);
    body->add_child(std::move(sidebar));
    body->add_child(std::move(page));
    RowLayout* body_ptr = body.get();
    auto footer = std::make_unique<ScrollView>(&engine);
    footer->set_viewport_extent(Size{100.0f, 32.0f});
    ScrollView* footer_ptr = footer.get();
    column.add_child(std::move(body));
    column.add_child(std::move(footer));
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    column.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert the flexible body shrinks to leave the action row its height (§8.2)
    EXPECT_FLOAT_EQ(body_ptr->measured_size().height, 368.0f);
    EXPECT_FLOAT_EQ(footer_ptr->measured_size().height, 32.0f);
    EXPECT_FLOAT_EQ(footer_ptr->bounds().y, 368.0f);
    EXPECT_FLOAT_EQ(footer_ptr->bounds().height, 32.0f);
}

TEST(ScrollViewChecks, AFlexibleScrollViewLeavesItsSiblingItsWidth) {
    // arrange the mirror order, where the page scrolls before a fixed action beside it (§8.2)
    BindingEngine engine;
    RowLayout row;
    row.set_gap(10.0f);
    auto page = std::make_unique<ScrollView>(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{0.0f, 900.0f};
    page->add_child(std::move(content));
    page->set_horizontal_align(Align::Stretch);
    auto action = std::make_unique<ScrollView>(&engine);
    action->set_viewport_extent(Size{120.0f, 40.0f});
    ScrollView* page_ptr = page.get();
    ScrollView* action_ptr = action.get();
    row.add_child(std::move(page));
    row.add_child(std::move(action));
    Size measured{};
    // act
    row.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    row.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{500.0f, 400.0f}));
    // assert the flexible page takes only the space the action and the gap left (§8.2)
    EXPECT_FLOAT_EQ(page_ptr->measured_size().width, 370.0f);
    EXPECT_FLOAT_EQ(action_ptr->measured_size().width, 120.0f);
    EXPECT_FLOAT_EQ(action_ptr->bounds().x, 380.0f);
    EXPECT_FLOAT_EQ(action_ptr->bounds().width, 120.0f);
}

TEST(ScrollViewChecks, AnUndeclaredHeightTakesTheOfferedHeight) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 0.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // assert the declared width wins while the undeclared height keeps the offer, and the bar takes its gutter (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 200.0f);
    EXPECT_FLOAT_EQ(measured.height, 400.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 190.0f);
}

TEST(ScrollViewChecks, AnUndeclaredViewportTakesTheWholeOffer) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{0.0f, 0.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // assert neither axis is declared, so the viewport is the whole offer and the bar takes its gutter (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
    EXPECT_FLOAT_EQ(measured.height, 400.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 490.0f);
}

TEST(ScrollViewChecks, ADeclaredViewportIsClampedIntoTheOffer) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{800.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // assert a declared axis never escapes the parent's ceiling, and the bar takes its gutter (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 500.0f);
    EXPECT_FLOAT_EQ(measured.height, 300.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 490.0f);
}

TEST(ScrollViewChecks, ADeclaredAxisBelowTheMinimumIsRaised) {
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
    view.measure(SizeConstraints{.minimum = Size{400.0f, 0.0f}, .preferred = {}, .maximum = Size{800.0f, 600.0f}}, measured);
    // assert the minimum raises the declared width while the declared height stands, and the bar takes its gutter (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 400.0f);
    EXPECT_FLOAT_EQ(measured.height, 300.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 390.0f);
}

TEST(ScrollViewChecks, AHeightOnlyViewportClipsAndScrollsTheContent) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    const Rect bounds = Rect::from_origin_size(Point{10.0f, 20.0f}, Size{500.0f, 300.0f});
    // act
    view.arrange(bounds);
    RecordingContext context;
    view.render(context);
    // assert the content takes the resolved width less the bar gutter and the viewport clips and scrolls (§8.2)
    EXPECT_FLOAT_EQ(content_ptr->bounds().width, 490.0f);
    EXPECT_FLOAT_EQ(content_ptr->bounds().height, 900.0f);
    EXPECT_TRUE(view.scrollable());
    EXPECT_FLOAT_EQ(view.maximum_offset(), 600.0f);
    ASSERT_FALSE(context.clips.empty());
    EXPECT_EQ(context.clips.front(), Rect::from_origin_size(Point{10.0f, 20.0f}, Size{490.0f, 300.0f}));
}

TEST(ScrollViewChecks, ADeclaredHeightWinsOverTheArrangedBounds) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // act
    view.arrange(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{500.0f, 400.0f}));
    // assert the declared height survives a parent that assigns a taller rectangle (§8.2)
    EXPECT_EQ(view.bounds(), (Rect::from_origin_size(Point{10.0f, 20.0f}, Size{500.0f, 300.0f})));
    EXPECT_FLOAT_EQ(view.maximum_offset(), 600.0f);
    EXPECT_TRUE(view.scrollable());
}

TEST(ScrollViewChecks, ADeclaredWidthWinsOverTheArrangedBounds) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 0.0f});
    Size measured{};
    view.measure(SizeConstraints::at_most(Size{500.0f, 400.0f}), measured);
    // act
    view.arrange(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{500.0f, 400.0f}));
    // assert the declared width survives a parent that assigns a wider rectangle, and the bar takes its gutter (§8.2)
    EXPECT_EQ(view.bounds(), (Rect::from_origin_size(Point{10.0f, 20.0f}, Size{200.0f, 400.0f})));
    EXPECT_FLOAT_EQ(content_ptr->bounds().width, 190.0f);
    EXPECT_FLOAT_EQ(view.maximum_offset(), 500.0f);
}

TEST(ScrollViewChecks, AnUndeclaredAxisUnderAnUnboundedOfferIsFinite) {
    // arrange
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{0.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert an unbounded parent falls back to its preferred extent instead of infinity (§8.2)
    EXPECT_TRUE(std::isfinite(measured.width));
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 300.0f);
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
    view.scroll_to(100.0f);
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 25.0f, .precision = ScrollPrecision::Point};
    // act an up notch, whose point delta is the distance, so no second momentum is applied (§8.3)
    view.dispatch_event(up);
    // assert positive turns toward the start, which is the direction the platform reports (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 75.0f);
    EXPECT_TRUE(up.handled);
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -25.0f, .precision = ScrollPrecision::Point};
    // act the matching down notch
    view.dispatch_event(down);
    // assert the other sign returns the viewport to the offset it started from (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
    EXPECT_TRUE(down.handled);
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
    view.scroll_to(100.0f);
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch, which is one themed line toward the start (§8.3)
    view.dispatch_event(up);
    // assert the container owns the conversion, because inheriting the platform policy a second time is the defect (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 60.0f);
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act the matching down notch
    view.dispatch_event(down);
    // assert the line extent applies to both directions, so only the sign changes (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
}

TEST(ScrollViewChecks, AnUpWheelNotchMovesTowardTheStart) {
    // arrange a viewport whose offset starts in the middle of its range (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.scroll_to(100.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(wheel);
    // assert an up notch moves the content toward its start, which is the native direction (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(view.offset(), 60.0f);
}

TEST(ScrollViewChecks, ADownWheelNotchMovesTowardTheEnd) {
    // arrange a viewport whose offset starts in the middle of its range (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.scroll_to(100.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(wheel);
    // assert a down notch moves the content toward its end, which mirrors the up notch (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(view.offset(), 140.0f);
}

TEST(ScrollViewChecks, AThemedLineExtentScalesTheWheelNotItsSign) {
    // arrange a themed viewport whose line extent is twenty five (§6.1)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scroll_line_extent(25.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.scroll_to(100.0f);
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(up);
    // assert the themed extent scales the distance toward the start, not the sign (§6.1, §8.3)
    EXPECT_FLOAT_EQ(view.offset(), 75.0f);
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(down);
    // assert the same themed extent applies the other way (§6.1, §8.3)
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
}

TEST(ScrollViewChecks, TheWheelClampsAtBothEnds) {
    // arrange a viewport at its leading edge (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch already at the start
    view.dispatch_event(up);
    // assert nothing moves and the notch is left for an ancestor, which is what lets nested viewports chain (§8.3)
    EXPECT_FALSE(up.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
    view.scroll_to(view.maximum_offset());
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act a down notch already at the end
    view.dispatch_event(down);
    // assert the offset stays at the maximum and the notch is left unclaimed (§8.2, §8.3)
    EXPECT_FALSE(down.handled);
    EXPECT_FLOAT_EQ(view.offset(), view.maximum_offset());
}

TEST(ScrollViewChecks, AWheelOverContentThatFitsIsLeftToTheParent) {
    // arrange content shorter than the viewport, so the viewport has no travel (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 200.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(up);
    // assert a wheel that cannot move the content is left for an ancestor (§8.3)
    EXPECT_FALSE(up.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act
    view.dispatch_event(down);
    // assert the other sign cannot claim a viewport that has no travel either (§8.2, §8.3)
    EXPECT_FALSE(down.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
}

TEST(ScrollViewChecks, APointWheelWithAFractionalDeltaMovesByExactlyThatManyPoints) {
    // arrange a viewport in the middle of its range (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.scroll_to(100.0f);
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 12.5f, .precision = ScrollPrecision::Point};
    // act
    view.dispatch_event(up);
    // assert a fractional point delta is applied without rounding, because the trackpad counts points (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 87.5f);
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -12.5f, .precision = ScrollPrecision::Point};
    // act
    view.dispatch_event(down);
    // assert the matching down delta returns to the starting offset exactly (§8.3)
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
}

TEST(ScrollViewChecks, AnInnerViewportThatFitsLeavesTheWheelToItsParent) {
    // arrange an outer viewport whose inner viewport has no travel of its own (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 100.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    ASSERT_FLOAT_EQ(inner_ptr->maximum_offset(), 0.0f);
    ASSERT_FLOAT_EQ(outer.maximum_offset(), 100.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act the wheel turns down over the inner viewport
    InputDispatcher::dispatch(outer, wheel);
    // assert the inner declines and the enclosing viewport takes the notch (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(outer.offset(), 40.0f);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 0.0f);
}

TEST(ScrollViewChecks, AnInnerViewportAtItsStartLeavesTheUpWheelToItsParent) {
    // arrange an inner viewport at its leading edge inside an outer viewport already scrolled (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    outer.scroll_to(50.0f);
    ASSERT_FLOAT_EQ(inner_ptr->offset(), 0.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch over the inner, which is already at its start
    InputDispatcher::dispatch(outer, wheel);
    // assert the inner declines and the outer moves toward its own start (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 0.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 10.0f);
}

TEST(ScrollViewChecks, AnInnerViewportAtItsEndLeavesTheDownWheelToItsParent) {
    // arrange an inner viewport at its trailing edge inside an outer viewport already scrolled (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    outer.scroll_to(50.0f);
    inner_ptr->scroll_to(inner_ptr->maximum_offset());
    ASSERT_FLOAT_EQ(inner_ptr->offset(), 500.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act a down notch over the inner, which is already at its end
    InputDispatcher::dispatch(outer, wheel);
    // assert the inner declines and the outer moves toward its own end (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 500.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 90.0f);
}

TEST(ScrollViewChecks, AnInnerViewportThatMovesConsumesTheWheel) {
    // arrange an inner viewport in the middle of its range inside a scrolled outer viewport (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    outer.scroll_to(50.0f);
    inner_ptr->scroll_to(100.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch the inner viewport can honour
    InputDispatcher::dispatch(outer, wheel);
    // assert the moving viewport consumes the notch whole and the outer keeps its place (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 60.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 50.0f);
}

TEST(ScrollViewChecks, ADisabledInnerViewportLeavesTheWheelToItsParent) {
    // arrange a disabled inner viewport inside a scrolled outer viewport (§8.3)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    outer.scroll_to(50.0f);
    inner_ptr->set_disabled(true);
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch over the disabled inner
    InputDispatcher::dispatch(outer, wheel);
    // assert the inner is not a hit and the outer takes the wheel (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 0.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 10.0f);
}

TEST(ScrollViewChecks, AWheelAtTheEdgeDoesNotDetachABoundOffset) {
    // arrange a binding that asks for more than the range holds (§7.3, AD-24)
    BindingEngine engine;
    ScrollView view(&engine);
    struct Pin
    {
        // the offset the expression produces, which stands for content extent less viewport extent (§7.3)
        float target = 1000.0f;
    };
    Pin pin;
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
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
    ASSERT_FLOAT_EQ(view.offset(), 600.0f);
    ASSERT_FALSE(view.user_scrolled());
    std::vector<float> reported;
    view.set_on_scroll([&reported](float offset) {
        // the handler records any write, which is what a no-op notch must not raise (§8.3)
        reported.push_back(offset);
    });
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act a down notch at the presented end, which the binding already overran
    view.dispatch_event(down);
    // assert the presented offset did not move, so the event is left alone and the binding still owns the value (§7.3)
    EXPECT_FALSE(down.handled);
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    EXPECT_FALSE(view.user_scrolled());
    EXPECT_TRUE(reported.empty());
}

TEST(ScrollViewChecks, AnOverrunBindingIsDetachedOnlyWhenTheOffsetMoves) {
    // arrange a binding that asks for more than the range holds (§7.3, AD-24)
    BindingEngine engine;
    ScrollView view(&engine);
    struct Pin
    {
        // the offset the expression produces, which stands for content extent less viewport extent (§7.3)
        float target = 1000.0f;
    };
    Pin pin;
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
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
    ASSERT_FLOAT_EQ(view.offset(), 600.0f);
    ASSERT_FALSE(view.user_scrolled());
    // act an imperative write naming the presented offset, which is a no-op
    view.set_offset(600.0f);
    // assert the binding still owns the value, because the presented offset did not change (§7.3)
    EXPECT_FALSE(view.user_scrolled());
    std::vector<float> reported;
    view.set_on_scroll([&reported](float offset) {
        // the handler records the value the moving notch reports (§8.3)
        reported.push_back(offset);
    });
    Event up{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch, which can move the presented offset
    view.dispatch_event(up);
    // assert the moving notch takes precedence over the binding and reports the new offset (§7.3, §8.3)
    EXPECT_TRUE(up.handled);
    EXPECT_FLOAT_EQ(view.offset(), 560.0f);
    EXPECT_TRUE(view.user_scrolled());
    ASSERT_EQ(reported.size(), 1u);
    EXPECT_FLOAT_EQ(reported.at(0), 560.0f);
}

TEST(ScrollViewChecks, AnInnerViewportConsumesTheWholeNotchAtItsEnd) {
    // arrange an inner viewport twenty points from its end inside a scrolled outer viewport (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    outer.scroll_to(50.0f);
    inner_ptr->scroll_to(480.0f);
    Event first{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act a notch that reaches the inner end after twenty points
    InputDispatcher::dispatch(outer, first);
    // assert the partial notch is consumed whole and the outer keeps its place (§8.3)
    EXPECT_TRUE(first.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 500.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 50.0f);
    Event second{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act the next notch, which the inner can no longer move
    InputDispatcher::dispatch(outer, second);
    // assert the next notch starts on the outer viewport (§8.3)
    EXPECT_TRUE(second.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 500.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 90.0f);
}

TEST(ScrollViewChecks, TheNearestMovableViewportClaimsTheWheel) {
    // arrange three viewports where the middle one is the nearest that can move (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto middle = std::make_unique<ScrollView>(&engine);
    middle->set_viewport_extent(Size{0.0f, 400.0f});
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 600.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 100.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    middle->add_child(std::move(inner));
    ScrollView* middle_ptr = middle.get();
    outer.add_child(std::move(middle));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    outer.scroll_to(50.0f);
    ASSERT_FLOAT_EQ(inner_ptr->maximum_offset(), 0.0f);
    Event first{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act a notch over the innermost viewport, which cannot move
    InputDispatcher::dispatch(outer, first);
    // assert the middle viewport takes the notch and the outer keeps its place (§8.3)
    EXPECT_TRUE(first.handled);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 0.0f);
    EXPECT_FLOAT_EQ(middle_ptr->offset(), 40.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 50.0f);
    middle_ptr->scroll_to(middle_ptr->maximum_offset());
    Event second{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act another notch once the middle viewport reached its end
    InputDispatcher::dispatch(outer, second);
    // assert the walk continues to the outer viewport (§8.3)
    EXPECT_TRUE(second.handled);
    EXPECT_FLOAT_EQ(middle_ptr->offset(), 200.0f);
    EXPECT_FLOAT_EQ(outer.offset(), 90.0f);
}

TEST(ScrollViewChecks, AZeroWheelIsLeftAlone) {
    // arrange a scrollable viewport in the middle of its range (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.scroll_to(100.0f);
    std::vector<float> reported;
    view.set_on_scroll([&reported](float offset) {
        // the handler records any write, which a zero delta must not raise (§8.3)
        reported.push_back(offset);
    });
    Event zero{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = 0.0f, .precision = ScrollPrecision::Line};
    // act a wheel event with no delta, which is what a horizontal gesture produces
    view.dispatch_event(zero);
    // assert the viewport neither moves nor claims the event (§8.3)
    EXPECT_FALSE(zero.handled);
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
    EXPECT_TRUE(reported.empty());
}

TEST(ScrollViewChecks, APointWheelChainsByItsOwnDelta) {
    // arrange an outer viewport whose inner viewport has no travel (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 100.0f};
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = -12.5f, .precision = ScrollPrecision::Point};
    // act a point delta over the inner viewport
    InputDispatcher::dispatch(outer, wheel);
    // assert the outer applies the point delta once, because the inner declined it (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(outer.offset(), 12.5f);
    EXPECT_FLOAT_EQ(inner_ptr->offset(), 0.0f);
}

TEST(ScrollViewChecks, ADeclinedWheelFallsThroughToALowerSibling) {
    // arrange two overlaid viewports where only the lower one can move toward the start (§8.2)
    BindingEngine engine;
    StackLayout stack;
    auto bottom = std::make_unique<ScrollView>(&engine);
    auto bottom_content = std::make_unique<ContentNode>();
    bottom_content->intrinsic = Size{100.0f, 900.0f};
    bottom->add_child(std::move(bottom_content));
    auto top = std::make_unique<ScrollView>(&engine);
    auto top_content = std::make_unique<ContentNode>();
    top_content->intrinsic = Size{100.0f, 900.0f};
    top->add_child(std::move(top_content));
    ScrollView* bottom_ptr = bottom.get();
    ScrollView* top_ptr = top.get();
    stack.add_child(std::move(bottom));
    stack.add_child(std::move(top));
    Size measured{};
    stack.measure(SizeConstraints::tight(Size{200.0f, 300.0f}), measured);
    stack.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    bottom_ptr->scroll_to(100.0f);
    Event wheel{.kind = EventKind::Wheel, .position = Point{5.0f, 5.0f}, .scroll = 1.0f, .precision = ScrollPrecision::Line};
    // act an up notch over the top viewport, which is already at its start
    InputDispatcher::dispatch(stack, wheel);
    // assert the declined event is offered to the lower sibling, as every positioned event is (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(top_ptr->offset(), 0.0f);
    EXPECT_FLOAT_EQ(bottom_ptr->offset(), 60.0f);
}

TEST(ScrollViewChecks, AnOutOfRangeOverrideRestoresWhenTheRangeGrows) {
    // arrange a user scroll at the end of a long range (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.set_offset(600.0f);
    ASSERT_TRUE(view.user_scrolled());
    // act the content shrinks so the presented offset is clamped below the stored one
    content_ptr->intrinsic = Size{100.0f, 400.0f};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    ASSERT_FLOAT_EQ(view.offset(), 100.0f);
    Event down{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -1.0f, .precision = ScrollPrecision::Line};
    // act a notch at the shrunken end, which cannot move the presented offset
    view.dispatch_event(down);
    EXPECT_FALSE(down.handled);
    EXPECT_FLOAT_EQ(view.offset(), 100.0f);
    // act the content grows back to its old extent
    content_ptr->intrinsic = Size{100.0f, 900.0f};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the stored user position is restored, which is why the guard preserves it (§7.3)
    EXPECT_FLOAT_EQ(view.offset(), 600.0f);
    EXPECT_TRUE(view.user_scrolled());
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

TEST(ScrollViewChecks, TheContentIsClippedToTheContentViewport) {
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
    // assert the content viewport is the clip, which keeps scrolled content out of the gutter and the surface (§6.2)
    ASSERT_EQ(context.clips.size(), 1u);
    EXPECT_EQ(context.clips.at(0), view.content_viewport());
    EXPECT_EQ(view.content_viewport(), (Rect::from_origin_size(Point{10.0f, 20.0f}, Size{190.0f, 300.0f})));
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
    EXPECT_TRUE(view.content_viewport().empty());
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
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -40.0f, .precision = ScrollPrecision::Point};
    // act
    view.dispatch_event(wheel);
    // assert a hidden view draws nothing to aim at, so it neither claims nor hits (§8.3)
    EXPECT_FALSE(wheel.handled);
    EXPECT_FLOAT_EQ(view.offset(), 0.0f);
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
    Event wheel{.kind = EventKind::Wheel, .position = Point{10.0f, 10.0f}, .scroll = -40.0f, .precision = ScrollPrecision::Point};
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

TEST(ScrollViewChecks, ACancelEndsAThumbDrag) {
    // arrange a viewport whose thumb is being dragged toward the end (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{195.0f, 50.0f}};
    view.dispatch_event(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{195.0f, 250.0f}};
    view.dispatch_event(move);
    ASSERT_FLOAT_EQ(view.offset(), 600.0f);
    Event cancel{.kind = EventKind::PointerCancel, .position = Point{195.0f, 250.0f}};
    // act
    view.dispatch_event(cancel);
    Event after{.kind = EventKind::PointerMove, .position = Point{195.0f, 50.0f}};
    view.dispatch_event(after);
    // assert the cancel ended the drag, so the next move scrolls nothing (§8.3)
    EXPECT_TRUE(cancel.handled);
    EXPECT_FALSE(after.handled);
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
    // assert the content was measured against the themed gutter (§6.1, §8.2)
    EXPECT_EQ(view.content_viewport().width, 186.0f);
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

TEST(ScrollViewChecks, AnOverlayBarReservesNoGutter) {
    // arrange a viewport whose theme paints an overlay bar, which is what a native scrollbar is (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(7.0f);
    theme.set_scrollbar_overlay(true);
    theme.set_scrollbar_overlay_inset(2.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    RecordingContext context;
    // act
    view.render(context);
    // assert the content keeps the whole viewport, because an overlay bar takes no gutter (§8.2)
    EXPECT_FLOAT_EQ(view.content_viewport().width, 200.0f);
    // assert the strip rests against the trailing edge less its standoff, which is where an overlay bar rides (§8.2)
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.height == 300.0f)
            track = rect;
    EXPECT_FLOAT_EQ(track.width, 7.0f);
    EXPECT_FLOAT_EQ(track.x, 191.0f);
}

TEST(ScrollViewChecks, AnOverlayBarAppearsOnlyWhileTheContentOverflows) {
    // arrange a viewport whose inset content fits, which is what an as needed bar has to leave alone (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 100.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(7.0f);
    theme.set_scrollbar_overlay(true);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert content that fits draws no bar, because the as needed policy still decides (§8)
    EXPECT_FALSE(view.scrollbar_visible());
    // act the content grows past the viewport, which is what asks for a bar (§8)
    content_ptr->intrinsic = Size{100.0f, 900.0f};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the overlay bar appears without shifting the content, because no gutter is reserved (§8.2)
    EXPECT_TRUE(view.scrollbar_visible());
    EXPECT_FLOAT_EQ(view.content_viewport().width, 200.0f);
}

TEST(ScrollViewChecks, AnOverlayBarMeasuresTheContentAtTheFullWidth) {
    // arrange an overlay viewport whose content would wrap narrower if a gutter were taken (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(10.0f);
    theme.set_scrollbar_overlay(true);
    apply_theme(view, theme);
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the content was measured once at the whole width, because an overlay runs no narrowed second pass (§8.2)
    EXPECT_FLOAT_EQ(view.content_extent().width, 200.0f);
}

TEST(ScrollViewChecks, TheMacosPresetOverlaysAndTheDerivedPresetReserves) {
    // arrange two identical viewports, one through each preset (§8.4)
    BindingEngine macos_engine;
    ScrollView macos_view(&macos_engine);
    auto macos_content = std::make_unique<ContentNode>();
    macos_content->intrinsic = Size{100.0f, 900.0f};
    macos_view.add_child(std::move(macos_content));
    macos_view.set_viewport_extent(Size{200.0f, 300.0f});
    const Theme macos = Theme::macos(Accents{}, ColorScheme::Light);
    apply_theme(macos_view, macos);
    BindingEngine derived_engine;
    ScrollView derived_view(&derived_engine);
    auto derived_content = std::make_unique<ContentNode>();
    derived_content->intrinsic = Size{100.0f, 900.0f};
    derived_view.add_child(std::move(derived_content));
    derived_view.set_viewport_extent(Size{200.0f, 300.0f});
    const Theme derived = ThemeBuilder::light(Accents{});
    apply_theme(derived_view, derived);
    Size measured{};
    // act
    macos_view.measure(SizeConstraints::unconstrained(), measured);
    macos_view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    derived_view.measure(SizeConstraints::unconstrained(), measured);
    derived_view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the macos preset overlays while the derived preset keeps its ten point gutter (§8.4)
    EXPECT_FLOAT_EQ(macos_view.content_viewport().width, 200.0f);
    EXPECT_FLOAT_EQ(derived_view.content_viewport().width, 190.0f);
    EXPECT_TRUE(macos_view.scrollbar_visible());
    EXPECT_TRUE(derived_view.scrollbar_visible());
}

TEST(ScrollViewChecks, AnOverlayHoverAndDragAskForAPaint) {
    // arrange a themed overlay bar that has already been drawn (§6.1)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(7.0f);
    theme.set_scrollbar_overlay(true);
    theme.set_scrollbar_hover_extent(11.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.clear_dirty();
    // act the pointer enters, which widens the thumb (§8.3)
    Event enter{.kind = EventKind::PointerEnter, .position = Point{20.0f, 20.0f}};
    view.dispatch_event(enter);
    // assert the widening asks for the strip it now draws to be repainted (§6.1)
    EXPECT_TRUE(view.render_dirty());
    // act the press grabs the thumb and the release ends the drag (§8.3)
    view.clear_dirty();
    Event down{.kind = EventKind::PointerDown, .position = Point{194.0f, 10.0f}};
    view.dispatch_event(down);
    EXPECT_TRUE(view.render_dirty());
    view.clear_dirty();
    Event up{.kind = EventKind::PointerUp, .position = Point{194.0f, 10.0f}};
    view.dispatch_event(up);
    EXPECT_TRUE(view.render_dirty());
}

TEST(ScrollViewChecks, TheOverlayTrackWidensWithTheThumb) {
    // arrange a themed overlay bar whose pointer is inside the viewport (§6.1)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(7.0f);
    theme.set_scrollbar_overlay(true);
    theme.set_scrollbar_hover_extent(11.0f);
    theme.set_scrollbar_overlay_inset(2.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act
    Event enter{.kind = EventKind::PointerEnter, .position = Point{20.0f, 20.0f}};
    view.dispatch_event(enter);
    RecordingContext context;
    view.render(context);
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.height == 300.0f)
            track = rect;
    // assert the track takes the widened extent, so the thumb never rides outside its own strip (§6.1)
    EXPECT_FLOAT_EQ(track.width, 11.0f);
    EXPECT_FLOAT_EQ(track.x, 187.0f);
}

TEST(ScrollViewChecks, AnOverlayThumbWidensWhileThePointerIsOnTheView) {
    // arrange a themed overlay bar, whose thumb the pointer makes grabbable (§6.1)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(7.0f);
    theme.set_scrollbar_overlay(true);
    theme.set_scrollbar_hover_extent(11.0f);
    theme.set_scrollbar_overlay_inset(2.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    RecordingContext resting_context;
    view.render(resting_context);
    Rect resting{};
    for (const Rect& rect : resting_context.rounded)
        resting = rect;
    // assert the resting thumb takes the extent the theme states (§6.1)
    EXPECT_FLOAT_EQ(resting.width, 7.0f);
    // act the pointer enters the viewport, which is where the bar reads its hover (§8.3)
    Event enter{.kind = EventKind::PointerEnter, .position = Point{20.0f, 20.0f}};
    view.dispatch_event(enter);
    RecordingContext hovered_context;
    view.render(hovered_context);
    Rect hovered{};
    for (const Rect& rect : hovered_context.rounded)
        hovered = rect;
    // assert the hovered thumb widens and still stands off the trailing edge by its inset (§6.1)
    EXPECT_FLOAT_EQ(hovered.width, 11.0f);
    EXPECT_FLOAT_EQ(hovered.x, 187.0f);
    // act the pointer leaves, which is what returns the thumb to its resting width (§8.3)
    Event leave{.kind = EventKind::PointerLeave, .position = Point{20.0f, 20.0f}};
    view.dispatch_event(leave);
    RecordingContext left_context;
    view.render(left_context);
    Rect left{};
    for (const Rect& rect : left_context.rounded)
        left = rect;
    EXPECT_FLOAT_EQ(left.width, 7.0f);
}

TEST(ScrollViewChecks, AnOverlayThumbWidensWhileTheThumbIsDragged) {
    // arrange an overlay bar whose resting thumb the pointer can grab (§8.3)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(7.0f);
    theme.set_scrollbar_overlay(true);
    theme.set_scrollbar_hover_extent(11.0f);
    theme.set_scrollbar_overlay_inset(2.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act the press lands on the resting thumb, which is the only draggable part (§8.3)
    Event down{.kind = EventKind::PointerDown, .position = Point{194.0f, 10.0f}};
    view.dispatch_event(down);
    EXPECT_TRUE(down.handled);
    RecordingContext drag_context;
    view.render(drag_context);
    Rect drag{};
    for (const Rect& rect : drag_context.rounded)
        drag = rect;
    // assert a dragged thumb takes the hover extent, because the pointer is holding it (§6.1)
    EXPECT_FLOAT_EQ(drag.width, 11.0f);
    // act the release ends the drag, which drops the thumb back to the resting width (§8.3)
    Event up{.kind = EventKind::PointerUp, .position = Point{194.0f, 10.0f}};
    view.dispatch_event(up);
    RecordingContext released_context;
    view.render(released_context);
    Rect released{};
    for (const Rect& rect : released_context.rounded)
        released = rect;
    EXPECT_FLOAT_EQ(released.width, 7.0f);
}

TEST(ScrollViewChecks, AGutterBarIgnoresTheHoverExtent) {
    // arrange a gutter bar whose theme also carries an overlay hover width, which must not apply (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(14.0f);
    theme.set_scrollbar_hover_extent(20.0f);
    apply_theme(view, theme);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // act the pointer enters, which is what widens an overlay bar (§8.3)
    Event enter{.kind = EventKind::PointerEnter, .position = Point{20.0f, 20.0f}};
    view.dispatch_event(enter);
    RecordingContext context;
    view.render(context);
    Rect thumb{};
    for (const Rect& rect : context.rounded)
        thumb = rect;
    // assert a gutter bar keeps its extent and its gutter, because the overlay policy is off (§8.2)
    EXPECT_FLOAT_EQ(thumb.width, 14.0f);
    EXPECT_FLOAT_EQ(view.content_viewport().width, 186.0f);
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

TEST(ScrollViewChecks, AnOverflowingAsNeededBarReservesItsGutter) {
    // arrange a viewport whose content is taller than it (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(bounds);
    // assert the bar owns the trailing strip and the content is measured and clipped inside the rest (§8.2)
    EXPECT_TRUE(view.scrollbar_visible());
    EXPECT_EQ(view.content_viewport(), (Rect::from_origin_size(Point{0.0f, 0.0f}, Size{190.0f, 300.0f})));
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 190.0f);
    EXPECT_FLOAT_EQ(content_ptr->bounds().width, 190.0f);
    RecordingContext context;
    view.render(context);
    ASSERT_FALSE(context.clips.empty());
    EXPECT_EQ(context.clips.front(), view.content_viewport());
    // assert the track occupies the gutter the content viewport excludes (§6.1)
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.width == 10.0f && rect.height == 300.0f)
            track = rect;
    EXPECT_EQ(track, (Rect::from_origin_size(Point{190.0f, 0.0f}, Size{10.0f, 300.0f})));
}

TEST(ScrollViewChecks, AFittingAsNeededBarLeavesTheFullWidth) {
    // arrange content that fits, so the automatic policy has no bar to reserve (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 200.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(bounds);
    // assert no gutter is taken, because the content never needs a bar (§8.2)
    EXPECT_FALSE(view.scrollbar_visible());
    EXPECT_EQ(view.content_viewport(), bounds);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 200.0f);
    RecordingContext context;
    view.render(context);
    ASSERT_FALSE(context.clips.empty());
    EXPECT_EQ(context.clips.front(), bounds);
}

TEST(ScrollViewChecks, AnAlwaysOnBarReservesItsGutterEvenWhenTheContentFits) {
    // arrange a forced bar over content that fits, which still owns its gutter (§8)
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
    // assert the gutter is reserved while the thumb is left off, because there is no travel (§8.2)
    EXPECT_TRUE(view.scrollbar_visible());
    EXPECT_FALSE(view.scrollable());
    EXPECT_EQ(view.content_viewport().width, 190.0f);
    RecordingContext context;
    view.render(context);
    EXPECT_TRUE(context.rounded.empty());
}

TEST(ScrollViewChecks, AnAlwaysOffBarLeavesTheFullWidthToOverflowingContent) {
    // arrange a hidden bar over content that would otherwise overflow (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(bounds);
    // assert the content still scrolls but keeps the whole width, because no bar takes a gutter (§8)
    EXPECT_FALSE(view.scrollbar_visible());
    EXPECT_TRUE(view.scrollable());
    EXPECT_EQ(view.content_viewport(), bounds);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 200.0f);
    RecordingContext context;
    view.render(context);
    ASSERT_FALSE(context.clips.empty());
    EXPECT_EQ(context.clips.front(), bounds);
}

TEST(ScrollViewChecks, TheAsNeededProbeMeasuresTwiceWhenTheContentOverflows) {
    // arrange content whose height decides whether the bar is needed (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WidthRecordingContent>();
    content->height = 900.0f;
    WidthRecordingContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert the full width probe runs first and the reserved gutter narrows the second pass (§8.2)
    ASSERT_EQ(content_ptr->widths.size(), 2u);
    EXPECT_FLOAT_EQ(content_ptr->widths.at(0), 200.0f);
    EXPECT_FLOAT_EQ(content_ptr->widths.at(1), 190.0f);
}

TEST(ScrollViewChecks, TheAsNeededProbeMeasuresOnceWhenTheContentFits) {
    // arrange content that fits, so the automatic policy never narrows it (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WidthRecordingContent>();
    content->height = 200.0f;
    WidthRecordingContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert exactly one probe, at the full width, because no gutter is reserved (§8.2)
    ASSERT_EQ(content_ptr->widths.size(), 1u);
    EXPECT_FLOAT_EQ(content_ptr->widths.at(0), 200.0f);
}

TEST(ScrollViewChecks, AnAlwaysOnBarMeasuresOnceAtTheContentWidth) {
    // arrange a forced bar, whose gutter is known before the content is measured (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WidthRecordingContent>();
    content->height = 900.0f;
    WidthRecordingContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert one pass at the narrowed width, because the policy reserved the gutter up front (§8.2)
    ASSERT_EQ(content_ptr->widths.size(), 1u);
    EXPECT_FLOAT_EQ(content_ptr->widths.at(0), 190.0f);
}

TEST(ScrollViewChecks, AnAlwaysOffBarMeasuresOnceAtTheFullWidth) {
    // arrange a hidden bar over content that would otherwise overflow (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WidthRecordingContent>();
    content->height = 900.0f;
    WidthRecordingContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    // assert one pass at the full width, because no bar takes a gutter (§8.2)
    ASSERT_EQ(content_ptr->widths.size(), 1u);
    EXPECT_FLOAT_EQ(content_ptr->widths.at(0), 200.0f);
}

TEST(ScrollViewChecks, ThePushedViewportCarriesTheContentWidth) {
    // arrange a virtualized content whose reported extent is wider than the content area (§8.2, AD-13)
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
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the window names the gutter-free area, so rows measure inside it (§8.2, AD-13)
    EXPECT_EQ(content_ptr->window.viewport, (Size{190.0f, 300.0f}));
    EXPECT_EQ(content_ptr->window.content, (Size{200.0f, 900.0f}));
    // assert the content is placed no wider than the window, because only the vertical axis scrolls (§8.2)
    EXPECT_FLOAT_EQ(content_ptr->bounds().width, 190.0f);
}

TEST(ScrollViewChecks, APolicySwitchInvalidatesLayout) {
    // arrange a laid out view that is clean (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.clear_dirty();
    // act a policy switch adds or removes the gutter (§8)
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    // assert both layout passes run, because the content width changed (§8.2)
    EXPECT_TRUE(view.layout_dirty());
}

TEST(ScrollViewChecks, AThemedGutterWidensWithTheThemeExtent) {
    // arrange a themed viewport whose bar is wider than the fallback (§6.1)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(14.0f);
    apply_theme(view, theme);
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the content is measured against the themed gutter, not the fallback (§6.1, §8.2)
    EXPECT_EQ(view.content_viewport().width, 186.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 186.0f);
    // act a wider theme reflows the same content
    Theme wider;
    wider.set_scrollbar_extent(20.0f);
    apply_theme(view, wider);
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the content follows the wider gutter, because the theme owns the metric (§6.1, §8.2)
    EXPECT_EQ(view.content_viewport().width, 180.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 180.0f);
}

TEST(ScrollViewChecks, AViewportNarrowerThanTheBarHasNoContentArea) {
    // arrange a viewport narrower than the bar it reserves (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{8.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 300.0f}));
    // assert the content width clamps to zero rather than going negative (§8.2)
    EXPECT_EQ(view.content_viewport().width, 0.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 0.0f);
    EXPECT_TRUE(view.scrollable());
}

TEST(ScrollViewChecks, TheSecondMeasureSettlesTheExtentAndTheOffset) {
    // arrange content whose height grows as the gutter narrows it, which only the second pass can see (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WidthDrivenContent>();
    content->reference_width = 200.0f;
    content->base_height = 900.0f;
    content->slope = 2.0f;
    WidthDrivenContent* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the extent, the offset range and the content width all come from the narrowed second pass (§8.2)
    EXPECT_EQ(content_ptr->widths, (std::vector<float>{200.0f, 190.0f}));
    EXPECT_EQ(view.content_extent(), (Size{190.0f, 920.0f}));
    EXPECT_FLOAT_EQ(view.maximum_offset(), 620.0f);
    EXPECT_FLOAT_EQ(content_ptr->bounds().width, 190.0f);
}

TEST(ScrollViewChecks, AContentThatFitsAfterTheNarrowedRemeasureKeepsItsGutter) {
    // arrange content whose probe overflows but whose narrowed pass fits, which is the stable hysteresis (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<WidthDrivenContent>();
    content->reference_width = 200.0f;
    content->base_height = 200.0f;
    content->slope = -1.0f;
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 195.0f});
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 195.0f}));
    // assert the probe's reservation stands, so a width-driven content cannot flip the gutter every measure (§8.2)
    EXPECT_EQ(view.content_viewport().width, 190.0f);
    EXPECT_EQ(view.content_extent(), (Size{190.0f, 190.0f}));
    EXPECT_TRUE(view.scrollbar_visible());
    EXPECT_FALSE(view.scrollable());
    RecordingContext context;
    view.render(context);
    // assert the reserved gutter draws the track and leaves the thumb off, because there is no travel (§6.1)
    EXPECT_TRUE(context.rounded.empty());
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.width == 10.0f && rect.height == 195.0f)
            track = rect;
    EXPECT_EQ(track, (Rect::from_origin_size(Point{190.0f, 0.0f}, Size{10.0f, 195.0f})));
}

TEST(ScrollViewChecks, AContentThatShrinksReleasesTheGutter) {
    // arrange an overflowing view, whose bar takes a gutter (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    ASSERT_EQ(view.content_viewport().width, 190.0f);
    // act the content shrinks until it fits, which is a data change rather than a policy change
    content_ptr->intrinsic = Size{100.0f, 200.0f};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the gutter is released, because the next measure found no overflow (§8.2)
    EXPECT_EQ(view.content_viewport().width, 200.0f);
    EXPECT_FALSE(view.scrollbar_visible());
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 200.0f);
}

TEST(ScrollViewChecks, APolicySwitchToAlwaysOffRestoresTheFullWidth) {
    // arrange an overflowing view whose measured policy reserved the gutter (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(bounds);
    ASSERT_EQ(view.content_viewport().width, 190.0f);
    // act the policy turns the bar off, which must drop the gutter immediately (§8)
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOff);
    // assert the accessors agree before the next measure, because the policy is total (§8.2)
    EXPECT_EQ(view.content_viewport(), bounds);
    EXPECT_FALSE(view.scrollbar_visible());
    // act the next layout settles the content at the full width
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(bounds);
    // assert the content was measured at the full width and no track was painted (§8.2)
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 200.0f);
    RecordingContext context;
    view.render(context);
    for (const Rect& rect : context.rects)
        EXPECT_FALSE(rect.width == 10.0f && rect.height == 300.0f);
}

TEST(ScrollViewChecks, ASwitchFromAlwaysOnToAsNeededDropsTheForcedGutter) {
    // arrange a forced bar over fitting content, whose gutter is reserved without overflow (§8)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 200.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    ASSERT_EQ(view.content_viewport().width, 190.0f);
    // act the automatic policy takes over, which only reserves on overflow (§8)
    view.set_scrollbar_visibility(ScrollbarVisibility::AsNeeded);
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the forced gutter is gone and the content takes the full width again (§8.2)
    EXPECT_EQ(view.content_viewport().width, 200.0f);
    EXPECT_FALSE(view.scrollbar_visible());
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 200.0f);
}

TEST(ScrollViewChecks, TheBarStaysInsideANarrowViewport) {
    // arrange a forced bar wider than the view itself, which must not paint outside its bounds (§8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{8.0f, 300.0f});
    view.set_scrollbar_visibility(ScrollbarVisibility::AlwaysOn);
    const Rect bounds = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{8.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(bounds);
    RecordingContext context;
    // act
    view.render(context);
    // assert the track and the thumb are clipped to the bounds rather than starting left of the view (§8.2)
    Rect track{};
    for (const Rect& rect : context.rects)
        if (rect.width == 8.0f && rect.height == 300.0f)
            track = rect;
    EXPECT_EQ(track, bounds);
    ASSERT_EQ(context.rounded.size(), 1u);
    EXPECT_GE(context.rounded.front().x, bounds.x);
    EXPECT_LE(context.rounded.front().x + context.rounded.front().width, bounds.x + bounds.width);
    // act a scroll, whose damage must stay inside the bounds as well
    view.clear_damage_region();
    view.set_offset(50.0f);
    view.arrange(bounds);
    // assert the clamped strip damages exactly the view instead of spilling past its leading edge (§6.1)
    ASSERT_TRUE(view.damage_region().has_value());
    EXPECT_EQ(*view.damage_region(), bounds);
}

TEST(ScrollViewChecks, ANegativeBarExtentReservesNothing) {
    // arrange a theme whose bar width is negative, which the gutter clamp must refuse (§6.1, §8.2)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Theme theme;
    theme.set_scrollbar_extent(-5.0f);
    apply_theme(view, theme);
    Size measured{};
    // act
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert the content keeps the full width, because a negative gutter is no gutter (§8.2)
    EXPECT_EQ(view.content_viewport().width, 200.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 200.0f);
    EXPECT_FALSE(view.scrollbar_visible());
}

TEST(ScrollViewChecks, ANestedScrollViewReservesOnlyItsOwnGutter) {
    // arrange an inner viewport inside an outer one, both overflowing (§8.2)
    BindingEngine engine;
    ScrollView outer(&engine);
    auto inner = std::make_unique<ScrollView>(&engine);
    inner->set_viewport_extent(Size{0.0f, 400.0f});
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    ContentNode* content_ptr = content.get();
    inner->add_child(std::move(content));
    ScrollView* inner_ptr = inner.get();
    outer.add_child(std::move(inner));
    outer.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    // act
    outer.measure(SizeConstraints::unconstrained(), measured);
    outer.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    // assert each viewport removes only its own gutter, so the inner content is narrowed twice in total (§8.2)
    EXPECT_EQ(outer.content_viewport().width, 190.0f);
    EXPECT_EQ(inner_ptr->content_viewport().width, 180.0f);
    EXPECT_FLOAT_EQ(content_ptr->measured_size().width, 180.0f);
}

TEST(ScrollViewChecks, AKeyAtTheEdgeIsLeftToAnEnclosingViewport) {
    // arrange a viewport tall enough to scroll, which the keys drive (§8.3)
    BindingEngine engine;
    ScrollView view(&engine);
    auto content = std::make_unique<ContentNode>();
    content->intrinsic = Size{100.0f, 900.0f};
    view.add_child(std::move(content));
    view.set_viewport_extent(Size{200.0f, 300.0f});
    Size measured{};
    view.measure(SizeConstraints::unconstrained(), measured);
    view.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 300.0f}));
    view.scroll_to(view.maximum_offset());
    // act the keys that have nowhere to go at the trailing edge (§8.3)
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    view.dispatch_event(down);
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    view.dispatch_event(end);
    // assert neither was claimed, because an edge leaves the key to whatever encloses the view (§8.3)
    EXPECT_FALSE(down.handled);
    EXPECT_FALSE(end.handled);
    // act the keys that have nowhere to go at the leading edge (§8.3)
    view.scroll_to(0.0f);
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    view.dispatch_event(up);
    Event home{.kind = EventKind::KeyDown, .key = Key::Home};
    view.dispatch_event(home);
    // assert the leading edge leaves them too, which is the same rule the wheel follows (§8.3)
    EXPECT_FALSE(up.handled);
    EXPECT_FALSE(home.handled);
}
