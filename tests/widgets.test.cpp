#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "commands/command.h"
#include "core/constraints.h"
#include "focus/focus_manager.h"
#include "layout/column_layout.h"
#include "layout/row_layout.h"
#include "render/frame_scene.h"
#include "render/render_context.h"
#include "runtime/binding_engine.h"
#include "semantics/semantic_node.h"
#include "semantics/semantic_provider.h"
#include "style/theme_builder.h"
#include "ui/scene_builder.h"
#include "widgets/button.h"
#include "widgets/canvas.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/slider.h"
#include "widgets/theme_controller.h"
#include "window/window_metrics.h"

using namespace albor;

namespace
{
    // the child double a widget composes over, which paints one rectangle of its assigned bounds (§6.1)
    class WidgetChildNode final : public UiNode
    {
    public:
        // how many times the child was asked to draw (§6.1)
        std::size_t draws = 0;

        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the child offers the extent it was built with, because that is what its parent asked for (§8.2)
            UiNode::measure(constraints, out_size);
            // the intrinsic extent is clamped into the constraints, the way a leaf does (§8.2)
            out_size = constraints.constrain(m_intrinsic);
            m_measured_size = out_size;
        }

        void render(RenderContext& context) override {
            // the draw is counted, so a test can tell a recorded child from a skipped one (§6.1)
            draws++;
            // the child paints its own bounds, which is the content a parent has to record (§6.1)
            context.draw_rect(bounds(), Paint{.fill = m_color});
            // the children of the child draw over it, which is the composite contract (§6.1)
            UiNode::render(context);
        }

        // the extent the child measures to (§8.2)
        Size m_intrinsic = Size{40.0f, 30.0f};

        // the colour the child paints (§6.1)
        Color m_color = Color::rgb(0.2f, 0.6f, 0.9f);
    };

    // the rectangles of the shape commands of a scene, which is what a widget assertion reads off the one list (§6.1)
    std::vector<Rect> rect_commands(const FrameScene& scene) {
        // the shapes are gathered in paint order, because that is the paint order the list carries (§6.1)
        std::vector<Rect> rects;
        // every command is read, because a scene interleaves kinds (§6.1)
        for (const DrawCommand& command : scene.commands())
            if (command.kind() == DrawCommandKind::Rect || command.kind() == DrawCommandKind::RoundedRect)
                rects.push_back(command.rect_geometry().rect);
        return rects;
    }

    // the commands of the runs of a scene, which is what a run assertion reads off the one list (§6.1)
    std::vector<DrawCommand> text_commands(const FrameScene& scene) {
        // the runs are gathered in paint order, because that is the paint order the list carries (§6.1)
        std::vector<DrawCommand> gathered;
        // every command is read, because a scene interleaves kinds (§6.1)
        for (const DrawCommand& command : scene.commands())
            if (command.kind() == DrawCommandKind::Text)
                gathered.push_back(command);
        return gathered;
    }

    // the leaf the padding wrapper arranges, so a widget can be nested without a full tree (§8.2)
    class WidgetTestHost final : public UiNode
    {
    public:
        void measure(const SizeConstraints& constraints, Size& out_size) override {
            // the host fills whatever it is offered, because the widget tests exercise the widget not the host (§8.2)
            out_size = constraints.constrain(Size{100.0f, 100.0f});
            m_measured_size = out_size;
        }

        void arrange(const Rect& bounds) override {
            // the bounds are recorded before the children see them, which is the arrange contract (§8.2)
            m_bounds = bounds;
            m_measured_size = Size{.width = bounds.width, .height = bounds.height};
            for (const auto& child : children())
                child->arrange(bounds);
        }
    };

    // the text engine test double, which measures with a fixed advance so a widget test asserts on the widget rather than on whatever shaping backend the platform happens to provide (§9.1)
    class StubTextEngine final : public TextEngine
    {
    public:
        // the advance one byte of text contributes, in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // the height the double reports for any run, in logical pixels (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // how many measurements the widget asked for, which a test asserts on, and which has to be mutable because the measurement contract is const (§9.1)
        mutable std::size_t measurements = 0;

        // whether the double reports itself as able to measure (§9.1)
        bool usable = true;

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override {
            // the default size overload forwards to the explicit one, which is the whole implementation (§9.1)
            return measure(text, max_width, DEFAULT_FONT_SIZE);
        }

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override {
            // an engine that cannot measure reports nothing, which is what an absent backend looks like (§9.1)
            if (!usable)
                return TextLayout{};
            // an empty run carries no glyphs, so the double measures it as nothing, as the contract requires (§9.1)
            if (text.empty())
                return TextLayout{};
            // the measurement is counted, so a test can prove the label asked (§9.1)
            measurements++;
            // the double shapes one glyph per character, which stands in for shaping without a font backend (§9.1)
            ShapedRun::Line line;
            // the line origin is the baseline the synthesized glyphs hang from (§9.1)
            line.origin = Point{.x = 0.0f, .y = HEIGHT * 0.8f};
            // every character becomes one glyph placed a fixed advance apart, which is what the double measures (§9.1)
            for (const char character : text)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(character), .position = Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}});
            // the advance counts characters, and the ceiling clamps the result the way a container expects (§8.2)
            const float advanced = static_cast<float>(text.size()) * ADVANCE;
            // the shaped run travels beside the extents, which is what a measurement hands over (§9.1)
            return TextLayout{.run = ShapedRun::make({std::move(line)}, font_size), .size = Size{.width = max_width > 0.0f ? std::min(advanced, max_width) : advanced, .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        [[nodiscard]] bool ready() const override {
            // the double reports the flag a test sets, so an unusable engine can be exercised (§9.1)
            return usable;
        }
    };
} // namespace

TEST(LabelChecks, MeasuresThroughTheTextEngine) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    Size measured{};
    // act
    label.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_GT(measured.width, 0.0f);
    EXPECT_GT(measured.height, 0.0f);
}

TEST(LabelChecks, AnEmptyTextMeasuresAsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine);
    Size measured{};
    // act
    label.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(LabelChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "A long run of text that will not fit");
    Size measured{};
    // act
    label.measure(SizeConstraints::at_most(Size{10.0f, 10.0f}), measured);
    // assert
    EXPECT_LE(measured.width, 10.0f);
    EXPECT_LE(measured.height, 10.0f);
}

TEST(LabelChecks, AColumnNarrowsTheLabelToTheWidthItLeaves) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    ColumnLayout column;
    auto label = std::make_unique<Label>(&engine, &text_engine, "A long run of text that will not fit");
    Label* label_ptr = label.get();
    column.add_child(std::move(label));
    column.set_padding(10.0f);
    Size measured{};
    // act
    column.measure(SizeConstraints::at_most(Size{100.0f, 500.0f}), measured);
    // assert the label sees the width the insets leave, so its run wraps inside the column (§8.2, §9.1)
    EXPECT_FLOAT_EQ(label_ptr->measured_size().width, 80.0f);
    EXPECT_FLOAT_EQ(measured.width, 100.0f);
}

TEST(LabelChecks, ArrangeStoresTheBounds) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    const Rect bounds = Rect::from_origin_size(Point{3.0f, 4.0f}, Size{60.0f, 20.0f});
    // act
    label.arrange(bounds);
    // assert
    EXPECT_EQ(label.bounds(), bounds);
    EXPECT_EQ(label.preferred_size(), (Size{60.0f, 20.0f}));
}

TEST(LabelChecks, RendersTheRunIntoTheScene) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    Size ignored{};
    label.measure(SizeConstraints::unconstrained(), ignored);
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 20.0f}));
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f);
    SceneRecorder recorder(metrics);
    // act
    label.render(recorder);
    // assert
    ASSERT_EQ(text_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(text_commands(*recorder.scene()).front().text_geometry().layout.run->glyph_count(), 5u);
}

TEST(LabelChecks, AnEmptyRunRecordsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine);
    Size ignored{};
    label.measure(SizeConstraints::unconstrained(), ignored);
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    label.render(recorder);
    // assert
    EXPECT_TRUE(text_commands(*recorder.scene()).empty());
}

TEST(LabelChecks, TheRunIsDrawnInTheForegroundColour) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    label.set_foreground(Color::rgb(1.0f, 0.0f, 0.0f));
    Size ignored{};
    label.measure(SizeConstraints::unconstrained(), ignored);
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 20.0f}));
    EXPECT_EQ(label.foreground(), Color::rgb(1.0f, 0.0f, 0.0f));
    // act / assert the colour is read back, because the run colour itself lives in the recorded scene (§6.1)
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    label.render(recorder);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
}

TEST(LabelChecks, SetTextRemeasuresAndInvalidates) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "a");
    Size first{};
    label.measure(SizeConstraints::unconstrained(), first);
    label.clear_dirty();
    // act
    label.set_text("a much longer run of text");
    // assert
    EXPECT_EQ(label.text(), "a much longer run of text");
    EXPECT_TRUE(label.layout_dirty());
    Size second{};
    label.measure(SizeConstraints::unconstrained(), second);
    EXPECT_GT(second.width, first.width);
}

TEST(LabelChecks, AnUnchangedTextInvalidatesNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "same");
    label.clear_dirty();
    // act
    label.set_text("same");
    // assert
    EXPECT_FALSE(label.layout_dirty());
}

TEST(LabelChecks, SetForegroundRedrawsWithoutRemeasuring) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    label.clear_dirty();
    // act
    label.set_foreground(Color::rgb(0.0f, 1.0f, 0.0f));
    // assert
    EXPECT_TRUE(label.render_dirty());
    EXPECT_FALSE(label.layout_dirty());
}

TEST(LabelChecks, AnUnchangedForegroundRedrawsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    label.set_foreground(Color::rgb(0.0f, 1.0f, 0.0f));
    label.clear_dirty();
    // act
    label.set_foreground(Color::rgb(0.0f, 1.0f, 0.0f));
    // assert
    EXPECT_FALSE(label.render_dirty());
}

TEST(LabelChecks, TheForegroundDefaultsToWhite) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    // act assert
    EXPECT_EQ(label.foreground(), Color::rgb(1.0f, 1.0f, 1.0f));
}

TEST(LabelChecks, TheFontSizeDefaultsToTheEngineDefault) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    // act assert
    EXPECT_FLOAT_EQ(label.font_size(), TextEngine::DEFAULT_FONT_SIZE);
}

TEST(LabelChecks, SetFontSizeRemeasures) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    Size before{};
    label.measure(SizeConstraints::unconstrained(), before);
    label.clear_dirty();
    // act
    label.set_font_size(32.0f);
    // assert
    EXPECT_FLOAT_EQ(label.font_size(), 32.0f);
    EXPECT_TRUE(label.layout_dirty());
}

TEST(LabelChecks, AnUnchangedFontSizeInvalidatesNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    label.set_font_size(20.0f);
    label.clear_dirty();
    // act
    label.set_font_size(20.0f);
    // assert
    EXPECT_FALSE(label.layout_dirty());
}

TEST(LabelChecks, AnnouncesTheLabelRoleAndItsText) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor", "caption");
    label.arrange(Rect::from_origin_size(Point{2.0f, 3.0f}, Size{50.0f, 20.0f}));
    // act
    const SemanticNode node = label.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Label);
    EXPECT_EQ(node.name(), "Albor");
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{2.0f, 3.0f}, Size{50.0f, 20.0f})));
}

TEST(LabelChecks, AVisibleLabelIsNotDisabled) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    // act
    const SemanticNode node = label.semantic_node();
    // assert
    EXPECT_FALSE(node.disabled());
}

TEST(LabelChecks, AHiddenLabelAnnouncesAsDisabled) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    label.set_visible(false);
    // act
    const SemanticNode node = label.semantic_node();
    // assert
    EXPECT_TRUE(node.disabled());
}

TEST(LabelChecks, ExposesNoSemanticChildren) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    // act assert
    EXPECT_EQ(label.semantic_child_count(), 0u);
    EXPECT_EQ(label.semantic_child(0), nullptr);
}

TEST(LabelChecks, AWidgetExposesItsChildrenSemantically) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    StubTextEngine stub_text;
    button.add_child(std::make_unique<Label>(&engine, &stub_text, "child"));
    // act
    const std::size_t count = button.semantic_child_count();
    std::unique_ptr<SemanticProvider> child = button.semantic_child(0);
    // assert
    EXPECT_EQ(count, 1u);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::Label);
}

TEST(LabelChecks, AChildPastTheEndIsNoChild) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act
    std::unique_ptr<SemanticProvider> child = button.semantic_child(7);
    // assert
    EXPECT_EQ(child, nullptr);
}

TEST(LabelChecks, AChildWithoutSemanticsAnnouncesNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    RowLayout row;
    row.add_child(std::make_unique<WidgetTestHost>());
    BindingEngine widget_engine;
    Button button(&widget_engine, &text_engine, "Press");
    button.add_child(std::make_unique<RowLayout>());
    // act
    std::unique_ptr<SemanticProvider> child = button.semantic_child(0);
    // assert a plain layout carries no meaning, so the walk reaches a node that announces nothing (§10.1)
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::None);
    EXPECT_EQ(child->semantic_child_count(), 0u);
    EXPECT_EQ(child->semantic_child(0), nullptr);
}

TEST(ButtonChecks, MeasuresToAtLeastTheControlSize) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine);
    Size measured{};
    // act
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert an unlabelled button must stay grabbable (§8.2)
    EXPECT_GT(measured.width, 0.0f);
    EXPECT_GT(measured.height, 0.0f);
}

TEST(ButtonChecks, MeasuresToFitItsLabel) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press me");
    Size measured{};
    // act
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_GT(measured.width, 24.0f);
}

TEST(ButtonChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press me");
    Size measured{};
    // act
    button.measure(SizeConstraints::at_most(Size{10.0f, 10.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
}

TEST(ButtonChecks, TheLabelMeasuresThroughTheTextEngine) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    Size measured{};
    // act
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert the engine's advance drives the plate, once at construction and once in the measure pass (AD-14, §9.1)
    EXPECT_EQ(text_engine.measurements, 2u);
    EXPECT_FLOAT_EQ(measured.width, 50.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(ButtonChecks, AButtonWithNoEngineStaysGrabbable) {
    // arrange
    BindingEngine engine;
    Button button(&engine, nullptr, "Press");
    Size measured{};
    // act
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert the minimum extent covers the label the engine never measured (§8.2, AD-14)
    EXPECT_FLOAT_EQ(measured.width, 24.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(ButtonChecks, AChangedLabelIsRemeasuredByTheEngine) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act
    button.set_label("Press me");
    Size measured{};
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert the new run asks the engine again, and the plate follows it (§9.1, AD-14)
    EXPECT_EQ(text_engine.measurements, 3u);
    EXPECT_FLOAT_EQ(measured.width, 80.0f);
}

TEST(ButtonChecks, AMeasureNarrowsTheRunToTheOfferedWidth) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press me");
    Size measured{};
    button.measure(SizeConstraints::at_most(Size{30.0f, 100.0f}), measured);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the run was remeasured at the offered width, so it centres by its narrowed extent (§8.2, §9.1, AD-14)
    ASSERT_EQ(text_commands(*recorder.scene()).size(), 1u);
    EXPECT_FLOAT_EQ(text_commands(*recorder.scene()).front().text_geometry().layout.size.width, 30.0f);
    EXPECT_FLOAT_EQ(text_commands(*recorder.scene()).front().text_geometry().origin.x, 25.0f);
}

TEST(ButtonChecks, ArrangeStoresTheBounds) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    const Rect bounds = Rect::from_origin_size(Point{1.0f, 2.0f}, Size{80.0f, 30.0f});
    // act
    button.arrange(bounds);
    // assert
    EXPECT_EQ(button.bounds(), bounds);
}

TEST(ButtonChecks, RendersThePlateAndTheLabel) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate is recorded by the background hook and the run by the label (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(text_commands(*recorder.scene()).front().text_geometry().layout.run->glyph_count(), 5u);
}

TEST(ButtonChecks, AnUnlabelledButtonRecordsOnlyThePlate) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert
    EXPECT_TRUE(text_commands(*recorder.scene()).empty());
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(ButtonChecks, AnUnarrangedButtonDrawsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert a button with no bounds has no plate, so nothing is recorded (§6.2)
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
}

TEST(ButtonChecks, HitsInsideItsBounds) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{50.0f, 20.0f}));
    // act
    const bool inside = button.hit_test(Point{20.0f, 15.0f});
    // assert
    EXPECT_TRUE(inside);
}

TEST(ButtonChecks, DoesNotHitOutsideItsBounds) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{50.0f, 20.0f}));
    // act
    const bool outside = button.hit_test(Point{100.0f, 100.0f});
    // assert
    EXPECT_FALSE(outside);
}

TEST(ButtonChecks, DoesNotHitOnTheFarEdge) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    // act
    const bool on_edge = button.hit_test(Point{50.0f, 10.0f});
    // assert the far edge belongs to the next control, so the hit test excludes it (§8.3)
    EXPECT_FALSE(on_edge);
}

TEST(ButtonChecks, AnUnarrangedButtonIsNotAHit) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act
    const bool hit = button.hit_test(Point{0.0f, 0.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(ButtonChecks, AHiddenButtonIsNotAHit) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    button.set_visible(false);
    // act
    const bool hit = button.hit_test(Point{10.0f, 10.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(ButtonChecks, ADisabledButtonIsNotAHit) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    button.set_disabled(true);
    // act
    const bool hit = button.hit_test(Point{10.0f, 10.0f});
    // assert
    EXPECT_FALSE(hit);
    EXPECT_TRUE(button.disabled());
}

TEST(ButtonChecks, APressRunsTheClickHandler) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(ButtonChecks, APressOutsideIsNotClaimed) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::PointerDown, .position = Point{100.0f, 100.0f}};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 0u);
    EXPECT_FALSE(event.handled);
}

TEST(ButtonChecks, ADisabledButtonDoesNotRunTheHandler) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    button.set_disabled(true);
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 0u);
    EXPECT_FALSE(event.handled);
}

TEST(ButtonChecks, APressWithNoHandlerIsStillClaimed) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    // act
    button.dispatch_event(event);
    // assert the button swallowed the press, so nothing behind it reacts (§8.3)
    EXPECT_TRUE(event.handled);
}

TEST(ButtonChecks, AKeyPressIsNotClaimed) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::KeyDown};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 0u);
    EXPECT_FALSE(event.handled);
}

TEST(ButtonChecks, ReturnActivatesTheButton) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    button.dispatch_event(event);
    // assert the return key is a press for the focused button (§8.3)
    EXPECT_EQ(clicks, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(ButtonChecks, SpaceActivatesTheButton) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(ButtonChecks, AModifiedKeyDoesNotActivateTheButton) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    event.modifiers.command = true;
    // act
    button.dispatch_event(event);
    // assert the command return is an application shortcut, so the button stays out of it (§8.3)
    EXPECT_EQ(clicks, 0u);
    EXPECT_FALSE(event.handled);
}

TEST(ButtonChecks, ADisabledButtonDoesNotReactToTheKey) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    button.set_disabled(true);
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 0u);
    EXPECT_FALSE(event.handled);
}

TEST(ButtonChecks, AHiddenButtonIgnoresTheKey) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    button.set_visible(false);
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    button.dispatch_event(event);
    // assert
    EXPECT_EQ(clicks, 0u);
    EXPECT_FALSE(event.handled);
}

TEST(ButtonChecks, AKeyWithNoHandlerIsStillClaimed) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{50.0f, 20.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::Return};
    // act
    button.dispatch_event(event);
    // assert the button swallowed the key, so nothing behind it reacts (§8.3)
    EXPECT_TRUE(event.handled);
}

TEST(ButtonChecks, SetLabelRemeasures) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "a");
    Size before{};
    button.measure(SizeConstraints::unconstrained(), before);
    button.clear_dirty();
    // act
    button.set_label("a much longer label");
    // assert
    EXPECT_EQ(button.label(), "a much longer label");
    EXPECT_TRUE(button.layout_dirty());
}

TEST(ButtonChecks, AnUnchangedLabelInvalidatesNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "same");
    button.clear_dirty();
    // act
    button.set_label("same");
    // assert
    EXPECT_FALSE(button.layout_dirty());
}

TEST(ButtonChecks, SetDisabledRedraws) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.clear_dirty();
    // act
    button.set_disabled(true);
    // assert
    EXPECT_TRUE(button.render_dirty());
}

TEST(ButtonChecks, AnUnchangedDisabledStateRedrawsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_disabled(true);
    button.clear_dirty();
    // act
    button.set_disabled(true);
    // assert
    EXPECT_FALSE(button.render_dirty());
}

TEST(ButtonChecks, SetBackgroundRedraws) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.clear_dirty();
    // act
    button.set_background(Color::rgb(0.1f, 0.2f, 0.3f));
    // assert
    EXPECT_EQ(button.background(), Color::rgb(0.1f, 0.2f, 0.3f));
    EXPECT_TRUE(button.render_dirty());
}

TEST(ButtonChecks, AnUnchangedBackgroundRedrawsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.clear_dirty();
    // act
    button.set_background(button.background());
    // assert
    EXPECT_FALSE(button.render_dirty());
}

TEST(ButtonChecks, AnnouncesTheButtonRoleAndItsLabel) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press", "press_button");
    button.arrange(Rect::from_origin_size(Point{5.0f, 6.0f}, Size{80.0f, 30.0f}));
    // act
    const SemanticNode node = button.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Button);
    EXPECT_EQ(node.name(), "Press");
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{5.0f, 6.0f}, Size{80.0f, 30.0f})));
    EXPECT_FALSE(node.disabled());
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
}

TEST(ButtonChecks, ExposesTheActivateAction) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    // act
    const SemanticNode node = button.semantic_node();
    const bool invoked = node.invoke("activate");
    // assert
    EXPECT_TRUE(invoked);
    EXPECT_EQ(clicks, 1u);
}

TEST(ButtonChecks, ADisabledButtonRefusesTheActivateAction) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_disabled(true);
    std::size_t clicks = 0;
    button.set_on_click([&clicks] { clicks++; });
    // act
    const SemanticNode node = button.semantic_node();
    const bool invoked = node.invoke("activate");
    // assert the disabled state refuses the action before the widget body runs, which is what §10.1 requires
    EXPECT_FALSE(invoked);
    EXPECT_EQ(clicks, 0u);
    EXPECT_TRUE(node.disabled());
    EXPECT_TRUE(node.has_action("activate"));
}

TEST(ButtonChecks, TheActivateActionWithNoHandlerDoesNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act
    const SemanticNode node = button.semantic_node();
    const bool invoked = node.invoke("activate");
    // assert
    EXPECT_TRUE(invoked);
}

TEST(CheckBoxChecks, MeasuresToTheControlSquare) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    Size measured{};
    // act
    box.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, measured.height);
    EXPECT_GT(measured.width, 0.0f);
}

TEST(CheckBoxChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    Size measured{};
    // act
    box.measure(SizeConstraints::at_most(Size{4.0f, 4.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 4.0f);
    EXPECT_FLOAT_EQ(measured.height, 4.0f);
}

TEST(CheckBoxChecks, RendersTheOutline) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert an unchecked box strokes only, so nothing is filled (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_FALSE(box.checked());
}

TEST(CheckBoxChecks, ACheckedBoxAlsoFills) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.set_checked(true);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert the fill sits under the outline, so two rectangles are recorded (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 2u);
}

TEST(CheckBoxChecks, AnUnarrangedBoxDrawsNothing) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
}

TEST(CheckBoxChecks, HitsInsideItsBounds) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    // act
    const bool hit = box.hit_test(Point{5.0f, 5.0f});
    // assert
    EXPECT_TRUE(hit);
}

TEST(CheckBoxChecks, DoesNotHitOutsideItsBounds) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    // act
    const bool hit = box.hit_test(Point{50.0f, 50.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(CheckBoxChecks, AnUnarrangedBoxIsNotAHit) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    // act
    const bool hit = box.hit_test(Point{0.0f, 0.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(CheckBoxChecks, AHiddenBoxIsNotAHit) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    box.set_visible(false);
    // act
    const bool hit = box.hit_test(Point{5.0f, 5.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(CheckBoxChecks, ADisabledBoxIsNotAHit) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    box.set_disabled(true);
    // act
    const bool hit = box.hit_test(Point{5.0f, 5.0f});
    // assert
    EXPECT_FALSE(hit);
    EXPECT_TRUE(box.disabled());
}

TEST(CheckBoxChecks, APressTogglesTheState) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{5.0f, 5.0f}};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_TRUE(box.checked());
    EXPECT_TRUE(event.handled);
}

TEST(CheckBoxChecks, APressFlipsTheStateBack) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.set_checked(true);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{5.0f, 5.0f}};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_FALSE(box.checked());
}

TEST(CheckBoxChecks, TheToggleHandlerReceivesTheCheckedState) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    bool reported = false;
    box.set_on_toggle([&reported](bool state) { reported = state; });
    Event event{.kind = EventKind::PointerDown, .position = Point{5.0f, 5.0f}};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_TRUE(reported);
}

TEST(CheckBoxChecks, ADisabledBoxDoesNotToggle) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    box.set_disabled(true);
    Event event{.kind = EventKind::PointerDown, .position = Point{5.0f, 5.0f}};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_FALSE(box.checked());
    EXPECT_FALSE(event.handled);
}

TEST(CheckBoxChecks, AKeyPressIsNotClaimed) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_FALSE(event.handled);
    EXPECT_FALSE(box.checked());
}

TEST(CheckBoxChecks, SpaceTogglesTheBox) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    std::size_t toggles = 0;
    box.set_on_toggle([&toggles](bool) { toggles++; });
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    box.dispatch_event(event);
    // assert the space key is a press for the focused box (§8.3)
    EXPECT_TRUE(box.checked());
    EXPECT_EQ(toggles, 1u);
    EXPECT_TRUE(event.handled);
}

TEST(CheckBoxChecks, SpaceFlipsTheCheckedStateBack) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.set_checked(true);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_FALSE(box.checked());
    EXPECT_TRUE(event.handled);
}

TEST(CheckBoxChecks, AModifiedSpaceDoesNotToggleTheBox) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    event.modifiers.control = true;
    // act
    box.dispatch_event(event);
    // assert the control space is an application shortcut, so the box stays out of it (§8.3)
    EXPECT_FALSE(box.checked());
    EXPECT_FALSE(event.handled);
}

TEST(CheckBoxChecks, ADisabledBoxIgnoresTheSpaceKey) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    box.set_disabled(true);
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_FALSE(box.checked());
    EXPECT_FALSE(event.handled);
}

TEST(CheckBoxChecks, AHiddenBoxIgnoresTheSpaceKey) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    box.set_visible(false);
    Event event{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    box.dispatch_event(event);
    // assert
    EXPECT_FALSE(box.checked());
    EXPECT_FALSE(event.handled);
}

TEST(CheckBoxChecks, SetCheckedRedraws) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.clear_dirty();
    // act
    box.set_checked(true);
    // assert
    EXPECT_TRUE(box.render_dirty());
    EXPECT_TRUE(box.checked());
}

TEST(CheckBoxChecks, AnUnchangedCheckedStateRedrawsNothing) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.set_checked(true);
    box.clear_dirty();
    // act
    box.set_checked(true);
    // assert
    EXPECT_FALSE(box.render_dirty());
}

TEST(CheckBoxChecks, SetDisabledRedraws) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.clear_dirty();
    // act
    box.set_disabled(true);
    // assert
    EXPECT_TRUE(box.render_dirty());
}

TEST(CheckBoxChecks, AnUnchangedDisabledStateRedrawsNothing) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.set_disabled(true);
    box.clear_dirty();
    // act
    box.set_disabled(true);
    // assert
    EXPECT_FALSE(box.render_dirty());
}

TEST(CheckBoxChecks, SetOutlineRedraws) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.clear_dirty();
    // act
    box.set_outline(Color::rgb(1.0f, 0.0f, 0.0f));
    // assert
    EXPECT_EQ(box.outline(), Color::rgb(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(box.render_dirty());
}

TEST(CheckBoxChecks, AnUnchangedOutlineRedrawsNothing) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.clear_dirty();
    // act
    box.set_outline(box.outline());
    // assert
    EXPECT_FALSE(box.render_dirty());
}

TEST(CheckBoxChecks, SetCheckFillRedraws) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.clear_dirty();
    // act
    box.set_check_fill(Color::rgb(0.0f, 1.0f, 0.0f));
    // assert
    EXPECT_TRUE(box.render_dirty());
}

TEST(CheckBoxChecks, AnUnchangedCheckFillRedrawsNothing) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.clear_dirty();
    // act
    box.set_check_fill(Color::rgba(0.20f, 0.55f, 0.90f, 1.0f));
    // assert
    EXPECT_FALSE(box.render_dirty());
}

TEST(CheckBoxChecks, AnnouncesTheCheckboxRoleAndItsIdentity) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "agree");
    box.arrange(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{16.0f, 16.0f}));
    // act
    const SemanticNode node = box.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Checkbox);
    EXPECT_EQ(node.name(), "agree");
    EXPECT_EQ(node.check(), CheckState::Unchecked);
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{16.0f, 16.0f})));
}

TEST(CheckBoxChecks, TheCheckStateFollowsTheStoredState) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "agree");
    box.set_checked(true);
    // act
    const SemanticNode node = box.semantic_node();
    // assert
    EXPECT_EQ(node.check(), CheckState::Checked);
    EXPECT_TRUE(node.checked());
}

TEST(CheckBoxChecks, ExposesTheToggleAction) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "agree");
    // act
    const SemanticNode node = box.semantic_node();
    const bool invoked = node.invoke("toggle");
    // assert
    EXPECT_TRUE(invoked);
    EXPECT_TRUE(box.checked());
}

TEST(CheckBoxChecks, ADisabledBoxRefusesTheToggleAction) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "agree");
    box.set_disabled(true);
    // act
    const SemanticNode node = box.semantic_node();
    const bool invoked = node.invoke("toggle");
    // assert the disabled state refuses the action, so the stored state never moves (§10.1)
    EXPECT_FALSE(invoked);
    EXPECT_FALSE(box.checked());
    EXPECT_TRUE(node.disabled());
    EXPECT_TRUE(node.has_action("toggle"));
}

TEST(CheckBoxChecks, TheToggleActionReportsTheUncheckedState) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "agree");
    bool reported = true;
    box.set_on_toggle([&reported](bool state) { reported = state; });
    // act
    const SemanticNode node = box.semantic_node();
    (void)node.invoke("toggle");
    // assert invoking the action takes the same path a press does, so the handler hears the new state (§10.1)
    EXPECT_TRUE(reported);
    EXPECT_TRUE(box.checked());
}

TEST(CheckBoxChecks, TheToggleActionWithNoHandlerStillToggles) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "agree");
    // act
    const SemanticNode node = box.semantic_node();
    (void)node.invoke("toggle");
    // assert
    EXPECT_TRUE(box.checked());
}

TEST(SliderChecks, StartsAtZeroInTheDefaultRange) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    // act assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    EXPECT_FLOAT_EQ(slider.minimum(), 0.0f);
    EXPECT_FLOAT_EQ(slider.maximum(), 1.0f);
}

TEST(SliderChecks, MeasuresToAWideShortControl) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    Size measured{};
    // act
    slider.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_GT(measured.width, measured.height);
}

TEST(SliderChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    Size measured{};
    // act
    slider.measure(SizeConstraints::at_most(Size{10.0f, 5.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(measured.height, 5.0f);
}

TEST(SliderChecks, RendersTheTrackAndTheThumb) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert the track and the thumb are two rectangles (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 2u);
}

TEST(SliderChecks, TheThumbMovesWithTheValue) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder low_recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    slider.render(low_recorder);
    slider.set_value(1.0f);
    SceneRecorder high_recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    slider.render(high_recorder);
    // assert the thumb is the second rectangle, so its x has to differ between the two values (§6.1)
    ASSERT_EQ(rect_commands(*high_recorder.scene()).size(), 2u);
    ASSERT_EQ(rect_commands(*low_recorder.scene()).size(), 2u);
    EXPECT_GT(rect_commands(*high_recorder.scene())[1].x, rect_commands(*low_recorder.scene())[1].x);
}

TEST(SliderChecks, AnUnarrangedSliderDrawsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
}

TEST(SliderChecks, ATrackWithNoLengthDrawsOnlyTheTrack) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert a track shorter than the thumb has no reachable position, so no thumb is drawn (§6.2)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(SliderChecks, HitsInsideItsBounds) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    // act
    const bool hit = slider.hit_test(Point{60.0f, 8.0f});
    // assert
    EXPECT_TRUE(hit);
}

TEST(SliderChecks, DoesNotHitOutsideItsBounds) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    // act
    const bool hit = slider.hit_test(Point{300.0f, 8.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(SliderChecks, AHiddenSliderIsNotAHit) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    slider.set_visible(false);
    // act
    const bool hit = slider.hit_test(Point{60.0f, 8.0f});
    // assert
    EXPECT_FALSE(hit);
}

TEST(SliderChecks, ADisabledSliderIsNotAHit) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    slider.set_disabled(true);
    // act
    const bool hit = slider.hit_test(Point{60.0f, 8.0f});
    // assert
    EXPECT_FALSE(hit);
    EXPECT_TRUE(slider.disabled());
}

TEST(SliderChecks, APressAtTheRightEndReportsTheMaximum) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{119.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 1.0f);
    EXPECT_TRUE(event.handled);
}

TEST(SliderChecks, APressAtTheLeftEndReportsTheMinimum) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_value(0.5f);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{0.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
}

TEST(SliderChecks, TheChangeHandlerReceivesTheNewValue) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    float reported = -1.0f;
    slider.set_on_change([&reported](float value) { reported = value; });
    Event event{.kind = EventKind::PointerDown, .position = Point{119.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(reported, 1.0f);
}

TEST(SliderChecks, ADisabledSliderDoesNotMove) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    slider.set_disabled(true);
    Event event{.kind = EventKind::PointerDown, .position = Point{119.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    EXPECT_FALSE(event.handled);
}

TEST(SliderChecks, AKeyPressIsNotClaimed) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FALSE(event.handled);
}

TEST(SliderChecks, AShortTrackDoesNotMove) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{4.0f, 16.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{2.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert the press is claimed but the track has no travel, so the value stays (§8)
    EXPECT_TRUE(event.handled);
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
}

TEST(SliderChecks, TheRightArrowStepsTheValueUp) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    // act
    slider.dispatch_event(event);
    // assert the step is the fixed amount of §8, so the key moved the value once (§8.3)
    EXPECT_FLOAT_EQ(slider.value(), 0.05f);
    EXPECT_TRUE(event.handled);
}

TEST(SliderChecks, TheLeftArrowStepsTheValueDown) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_value(0.5f);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.45f);
    EXPECT_TRUE(event.handled);
}

TEST(SliderChecks, TheUpArrowStepsTheValueUp) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.05f);
    EXPECT_TRUE(event.handled);
}

TEST(SliderChecks, TheDownArrowStepsTheValueDown) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_value(0.5f);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.45f);
    EXPECT_TRUE(event.handled);
}

TEST(SliderChecks, AModifiedArrowKeyDoesNotStepTheValue) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    event.modifiers.command = true;
    // act
    slider.dispatch_event(event);
    // assert the command arrow is an application shortcut, so the slider stays out of it (§8.3)
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    EXPECT_FALSE(event.handled);
}

TEST(SliderChecks, ADisabledSliderIgnoresTheArrowKey) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    slider.set_disabled(true);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    EXPECT_FALSE(event.handled);
}

TEST(SliderChecks, AHiddenSliderIgnoresTheArrowKey) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    slider.set_visible(false);
    Event event{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    EXPECT_FALSE(event.handled);
}

TEST(SliderChecks, ADragMovesTheValueWithTheMove) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{60.0f, 8.0f}};
    slider.dispatch_event(press);
    const float pressed = slider.value();
    Event move{.kind = EventKind::PointerMove, .position = Point{119.0f, 8.0f}};
    // act
    slider.dispatch_event(move);
    // assert the thumb follows the pointer across the drag, which is what dragging means (§8)
    EXPECT_TRUE(move.handled);
    EXPECT_GT(slider.value(), pressed);
    EXPECT_FLOAT_EQ(slider.value(), 1.0f);
}

TEST(SliderChecks, AReleaseEndsTheDrag) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{60.0f, 8.0f}};
    slider.dispatch_event(press);
    Event release{.kind = EventKind::PointerUp, .position = Point{60.0f, 8.0f}};
    slider.dispatch_event(release);
    const float settled = slider.value();
    Event move{.kind = EventKind::PointerMove, .position = Point{119.0f, 8.0f}};
    // act
    slider.dispatch_event(move);
    // assert the released drag is over, so the next move belongs to whoever asks (§8.3)
    EXPECT_TRUE(release.handled);
    EXPECT_FALSE(move.handled);
    EXPECT_FLOAT_EQ(slider.value(), settled);
}

TEST(SliderChecks, AMoveWithoutAPressDoesNotSteerTheSlider) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    Event move{.kind = EventKind::PointerMove, .position = Point{119.0f, 8.0f}};
    // act
    slider.dispatch_event(move);
    // assert a hover is not a drag, so the value stays where it was (§8.3)
    EXPECT_FALSE(move.handled);
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
}

TEST(SliderChecks, SetValueClampsIntoTheRange) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    // act
    slider.set_value(5.0f);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 1.0f);
    slider.set_value(-5.0f);
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
}

TEST(SliderChecks, AnUnchangedValueRedrawsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.clear_dirty();
    // act
    slider.set_value(0.0f);
    // assert
    EXPECT_FALSE(slider.render_dirty());
}

TEST(SliderChecks, SetRangeReclampsTheValue) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_value(0.9f);
    slider.clear_dirty();
    // act
    slider.set_range(0.0f, 0.5f);
    // assert a new range can exclude the value the slider had (§8)
    EXPECT_FLOAT_EQ(slider.value(), 0.5f);
    EXPECT_FLOAT_EQ(slider.minimum(), 0.0f);
    EXPECT_FLOAT_EQ(slider.maximum(), 0.5f);
    EXPECT_TRUE(slider.render_dirty());
}

TEST(SliderChecks, ADescendingRangeIsRefused) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    // act
    slider.set_range(1.0f, 0.0f);
    // assert a range that does not ascend has no valid positions (§8)
    EXPECT_FLOAT_EQ(slider.minimum(), 0.0f);
    EXPECT_FLOAT_EQ(slider.maximum(), 1.0f);
}

TEST(SliderChecks, AnUnchangedRangeDoesNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.clear_dirty();
    // act
    slider.set_range(0.0f, 1.0f);
    // assert
    EXPECT_FALSE(slider.render_dirty());
}

TEST(SliderChecks, StepMovesTheValueUp) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    // act
    slider.step(0.25f);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.25f);
}

TEST(SliderChecks, StepMovesTheValueDown) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_value(0.5f);
    // act
    slider.step(-0.25f);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.25f);
}

TEST(SliderChecks, StepClampsAtTheRangeEdges) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    // act
    slider.step(-1.0f);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    slider.step(5.0f);
    EXPECT_FLOAT_EQ(slider.value(), 1.0f);
}

TEST(SliderChecks, StepReportsTheChangeToTheHandler) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    float reported = -1.0f;
    slider.set_on_change([&reported](float value) { reported = value; });
    // act
    slider.step(0.5f);
    // assert
    EXPECT_FLOAT_EQ(reported, 0.5f);
}

TEST(SliderChecks, StepPastTheEndReportsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_value(1.0f);
    std::size_t changes = 0;
    slider.set_on_change([&changes](float) { changes++; });
    // act
    slider.step(0.5f);
    // assert the value is already at the top, so nothing moved (§8.3)
    EXPECT_EQ(changes, 0u);
}

TEST(SliderChecks, ADisabledSliderRefusesToStep) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_disabled(true);
    // act
    slider.step(0.5f);
    // assert
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
}

TEST(SliderChecks, SetTrackColorRedraws) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.clear_dirty();
    // act
    slider.set_track_color(Color::rgb(0.1f, 0.1f, 0.1f));
    // assert
    EXPECT_TRUE(slider.render_dirty());
}

TEST(SliderChecks, SetThumbColorRedraws) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.clear_dirty();
    // act
    slider.set_thumb_color(Color::rgb(0.1f, 0.1f, 0.1f));
    // assert
    EXPECT_TRUE(slider.render_dirty());
}

TEST(SliderChecks, AnnouncesTheSliderRoleAndItsValue) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine, "volume");
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    // act
    const SemanticNode node = slider.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Slider);
    EXPECT_EQ(node.name(), "volume 0.000000");
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
    EXPECT_FALSE(node.disabled());
}

TEST(SliderChecks, TheAnnouncedNameFollowsTheValue) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine, "volume");
    slider.set_value(0.5f);
    // act
    const SemanticNode node = slider.semantic_node();
    // assert a screen reader has to hear the number, so the name carries it (§10.1)
    EXPECT_EQ(node.name(), "volume 0.500000");
}

TEST(SliderChecks, ExposesTheIncrementAction) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine, "volume");
    // act
    const SemanticNode node = slider.semantic_node();
    const bool invoked = node.invoke("increment");
    // assert
    EXPECT_TRUE(invoked);
    EXPECT_GT(slider.value(), 0.0f);
}

TEST(SliderChecks, ExposesTheDecrementAction) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine, "volume");
    slider.set_value(0.5f);
    // act
    const SemanticNode node = slider.semantic_node();
    const bool invoked = node.invoke("decrement");
    // assert
    EXPECT_TRUE(invoked);
    EXPECT_LT(slider.value(), 0.5f);
}

TEST(SliderChecks, TheStepActionsOfADisabledSliderDoNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine, "volume");
    slider.set_disabled(true);
    // act
    const SemanticNode node = slider.semantic_node();
    const bool invoked = node.invoke("increment");
    // assert the disabled state refuses the action, so the value never moves (§10.1)
    EXPECT_FALSE(invoked);
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
    EXPECT_TRUE(node.disabled());
    EXPECT_TRUE(node.has_action("increment"));
    EXPECT_TRUE(node.has_action("decrement"));
}

TEST(CanvasChecks, StartsWithNothingToDraw) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    // act assert
    EXPECT_FALSE(canvas.draws());
    EXPECT_EQ(canvas.draw_count(), 0u);
}

TEST(CanvasChecks, MeasuresToTheDeclaredExtent) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.set_intrinsic_size(Size{40.0f, 30.0f});
    Size measured{};
    // act
    canvas.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 30.0f);
}

TEST(CanvasChecks, AnUndeclaredCanvasMeasuresAsNothing) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    Size measured{};
    // act
    canvas.measure(SizeConstraints::unconstrained(), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(CanvasChecks, ClampsIntoTheOfferedConstraints) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.set_intrinsic_size(Size{40.0f, 30.0f});
    Size measured{};
    // act
    canvas.measure(SizeConstraints::at_most(Size{10.0f, 10.0f}), measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 10.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
}

TEST(CanvasChecks, TheDrawCallbackRecordsIntoTheContext) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 30.0f}));
    canvas.set_draw([](RenderContext& context) { context.draw_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{10.0f, 10.0f}), {}); });
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    canvas.render(recorder);
    // assert
    EXPECT_TRUE(canvas.draws());
    EXPECT_EQ(canvas.draw_count(), 1u);
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(CanvasChecks, AnEmptyCanvasRecordsNothing) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    canvas.render(recorder);
    // assert the pass is counted even with no callback, so a test can tell it was offered (§6.4)
    EXPECT_EQ(canvas.draw_count(), 1u);
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
}

TEST(CanvasChecks, TheDrawCallbackRunsOnEveryFrame) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.set_draw([](RenderContext&) {});
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    canvas.render(recorder);
    canvas.render(recorder);
    // assert
    EXPECT_EQ(canvas.draw_count(), 2u);
}

TEST(CanvasChecks, SetDrawRedraws) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.clear_dirty();
    // act
    canvas.set_draw([](RenderContext&) {});
    // assert
    EXPECT_TRUE(canvas.render_dirty());
}

TEST(CanvasChecks, SetIntrinsicSizeRemeasures) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.clear_dirty();
    // act
    canvas.set_intrinsic_size(Size{10.0f, 10.0f});
    // assert
    EXPECT_TRUE(canvas.layout_dirty());
    EXPECT_EQ(canvas.intrinsic_size(), (Size{10.0f, 10.0f}));
}

TEST(CanvasChecks, AnUnchangedIntrinsicSizeInvalidatesNothing) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.set_intrinsic_size(Size{10.0f, 10.0f});
    canvas.clear_dirty();
    // act
    canvas.set_intrinsic_size(Size{10.0f, 10.0f});
    // assert
    EXPECT_FALSE(canvas.layout_dirty());
}

TEST(CanvasChecks, AnnouncesTheCanvasRoleAndItsIdentity) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine, "chart");
    canvas.set_draw([](RenderContext&) {});
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 30.0f}));
    // act
    const SemanticNode node = canvas.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Canvas);
    EXPECT_EQ(node.name(), "chart");
    EXPECT_FALSE(node.disabled());
}

TEST(CanvasChecks, AnEmptyCanvasAnnouncesAsDisabled) {
    // arrange
    BindingEngine engine;
    Canvas canvas(&engine, "chart");
    // act
    const SemanticNode node = canvas.semantic_node();
    // assert there is nothing to interact with, so the canvas reports itself unavailable (§10.1)
    EXPECT_TRUE(node.disabled());
}

TEST(CanvasChecks, AWidgetTreeWalksIntoTheSemanticTree) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    auto panel = std::make_unique<WidgetTestHost>();
    auto button = std::make_unique<Button>(&engine, &text_engine, "Press", "press_button");
    StubTextEngine stub_text;
    auto label = std::make_unique<Label>(&engine, &stub_text, "Count", "count_label");
    Button* button_ptr = button.get();
    Label* label_ptr = label.get();
    panel->add_child(std::move(button));
    panel->add_child(std::move(label));
    panel->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 100.0f}));
    SemanticTree tree;
    // the widget is wrapped by hand here, because a widget is a provider only through the semantic view (§10.1, AD-09)
    SemanticNodeProvider root_provider(button_ptr->semantic_node());
    // act
    tree.build_from(root_provider);
    // assert the walk reaches the widgets by way of the host child, which announces nothing (§10.1, AD-09)
    EXPECT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree.count_role(SemanticRole::Button), 1u);
    ASSERT_NE(tree.find("Press"), nullptr);
    EXPECT_EQ(label_ptr->semantic_node().name(), "Count");
}

TEST(LabelChecks, HandsBackTheMeasuredRun) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    // act
    const TextLayout run = label.layout();
    // assert the render pass draws exactly the run the measure pass produced (§9.1)
    ASSERT_NE(run.run, nullptr);
    EXPECT_EQ(run.run->glyph_count(), 5u);
    EXPECT_GT(run.size.width, 0.0f);
}

TEST(LabelChecks, TheMeasuredRunFollowsAFontSizeChange) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    Size measured{};
    label.measure(SizeConstraints::unconstrained(), measured);
    // act
    label.set_font_size(48.0f);
    // assert the run is remeasured eagerly, so the layout the render pass draws is already current (§9.1)
    ASSERT_NE(label.layout().run, nullptr);
    EXPECT_EQ(label.layout().run->glyph_count(), 5u);
    EXPECT_GT(label.layout().size.height, 0.0f);
}

TEST(LabelChecks, AHiddenLabelStopsRecording) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    Size measured{};
    label.measure(SizeConstraints::unconstrained(), measured);
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 20.0f}));
    label.set_visible(false);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    label.render(recorder);
    // assert a hidden node is skipped by stage one, so the scene builder never reaches its render (§6.1)
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
}

TEST(LabelChecks, AWidgetChildIsWrappedInANonOwningView) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    StubTextEngine stub_text;
    button.add_child(std::make_unique<Label>(&engine, &stub_text, "inner"));
    // act
    std::unique_ptr<SemanticProvider> child = button.semantic_child(0);
    // assert the view keeps the ui tree the only owner, so no subtree is copied for the walk (§10.1, AD-09)
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::Label);
    EXPECT_EQ(child->semantic_node().name(), "inner");
}

TEST(LabelChecks, AViewWithoutAProviderAnnouncesNothing) {
    // arrange
    SemanticProviderView view(nullptr);
    // act
    const SemanticNode node = view.semantic_node();
    const std::size_t count = view.semantic_child_count();
    std::unique_ptr<SemanticProvider> child = view.semantic_child(0);
    // assert a view with no provider must not dereference null, so it answers as an empty leaf (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::None);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(child, nullptr);
}

TEST(LabelChecks, AViewForwardsToTheProviderItWatches) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    SemanticProviderView view(&button);
    // act
    const SemanticNode node = view.semantic_node();
    const std::size_t count = view.semantic_child_count();
    // assert the watched provider answers for itself, because a view carries no state (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::Button);
    EXPECT_EQ(count, 0u);
}

TEST(LabelChecks, AViewReportsNoChildPastTheEnd) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    SemanticProviderView view(&button);
    // act
    std::unique_ptr<SemanticProvider> child = view.semantic_child(3);
    // assert the watched provider has no such child, so the view reports none (§10.1, AD-09)
    EXPECT_EQ(child, nullptr);
}

TEST(LabelChecks, AViewForwardsAChildThatExists) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    StubTextEngine stub_text;
    button.add_child(std::make_unique<Label>(&engine, &stub_text, "inner"));
    SemanticProviderView view(&button);
    // act
    std::unique_ptr<SemanticProvider> child = view.semantic_child(0);
    // assert
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::Label);
}

TEST(LabelChecks, TheNodeProviderAnnouncesItsFixedNode) {
    // arrange
    SemanticNodeProvider provider(SemanticNode{SemanticRole::Group});
    // act
    const SemanticNode node = provider.semantic_node();
    const std::size_t count = provider.semantic_child_count();
    std::unique_ptr<SemanticProvider> child = provider.semantic_child(0);
    // assert
    EXPECT_EQ(node.role(), SemanticRole::Group);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(child, nullptr);
}

TEST(SliderChecks, APressOutsideTheTrackIsNotClaimed) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    std::size_t changes = 0;
    slider.set_on_change([&changes](float) { changes++; });
    Event event{.kind = EventKind::PointerDown, .position = Point{400.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert a press beyond the track belongs to whatever is behind the slider (§8.3)
    EXPECT_FALSE(event.handled);
    EXPECT_EQ(changes, 0u);
    EXPECT_FLOAT_EQ(slider.value(), 0.0f);
}

TEST(SliderChecks, APressAtTheCurrentValueReportsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    // the thumb centre at zero sits at the far left, so pressing it again changes nothing (§8.3)
    std::size_t changes = 0;
    slider.set_on_change([&changes](float) { changes++; });
    Event event{.kind = EventKind::PointerDown, .position = Point{6.0f, 8.0f}};
    // act
    slider.dispatch_event(event);
    // assert
    EXPECT_TRUE(event.handled);
    EXPECT_EQ(changes, 0u);
}

TEST(CheckBoxChecks, APressOutsideTheBoxIsNotClaimed) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    std::size_t toggles = 0;
    box.set_on_toggle([&toggles](bool) { toggles++; });
    Event event{.kind = EventKind::PointerDown, .position = Point{200.0f, 200.0f}};
    // act
    box.dispatch_event(event);
    // assert a press beyond the box belongs to whatever is behind it (§8.3)
    EXPECT_FALSE(event.handled);
    EXPECT_FALSE(box.checked());
    EXPECT_EQ(toggles, 0u);
}

TEST(SliderChecks, AnUnchangedDisabledStateRedrawsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_disabled(true);
    slider.clear_dirty();
    // act
    slider.set_disabled(true);
    // assert
    EXPECT_FALSE(slider.render_dirty());
}

TEST(SliderChecks, AnUnchangedTrackColorRedrawsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_track_color(Color::rgb(0.10f, 0.20f, 0.30f));
    slider.clear_dirty();
    // act
    slider.set_track_color(Color::rgb(0.10f, 0.20f, 0.30f));
    // assert
    EXPECT_FALSE(slider.render_dirty());
}

TEST(SliderChecks, AnUnchangedThumbColorRedrawsNothing) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine);
    slider.set_thumb_color(Color::rgb(0.40f, 0.50f, 0.60f));
    slider.clear_dirty();
    // act
    slider.set_thumb_color(Color::rgb(0.40f, 0.50f, 0.60f));
    // assert
    EXPECT_FALSE(slider.render_dirty());
}

TEST(ButtonChecks, ADisabledButtonStillDrawsItsPlateAndLabel) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_disabled(true);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert a disabled control stays on screen, only faded, so it still records both commands (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
}

TEST(ButtonChecks, ADisabledAndUnlabelledButtonStillDrawsItsPlate) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine);
    button.set_disabled(true);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 30.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate is recorded before the empty run is skipped (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_TRUE(text_commands(*recorder.scene()).empty());
}

TEST(LabelChecks, AZeroWidthOfferMeasuresAsNothing) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    // a parent that offers no width at all is a legitimate constraint, not an unbounded one (§8.2)
    SizeConstraints no_width;
    no_width.maximum = Size{0.0f, 0.0f};
    Size measured{};
    // act
    label.measure(no_width, measured);
    // assert
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(LabelChecks, ALabelWithNoEngineStaysEmpty) {
    // arrange
    BindingEngine engine;
    // the engine is absent, which is what a scene built before one was installed looks like (§9.1)
    Label label(&engine, nullptr, "Albor");
    Size measured{};
    // act
    label.measure(SizeConstraints::unconstrained(), measured);
    // assert with nothing to measure through, the label invents no extent (§9.1)
    EXPECT_TRUE(label.layout().empty());
    EXPECT_FLOAT_EQ(measured.width, 0.0f);
    EXPECT_FLOAT_EQ(measured.height, 0.0f);
}

TEST(LabelChecks, ALabelWithNoEngineSurvivesATextChange) {
    // arrange
    BindingEngine engine;
    Label label(&engine, nullptr, "Albor");
    label.clear_dirty();
    // act
    label.set_text("a much longer run of text");
    // assert the change is still announced, because the accessible name changed even without a measurement (§10.1)
    EXPECT_EQ(label.text(), "a much longer run of text");
    EXPECT_TRUE(label.layout().empty());
    EXPECT_TRUE(label.layout_dirty());
}

TEST(LabelChecks, ALabelWithNoEngineSurvivesAFontSizeChange) {
    // arrange
    BindingEngine engine;
    Label label(&engine, nullptr, "Albor");
    label.clear_dirty();
    // act
    label.set_font_size(32.0f);
    // assert the requested size is remembered, so a later install would measure against it (§9.1)
    EXPECT_FLOAT_EQ(label.font_size(), 32.0f);
    EXPECT_TRUE(label.layout().empty());
}

TEST(LabelChecks, ALabelWithNoEngineDrawsNothing) {
    // arrange
    BindingEngine engine;
    Label label(&engine, nullptr, "Albor");
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    label.render(recorder);
    // assert an empty run records nothing, so nothing reaches the render stage (§6.2)
    EXPECT_TRUE(text_commands(*recorder.scene()).empty());
}

TEST(LabelChecks, TheLabelAsksTheEngineToMeasure) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    const std::size_t before = text_engine.measurements;
    Size measured{};
    // act
    label.measure(SizeConstraints::unconstrained(), measured);
    // assert the label measures through the engine it was given rather than computing an extent itself (§9.1)
    EXPECT_GT(text_engine.measurements, before);
}

TEST(WidgetCompositionChecks, AWidgetsChildIsMeasured) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    auto child = std::make_unique<WidgetChildNode>();
    child->m_intrinsic = Size{120.0f, 90.0f};
    WidgetChildNode& node = *child;
    button.add_child(std::move(child));
    // act
    Size measured{};
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert a widget is a component, so it measures for its children as well as for its own content (§8.1, §8.2)
    EXPECT_GE(measured.width, 120.0f);
    EXPECT_GE(measured.height, 90.0f);
    EXPECT_EQ(node.bounds().width, 0.0f);
}

TEST(WidgetCompositionChecks, AWidgetsChildIsArranged) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    auto child = std::make_unique<WidgetChildNode>();
    WidgetChildNode& node = *child;
    button.add_child(std::move(child));
    // act
    button.arrange(Rect::from_origin_size(Point{5.0f, 6.0f}, Size{60.0f, 40.0f}));
    // assert the child is placed, because the widget arranges through the component contract (§8.2)
    EXPECT_EQ(node.bounds().width, 60.0f);
    EXPECT_EQ(node.bounds().height, 40.0f);
}

TEST(WidgetCompositionChecks, AWidgetsChildIsRecordedAfterTheWidgetsOwnContent) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.add_child(std::make_unique<WidgetChildNode>());
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 40.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the composite order is background, then the widget content, then the children (§6.1)
    ASSERT_EQ(rect_commands(*recorder.scene()).size(), 2u);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(recorder.scene()->command_count(), 3u);
    EXPECT_EQ(recorder.scene()->commands()[0].kind(), DrawCommandKind::Rect);
    EXPECT_EQ(recorder.scene()->commands()[1].kind(), DrawCommandKind::Text);
    EXPECT_EQ(recorder.scene()->commands()[2].kind(), DrawCommandKind::Rect);
}

TEST(WidgetCompositionChecks, AWidgetsChildIsAHit) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.add_child(std::make_unique<WidgetChildNode>());
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 40.0f}));
    // act / assert the point lands on the child, which the component hit test walks (§8.3)
    EXPECT_TRUE(button.hit_test(Point{10.0f, 10.0f}));
}

TEST(WidgetCompositionChecks, AWidgetsChildIsReachedByTheSemanticWalk) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.add_child(std::make_unique<WidgetChildNode>());
    // act / assert the widget forwards its children to semantics, so a composite is announced whole (§10.1, AD-09)
    EXPECT_EQ(button.semantic_child_count(), 1u);
}

TEST(WidgetCompositionChecks, TheButtonPlateIsRecordedThroughTheBackgroundHook) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_background(Color::rgb(0.9f, 0.1f, 0.2f));
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate is recorded once, through the base hook the widget already overrode (§6.1, §6.2)
    ASSERT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().fill, (Color::rgb(0.9f, 0.1f, 0.2f)));
}

TEST(WidgetCompositionChecks, ADisabledButtonFadesThroughTheBackgroundHook) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_background(Color::rgb(1.0f, 1.0f, 1.0f));
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    button.set_disabled(true);
    // act
    button.render(recorder);
    // assert the disabled fade is a plate colour, so the state reaches the frame through one path (§6.1)
    ASSERT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].paint().fill.red, 0.5f);
}

TEST(WidgetCompositionChecks, ACanvasWithAChildStillRunsItsCallback) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Canvas canvas(&engine, "canvas");
    std::size_t passes = 0;
    canvas.set_draw([&passes](RenderContext&) { passes++; });
    canvas.add_child(std::make_unique<WidgetChildNode>());
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 40.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    canvas.render(recorder);
    // assert the application callback still runs once, and the child records over it (§6.4)
    EXPECT_EQ(passes, 1u);
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(WidgetCompositionChecks, ATransparentWidgetBackgroundRecordsNothing) {
    // arrange a widget whose background was never set, which is the default (§6.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "");
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    label.render(recorder);
    // assert the transparent background is culled rather than recorded as a dead draw (§6.2)
    EXPECT_EQ(recorder.scene()->command_count(), 0u);
}

TEST(ThemeChecks, AThemeCarriesAPaletteAndMetrics) {
    // arrange
    Theme theme;
    // act, assert the theme answers for both halves of what a widget resolves (§6.1, §8.2)
    EXPECT_EQ(theme.palette().button_plate[state_index(ControlState::Normal)].red, 0.20f);
    EXPECT_FLOAT_EQ(theme.metrics().corner_radius, 6.0f);
}

TEST(ThemeChecks, AThemeOverrideReplacesOneEntry) {
    // arrange
    Theme theme;
    theme.set_button_plate(Color::rgb(0.9f, 0.1f, 0.2f));
    // act, assert the normal entry moved and the rest of the palette stayed (§6.1)
    EXPECT_FLOAT_EQ(theme.palette().button_plate[state_index(ControlState::Normal)].red, 0.9f);
    EXPECT_FLOAT_EQ(theme.palette().button_plate[state_index(ControlState::Pressed)].red, 0.14f);
}

TEST(ThemeChecks, AThemeOverrideReplacesOneStateOnly) {
    // arrange
    Theme theme;
    theme.set_button_plate(ControlState::Hovered, Color::rgb(0.0f, 1.0f, 0.0f));
    // act, assert the hovered entry moved and the normal one did not (§6.1)
    EXPECT_FLOAT_EQ(theme.palette().button_plate[state_index(ControlState::Hovered)].green, 1.0f);
    EXPECT_FLOAT_EQ(theme.palette().button_plate[state_index(ControlState::Normal)].red, 0.20f);
}

TEST(ThemeChecks, AWidgetHasNoThemeUntilOneIsInstalled) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act, assert a widget built without a theme resolves nothing (§6.1)
    EXPECT_EQ(button.theme(), nullptr);
}

TEST(ThemeChecks, AWidgetWithNoThemeDrawsItsOwnPlate) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_background(Color::rgb(1.0f, 0.0f, 0.0f));
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the own value wins while no theme is installed, so nothing changes for an existing app (§6.1)
    ASSERT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].paint().fill.red, 1.0f);
}

TEST(ThemeChecks, AThemeOverrideChangesThePaintWithoutTouchingTheWidget) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_button_plate(Color::rgb(0.9f, 0.1f, 0.2f));
    Button button(&engine, &text_engine, "Press");
    button.set_background(Color::rgb(1.0f, 0.0f, 0.0f));
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act the widget is untouched apart from the theme pointer (§6.1)
    button.render(recorder);
    // assert the theme won over the widget's own colour, which is the override direction the plan sets (§6.1)
    ASSERT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].paint().fill.red, 0.9f);
}

TEST(ThemeChecks, AThemedButtonMeasuresFromTheThemeMetrics) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_button_minimum(Size{120.0f, 40.0f});
    Button button(&engine, &text_engine, "");
    button.set_theme(&theme);
    Size measured{};
    // act
    button.measure(SizeConstraints::at_most(Size{400.0f, 400.0f}), measured);
    // assert the empty label still measured to the themed minimum, because the metric is the theme's (§8.2)
    EXPECT_GE(measured.width, 120.0f);
    EXPECT_GE(measured.height, 40.0f);
}

TEST(ThemeChecks, AnUnthemedButtonKeepsItsOwnMinimum) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "");
    Size measured{};
    // act
    button.measure(SizeConstraints::at_most(Size{400.0f, 400.0f}), measured);
    // assert the fallback metrics are the literals the widget used before, so its extent is unchanged (§8.2)
    EXPECT_GE(measured.width, 24.0f);
    EXPECT_LT(measured.width, 120.0f);
}

TEST(ControlStateChecks, AButtonIsNormalWhileThePointerIsElsewhere) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act, assert the resting state needs no pointer (§8.3)
    EXPECT_EQ(button.state(), ControlState::Normal);
    EXPECT_FALSE(button.hovered());
}

TEST(ControlStateChecks, AnEnterMakesAButtonHovered) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    Event enter{.kind = EventKind::PointerEnter, .position = Point{10.0f, 5.0f}};
    // act
    button.dispatch_event(enter);
    // assert the transition arrived as a state (§8.3)
    EXPECT_TRUE(button.hovered());
    EXPECT_EQ(button.state(), ControlState::Hovered);
}

TEST(ControlStateChecks, ALeaveMakesAButtonNormalAgain) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    Event enter{.kind = EventKind::PointerEnter, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(enter);
    Event leave{.kind = EventKind::PointerLeave, .position = Point{90.0f, 5.0f}};
    // act
    button.dispatch_event(leave);
    // assert
    EXPECT_FALSE(button.hovered());
    EXPECT_EQ(button.state(), ControlState::Normal);
}

TEST(ControlStateChecks, ADisabledButtonReportsDisabledOverAHover) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    Event enter{.kind = EventKind::PointerEnter, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(enter);
    button.set_disabled(true);
    // act, assert a control that refuses input reads disabled whatever the pointer does (§8.3)
    EXPECT_EQ(button.state(), ControlState::Disabled);
}

TEST(ControlStateChecks, AThemedButtonDrawsADifferentPlatePerState) {
    // arrange the three states a pointer can produce, read off three plates (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    // act each state is drawn through its own recorder, because a scene is immutable once built (§6.1)
    const float normal_red = [&] {
        SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
        button.render(recorder);
        return recorder.scene()->commands()[0].paint().fill.red;
    }();
    Event enter{.kind = EventKind::PointerEnter, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(enter);
    float hovered_red = 0.0f;
    {
        SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
        button.render(recorder);
        hovered_red = recorder.scene()->commands()[0].paint().fill.red;
    }
    // act the press lands after the hover, which is the order a pointer produces (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(press);
    float pressed_red = 0.0f;
    {
        SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
        button.render(recorder);
        pressed_red = recorder.scene()->commands()[0].paint().fill.red;
    }
    // assert three states are three colours, which is what a hover visual is (§6.1, §8.3)
    EXPECT_FLOAT_EQ(normal_red, 0.20f);
    EXPECT_FLOAT_EQ(hovered_red, 0.26f);
    EXPECT_FLOAT_EQ(pressed_red, 0.14f);
}

TEST(ControlStateChecks, AMoveOutsideBoundsReleasesAHeldPress) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(press);
    ASSERT_TRUE(button.pressed());
    Event move{.kind = EventKind::PointerMove, .position = Point{400.0f, 400.0f}};
    // act the pointer dragged off the plate, which is what dragging away means (§8.3)
    button.dispatch_event(move);
    // assert the press did not stay held on a control the pointer left (§8.3)
    EXPECT_FALSE(button.pressed());
}

TEST(ControlStateChecks, AMoveInsideBoundsKeepsAHeldPress) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(press);
    Event move{.kind = EventKind::PointerMove, .position = Point{30.0f, 10.0f}};
    // act
    button.dispatch_event(move);
    // assert a drag inside the control keeps the press, because the drag belongs to it (§8.3)
    EXPECT_TRUE(button.pressed());
}

TEST(ThemeChecks, ACheckBoxKeepsItsOwnExtentWithoutATheme) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "box");
    Size measured{};
    // act
    box.measure(SizeConstraints::at_most(Size{200.0f, 200.0f}), measured);
    // assert the fallback is the literal the box always measured to (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 16.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(ThemeChecks, AThemedCheckBoxMeasuresFromTheTheme) {
    // arrange a theme whose metric differs from the literal, so the answer can only come from the theme (§8.2)
    BindingEngine engine;
    CheckBox box(&engine, "box");
    Theme theme;
    theme.set_checkbox_box(Size{40.0f, 40.0f});
    box.set_theme(&theme);
    Size measured{};
    // act
    box.measure(SizeConstraints::at_most(Size{200.0f, 200.0f}), measured);
    // assert the theme answered rather than the literal (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 40.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(ThemeChecks, ACheckBoxStrokeWidthFallsBackToItsLiteral) {
    // arrange
    BindingEngine engine;
    CheckBox box(&engine, "box");
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    box.set_checked(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert the outline is the two point stroke the box always drew (§6.1)
    ASSERT_EQ(recorder.scene()->commands().size(), 2u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[1].paint().stroke_width, 2.0f);
}

TEST(ThemeChecks, ASliderKeepsItsOwnExtentWithoutATheme) {
    // arrange
    BindingEngine engine;
    Slider slider(&engine, "slider");
    Size measured{};
    // act
    slider.measure(SizeConstraints::at_most(Size{400.0f, 400.0f}), measured);
    // assert the fallback is the literal the slider always measured to (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(ThemeChecks, ASliderThumbStillFitsItsTrack) {
    // arrange the track and thumb metrics that both paths use (§6.1)
    BindingEngine engine;
    Slider slider(&engine, "slider");
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert the thumb is the twelve point square at the minimum value, which is the thumb's origin (§6.1)
    ASSERT_EQ(recorder.scene()->commands().size(), 2u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[1].rect_geometry().rect.width, 12.0f);
}

TEST(ThemeChecks, AThemedSliderDrawsTheThemedTrackAndThumb) {
    // arrange a theme whose metrics differ from the literals, so the answer can only come from the theme (§6.1)
    BindingEngine engine;
    Theme theme;
    theme.set_slider_track_thickness(8.0f);
    theme.set_slider_thumb(20.0f);
    Slider slider(&engine, "slider");
    slider.set_theme(&theme);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert the themed geometry answered, which is what makes the metric configurable (§6.1)
    ASSERT_EQ(recorder.scene()->commands().size(), 2u);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].rect_geometry().rect.height, 8.0f);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[1].rect_geometry().rect.width, 20.0f);
}

TEST(ThemeChecks, AThemedSliderTrackResolvesByState) {
    // arrange
    BindingEngine engine;
    Theme theme;
    Slider slider(&engine, "slider");
    slider.set_theme(&theme);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act the track is drawn first, so its colour is the resting one (§6.1)
    slider.render(recorder);
    const float normal_red = recorder.scene()->commands()[0].paint().fill.red;
    // act the hover state resolves the same command through the palette (§6.1)
    Event enter{.kind = EventKind::PointerEnter, .position = Point{10.0f, 8.0f}};
    slider.dispatch_event(enter);
    SceneRecorder hovered_recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    slider.render(hovered_recorder);
    const float hovered_red = hovered_recorder.scene()->commands()[0].paint().fill.red;
    // assert two states are two colours, which is the hover visual the plan asks for (§6.1, §8.3)
    EXPECT_NE(normal_red, hovered_red);
}

TEST(ThemeChecks, ALabelKeepsItsOwnForegroundWithoutATheme) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    label.set_foreground(Color::rgb(0.1f, 0.2f, 0.3f));
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    label.render(recorder);
    // assert the label's own colour is what is drawn (§6.1)
    ASSERT_EQ(text_commands(*recorder.scene()).size(), 1u);
    EXPECT_FLOAT_EQ(text_commands(*recorder.scene())[0].paint().fill.red, 0.1f);
}

TEST(ThemeBuilderChecks, ADerivedPlateHasFourDistinctStates) {
    // arrange
    Accents accents;
    // act
    const Theme theme = ThemeBuilder::dark(accents);
    const Palette& palette = theme.palette();
    // assert the four states are four colours, which is what a hover and a press need (§6.1)
    const Color normal = palette.button_plate[state_index(ControlState::Normal)];
    const Color hovered = palette.button_plate[state_index(ControlState::Hovered)];
    const Color pressed = palette.button_plate[state_index(ControlState::Pressed)];
    const Color disabled = palette.button_plate[state_index(ControlState::Disabled)];
    EXPECT_NE(normal.red, hovered.red);
    EXPECT_NE(normal.red, pressed.red);
    EXPECT_FLOAT_EQ(disabled.alpha, 0.45f);
}

TEST(ThemeBuilderChecks, AHoverLiftsAndAPressSinks) {
    // arrange
    Accents accents;
    // act
    const Palette palette = ThemeBuilder::dark(accents).palette();
    // assert the two directions differ, because a press that lifted would read as a release (§6.1, §8.3)
    const Color normal = palette.button_plate[state_index(ControlState::Normal)];
    const Color hovered = palette.button_plate[state_index(ControlState::Hovered)];
    const Color pressed = palette.button_plate[state_index(ControlState::Pressed)];
    EXPECT_GT(hovered.red, normal.red);
    EXPECT_LT(pressed.red, normal.red);
}

TEST(ThemeBuilderChecks, LightAndDarkThemesDiffer) {
    // arrange one set of accents, drawn two ways (§6.1)
    Accents accents;
    // act
    const Palette light = ThemeBuilder::light(accents).palette();
    const Palette dark = ThemeBuilder::dark(accents).palette();
    // assert the schemes differ, which is what makes them two themes rather than one (§6.1)
    EXPECT_NE(light.button_plate[state_index(ControlState::Hovered)].red, dark.button_plate[state_index(ControlState::Hovered)].red);
}

TEST(ThemeBuilderChecks, ADerivedThemeCarriesItsSurface) {
    // arrange
    Accents accents;
    accents.surface = Color::rgb(0.9f, 0.9f, 0.9f);
    // act
    const Theme theme = ThemeBuilder::light(accents);
    // assert the surface is readable back, which a frame background needs (§6.1)
    EXPECT_FLOAT_EQ(theme.surface().red, 0.9f);
}

TEST(ThemeBuilderChecks, AChangedAccentMovesThePlate) {
    // arrange two accent sets, so the derivation has something to react to (§6.1)
    Accents first;
    Accents second;
    second.accent = Color::rgb(0.9f, 0.1f, 0.2f);
    // act
    const Palette first_palette = ThemeBuilder::dark(first).palette();
    const Palette second_palette = ThemeBuilder::dark(second).palette();
    // assert the plate followed the accent, which is the point of deriving it (§6.1)
    EXPECT_FLOAT_EQ(first_palette.button_plate[state_index(ControlState::Normal)].red, 0.20f);
    EXPECT_FLOAT_EQ(second_palette.button_plate[state_index(ControlState::Normal)].red, 0.9f);
}

TEST(ThemeBuilderChecks, ADerivedButtonDrawsTheDerivedPlate) {
    // arrange a widget on a derived theme, which is how an application installs one (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the drawn plate is the derived normal state, not the palette default (§6.1)
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].paint().fill.red, theme.palette().button_plate[state_index(ControlState::Normal)].red);
}

TEST(ThemeBuilderChecks, LightenAndDarkenMoveOppositeWays) {
    // arrange
    const Color base = Color::rgb(0.5f, 0.5f, 0.5f);
    // act assert the two helpers are inverses of each other in direction (§6.1)
    EXPECT_GT(ThemeBuilder::lighten(base, 0.2f).red, base.red);
    EXPECT_LT(ThemeBuilder::darken(base, 0.2f).red, base.red);
}

TEST(ThemeChecks, AThemedButtonDrawsARoundedPlate) {
    // arrange a theme with a radius, which is what a design system installs (§6.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_corner_radius(8.0f);
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate is the rounded kind, which is what the radius asked for (§6.2)
    ASSERT_FALSE(recorder.scene()->commands().empty());
    EXPECT_EQ(recorder.scene()->commands()[0].kind(), DrawCommandKind::RoundedRect);
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].rect_geometry().radius, 8.0f);
}

TEST(ThemeChecks, AnUnthemedButtonStillDrawsAHardPlate) {
    // arrange no theme, so nothing asked for a radius (§6.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the rectangle kind is what an unthemed control has always drawn (§6.2)
    ASSERT_FALSE(recorder.scene()->commands().empty());
    EXPECT_EQ(recorder.scene()->commands()[0].kind(), DrawCommandKind::Rect);
}

TEST(ThemeChecks, ADefaultThemeAlreadyCarriesARadius) {
    // arrange
    const Theme theme;
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act the widget is installed on the default theme without any configuration (§6.2)
    button.set_theme(&theme);
    EXPECT_GT(button.corner_radius(), 0.0f);
}

TEST(ThemeChecks, AThemeWalkReachesEveryWidgetInASubtree) {
    // arrange a tree of themed candidates, where only the root and one child are widgets (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    auto root = std::make_unique<Button>(&engine, &text_engine, "Root");
    auto plain = std::make_unique<WidgetChildNode>();
    auto nested = std::make_unique<Button>(&engine, &text_engine, "Nested");
    Button* root_ptr = root.get();
    Button* nested_ptr = nested.get();
    root->add_child(std::move(plain));
    root->add_child(std::move(nested));
    const Theme theme = ThemeBuilder::dark(Accents{});
    // act one walk installs the theme on the whole subtree (§6.1)
    apply_theme(*root, theme);
    // assert both widgets took it, which is the whole point of the walk (§6.1)
    EXPECT_EQ(root_ptr->theme(), &theme);
    EXPECT_EQ(nested_ptr->theme(), &theme);
}

TEST(ThemeChecks, AThemeWalkLeavesANonWidgetAlone) {
    // arrange a tree whose root is not a widget at all (§6.1)
    BindingEngine engine;
    WidgetChildNode root;
    auto nested = std::make_unique<Button>(&engine, nullptr, "Nested");
    Button* nested_ptr = nested.get();
    root.add_child(std::move(nested));
    const Theme theme = ThemeBuilder::light(Accents{});
    // act
    apply_theme(root, theme);
    // assert the widget inside the plain node was still reached (§6.1)
    EXPECT_EQ(nested_ptr->theme(), &theme);
}

TEST(ButtonCommand, ABoundButtonTakesItsLabelFromTheSharedObject) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button button(&engine, nullptr, "Local");
    button.set_command(&command);
    // act and assert the label is published onto the control rather than copied into it (§18.7)
    EXPECT_EQ(button.label(), "Save");
    EXPECT_EQ(button.command(), &command);
}

TEST(ButtonCommand, ABoundButtonFollowsTheEnablementOfTheSharedObject) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button button(&engine, nullptr, "Save");
    button.set_command(&command);
    // act
    command.set_enabled(false);
    // assert the control refuses input because the one object it reads says so (§18.7)
    EXPECT_TRUE(button.disabled());
    EXPECT_FALSE(button.hit_test(Point{1.0f, 1.0f}));
}

TEST(ButtonCommand, ABoundButtonFollowsTheApplicabilityOfTheSharedObject) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button button(&engine, nullptr, "Save");
    button.set_command(&command);
    // act
    command.set_visible(false);
    // assert withdrawing applicability removes the control from layout and from focus traversal (§18.2)
    EXPECT_FALSE(button.visible());
    EXPECT_FALSE(button.hit_test(Point{1.0f, 1.0f}));
}

TEST(ButtonCommand, TheCommandWinsOverAnImperativeDisabledWriteWhileBound) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button button(&engine, nullptr, "Save");
    button.set_command(&command);
    // act
    button.set_disabled(true);
    // assert a bound control does not answer to two sources of enablement (§18.7)
    EXPECT_FALSE(button.disabled());
}

TEST(ButtonCommand, UnbindingRestoresTheControlsOwnValue) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button button(&engine, nullptr, "Save");
    button.set_disabled(true);
    button.set_command(&command);
    // act
    button.set_command(nullptr);
    // assert the control owns its state again, which is what the form declared (§18.7)
    EXPECT_TRUE(button.disabled());
    EXPECT_EQ(button.command(), nullptr);
}

TEST(ButtonCommand, TwoButtonsBoundToOneCommandShareEveryPublishedField) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button toolbar(&engine, nullptr, "toolbar");
    Button menu(&engine, nullptr, "menu");
    toolbar.set_command(&command);
    menu.set_command(&command);
    // act
    command.set_label("Save As");
    command.set_enabled(false);
    // assert both consumers read one object, which is the entire point of the module (§18.2)
    EXPECT_EQ(toolbar.label(), "Save As");
    EXPECT_EQ(menu.label(), "Save As");
    EXPECT_TRUE(toolbar.disabled());
    EXPECT_TRUE(menu.disabled());
}

TEST(ButtonCommand, APressInvokesTheCommandThroughThePointerRoute) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    std::size_t runs = 0;
    InvocationSource route = InvocationSource::Pointer;
    command.set_action([&runs, &route](const CommandInvocation& invocation) {
        runs++;
        route = invocation.source;
    });
    Button button(&engine, nullptr, "Save", "save");
    button.set_command(&command);
    button.arrange(Rect{.x = 0.0f, .y = 0.0f, .width = 80.0f, .height = 24.0f});
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    button.dispatch_event(press);
    // assert the press reached the shared action rather than a local handler (§18.7)
    EXPECT_EQ(runs, std::size_t{1});
    EXPECT_EQ(route, InvocationSource::Pointer);
    EXPECT_TRUE(press.handled);
}

TEST(ButtonCommand, ADisabledCommandRefusesThePressBeforeTheControlSeesIt) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    std::size_t runs = 0;
    command.set_action([&runs](const CommandInvocation&) { runs++; });
    Button button(&engine, nullptr, "Save", "save");
    button.set_command(&command);
    button.arrange(Rect{.x = 0.0f, .y = 0.0f, .width = 80.0f, .height = 24.0f});
    command.set_enabled(false);
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    button.dispatch_event(press);
    // assert the control left the hit test entirely, which is what a disabled state means (§18.3)
    EXPECT_EQ(runs, std::size_t{0});
    EXPECT_FALSE(press.handled);
}

TEST(ButtonCommand, TheSemanticActivateActionRunsTheSameCommand) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    std::size_t runs = 0;
    InvocationSource route = InvocationSource::Pointer;
    command.set_action([&runs, &route](const CommandInvocation& invocation) {
        runs++;
        route = invocation.source;
    });
    Button button(&engine, nullptr, "Save", "save");
    button.set_command(&command);
    // act
    SemanticNode node = button.semantic_node();
    const bool invoked = node.invoke("activate");
    // assert assistive technology performs the same action a pointer does, through the same object (§18.7)
    EXPECT_TRUE(invoked);
    EXPECT_EQ(runs, std::size_t{1});
    EXPECT_EQ(route, InvocationSource::Accessibility);
}

TEST(ButtonCommand, TheSemanticNodeReportsTheSharedEnablement) {
    // arrange
    BindingEngine engine;
    Command command{"file.save", "Save"};
    Button button(&engine, nullptr, "Save", "save");
    button.set_command(&command);
    // act
    command.set_enabled(false);
    // assert the announcement follows the object, so assistive technology refuses what the control refuses (§10.1)
    EXPECT_TRUE(button.semantic_node().disabled());
}

TEST(ButtonCommand, AnUnboundButtonKeepsItsOwnHandlerAndItsOwnState) {
    // arrange
    BindingEngine engine;
    std::size_t presses = 0;
    Button button(&engine, nullptr, "Local", "save");
    button.set_on_click([&presses] { presses++; });
    button.arrange(Rect{.x = 0.0f, .y = 0.0f, .width = 80.0f, .height = 24.0f});
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    button.dispatch_event(press);
    button.set_disabled(true);
    // assert the pre-existing path a generated form uses is untouched by the module (§18.7)
    EXPECT_EQ(presses, std::size_t{1});
    EXPECT_TRUE(button.disabled());
}

TEST(CheckBoxCommand, AToggleInvokesTheCommandWithTheNewTruth) {
    // arrange
    BindingEngine engine;
    Command command{"view.armed", "Armed"};
    std::size_t runs = 0;
    bool observed = false;
    command.set_action([&runs, &observed](const CommandInvocation& invocation) {
        runs++;
        observed = std::get<bool>(invocation.argument);
    });
    CheckBox box(&engine, "armed");
    box.set_command(&command);
    box.arrange(Rect{.x = 0.0f, .y = 0.0f, .width = 24.0f, .height = 24.0f});
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{8.0f, 8.0f}};
    box.dispatch_event(press);
    // assert an action that carries a value receives it through the invocation (§18.3)
    EXPECT_EQ(runs, std::size_t{1});
    EXPECT_TRUE(observed);
    EXPECT_TRUE(box.checked());
}

TEST(CheckBoxCommand, ACheckBoxFollowsTheApplicabilityOfTheSharedObject) {
    // arrange
    BindingEngine engine;
    Command command{"view.armed", "Armed"};
    CheckBox box(&engine, "armed");
    box.set_command(&command);
    // act
    command.set_visible(false);
    // assert withdrawing applicability removes the control from layout and from focus traversal (§18.2)
    EXPECT_FALSE(box.visible());
}

TEST(SliderCommand, AStepInvokesTheCommandWithTheNewValue) {
    // arrange
    BindingEngine engine;
    Command command{"level.set", "Level"};
    double observed = 0.0;
    command.set_action([&observed](const CommandInvocation& invocation) { observed = std::get<double>(invocation.argument); });
    Slider slider(&engine, "level");
    slider.set_command(&command);
    // act
    slider.step(0.25f);
    // assert an action that carries a value receives it through the invocation (§18.3)
    EXPECT_DOUBLE_EQ(observed, 0.25);
}

TEST(SliderCommand, ADisabledCommandRefusesTheStep) {
    // arrange
    BindingEngine engine;
    Command command{"level.set", "Level"};
    std::size_t runs = 0;
    command.set_action([&runs](const CommandInvocation&) { runs++; });
    Slider slider(&engine, "level");
    slider.set_command(&command);
    command.set_enabled(false);
    // act
    slider.step(0.25f);
    // assert the control refuses input because the one object it reads says so (§18.7)
    EXPECT_EQ(runs, std::size_t{0});
    EXPECT_TRUE(slider.disabled());
}

TEST(FocusStateChecks, AControlWithNoFocusIsNormal) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act, assert a control nobody focused rests in the normal state (§8.3)
    EXPECT_FALSE(button.focused());
    EXPECT_EQ(button.state(), ControlState::Normal);
}

TEST(FocusStateChecks, TakingTheFocusReportsFocused) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    // act
    button.set_focused(true);
    // assert the widget records that it holds the focus, which is what a theme entry reads (§8.3)
    EXPECT_TRUE(button.focused());
    EXPECT_EQ(button.state(), ControlState::Focused);
}

TEST(FocusStateChecks, DroppingTheFocusReturnsToNormal) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_focused(true);
    // act
    button.set_focused(false);
    // assert the ring disappears with the focus, because the ring is the signal (§8.3)
    EXPECT_FALSE(button.focused());
    EXPECT_EQ(button.state(), ControlState::Normal);
}

TEST(FocusStateChecks, ADisabledControlReportsDisabledOverAFocus) {
    // arrange a focused control that then refuses input, because disabled is the fact a user needs (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_focused(true);
    button.set_disabled(true);
    // act, assert the disabled entry wins over the focus, following the fixed precedence of §6.1
    EXPECT_EQ(button.state(), ControlState::Disabled);
}

TEST(FocusStateChecks, AHoverOutranksAFocus) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_focused(true);
    Event enter{EventKind::PointerEnter};
    // act
    button.dispatch_event(enter);
    // assert the pointer is the stronger signal, because the user is pointing at the control (§8.3)
    EXPECT_EQ(button.state(), ControlState::Hovered);
}

TEST(FocusStateChecks, TheFocusManagerReachesAWidget) {
    // arrange a focused widget inside a root, which is the path a tab press takes (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    FocusManager manager;
    // act
    manager.request_focus(&button);
    // assert the focus target on the widget base is what carries the change (§8.3)
    EXPECT_TRUE(button.focused());
    EXPECT_EQ(manager.focused(), &button);
}

TEST(FocusStateChecks, TheFocusManagerClearsTheRingItLeft) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    FocusManager manager;
    manager.request_focus(&button);
    // act
    manager.clear_focus();
    // assert the previous holder is told it lost the focus, so no stale ring survives (§8.3)
    EXPECT_FALSE(button.focused());
}

TEST(FocusRingChecks, AnUnfocusedThemedControlDrawsNoRing) {
    // arrange a themed control nobody focused, which is the resting paint of an application (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert only the plate and its label are recorded, because the normal state carries no ring (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(FocusRingChecks, AFocusedControlStrokesARingOutsideItsPlate) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    button.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate and the ring are two shapes, because the ring stands off the plate (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    const std::vector<Rect> rects = rect_commands(*scene);
    ASSERT_EQ(rects.size(), 2u);
    // assert the ring is larger than the plate on every side, because an offset ring surrounds rather than overlaps (§6.1)
    const float offset = theme.metrics().focus_ring_offset;
    EXPECT_LT(rects[1].x, rects[0].x - offset + 0.001f);
    EXPECT_GT(rects[1].width, rects[0].width);
}

TEST(FocusRingChecks, TheRingStrokesRatherThanFills) {
    // arrange
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    button.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the ring is a line around the plate, so its paint carries a stroke and no fill (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    const DrawCommand& ring = scene->commands()[1];
    EXPECT_FALSE(ring.paint().fills());
    EXPECT_TRUE(ring.paint().strokes());
    EXPECT_FLOAT_EQ(ring.paint().stroke_width, theme.metrics().focus_ring_width);
}

TEST(FocusRingChecks, AnUnthemedControlDrawsNoRing) {
    // arrange a control with no theme, which keeps its own paint because nothing configured a ring (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    button.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate is the only shape, because a null theme means no ring role exists (§6.1)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(FocusRingChecks, AHiddenControlDrawsNoRing) {
    // arrange a focused control that was then hidden, because there is nothing on screen to point at (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    button.set_focused(true);
    button.set_visible(false);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert only the plate is recorded, because a hidden control carries no ring (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    EXPECT_EQ(rect_commands(*scene).size(), 1u);
}

TEST(FocusRingChecks, ADisabledControlDrawsAFadedRing) {
    // arrange a focused control that then refuses input, so the ring fades with the plate (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    button.set_focused(true);
    button.set_disabled(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the ring takes the disabled entry, because a control that refuses keys should not shout (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    const DrawCommand& ring = scene->commands()[1];
    EXPECT_EQ(ring.paint().stroke, theme.palette().focus_ring[state_index(ControlState::Disabled)]);
}

TEST(ThemeControllerChecks, AControllerStartsOnTheDarkScheme) {
    // arrange, act assert the dark theme is the resting one, because the shipped accents are a dark surface (§6.1)
    ThemeController controller{Accents{}};
    EXPECT_EQ(controller.scheme(), ColorScheme::Dark);
}

TEST(ThemeControllerChecks, InstallingAThemeReachesEveryWidget) {
    // arrange a root over two widgets, because a flip has to reach the nested one too (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button outer(&engine, &text_engine, "Outer");
    Button inner(&engine, &text_engine, "Inner");
    outer.add_child(std::make_unique<Button>(&engine, &text_engine, "Deep"));
    ThemeController controller{Accents{}};
    // act
    controller.install(outer, ColorScheme::Light);
    // assert both widgets resolve through the installed theme, because apply_theme walks the subtree (§6.1)
    EXPECT_EQ(outer.theme(), &controller.theme());
    EXPECT_EQ(outer.theme()->palette().button_plate[state_index(ControlState::Normal)].red, controller.theme().palette().button_plate[state_index(ControlState::Normal)].red);
    // assert the subtree is remembered, because a later flip reinstalls over it (§6.1)
    EXPECT_EQ(controller.root(), &outer);
}

TEST(ThemeControllerChecks, AFlipMovesToTheOtherScheme) {
    // arrange accents on a light surface, because a scheme is meaningless without the surface it is drawn on (§6.1)
    Accents light_accents;
    light_accents.surface = Color::rgb(0.96f, 0.96f, 0.97f);
    light_accents.on_surface = Color::rgb(0.10f, 0.10f, 0.12f);
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    ThemeController controller{light_accents, Accents{}};
    controller.install(button, ColorScheme::Dark);
    const Color dark_plate = controller.theme().palette().button_plate[state_index(ControlState::Normal)];
    const Color dark_surface = controller.theme().surface();
    // act
    controller.toggle();
    // assert the light plate replaced the dark one, which is what a flip means (§6.1)
    EXPECT_EQ(controller.scheme(), ColorScheme::Light);
    EXPECT_NE(controller.theme().surface(), dark_surface);
    EXPECT_EQ(controller.theme().palette().label_foreground[state_index(ControlState::Normal)], Color::rgba(0.10f, 0.10f, 0.12f, 1.0f));
    // assert the widget resolved the new one, because the flip reinstalls over the tree (§6.1)
    EXPECT_EQ(button.theme(), &controller.theme());
    // assert the plate tracked the accents, which stay the same when only the surface moves (§6.1)
    EXPECT_EQ(controller.theme().palette().button_plate[state_index(ControlState::Normal)], dark_plate);
}

TEST(ThemeControllerChecks, AnAccentChangeMovesThePlateToo) {
    // arrange a second accent set that moves the accent itself, which is the other half of a scheme (§6.1)
    Accents light_accents;
    light_accents.surface = Color::rgb(0.96f, 0.96f, 0.97f);
    light_accents.on_surface = Color::rgb(0.10f, 0.10f, 0.12f);
    light_accents.accent = Color::rgb(0.10f, 0.35f, 0.75f);
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    ThemeController controller{light_accents, Accents{}};
    controller.install(button, ColorScheme::Dark);
    const Color dark_plate = controller.theme().palette().button_plate[state_index(ControlState::Normal)];
    // act
    controller.toggle();
    // assert the plate moved with the accent, because a scheme chooses the colour a control is filled with (§6.1)
    EXPECT_NE(controller.theme().palette().button_plate[state_index(ControlState::Normal)], dark_plate);
}

TEST(ThemeControllerChecks, AToggleTwiceReturnsToTheStart) {
    // arrange
    ThemeController controller{Accents{}};
    // act, assert the two schemes toggle in both directions, because a toggle has exactly two states (§6.1)
    controller.toggle();
    controller.toggle();
    EXPECT_EQ(controller.scheme(), ColorScheme::Dark);
}

TEST(ThemeControllerChecks, AFlipNotifiesTheApplication) {
    // arrange a handler standing in for the persisted preference (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    ThemeController controller{Accents{}};
    controller.install(button, ColorScheme::Dark);
    std::vector<ColorScheme> seen;
    controller.set_scheme_changed_handler([&seen](ColorScheme scheme) { seen.push_back(scheme); });
    // act
    controller.toggle();
    // assert the application was told, which is where a preference would be written (§6.1)
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], ColorScheme::Light);
}

TEST(ThemeControllerChecks, AnInstallNotifiesAnAlreadyAttachedHandler) {
    // arrange a handler attached before the install, because an application may wire it either way (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    ThemeController controller{Accents{}};
    std::vector<ColorScheme> seen;
    controller.set_scheme_changed_handler([&seen](ColorScheme scheme) { seen.push_back(scheme); });
    // act
    controller.install(button, ColorScheme::Light);
    // assert the application was told of the start up choice, which is where a preference is written (§6.1)
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], ColorScheme::Light);
}

TEST(ThemeControllerChecks, AnUnchangedSchemeInstallsNothing) {
    // arrange a handler that would notice a redundant install (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    ThemeController controller{Accents{}};
    controller.install(button, ColorScheme::Dark);
    std::size_t notified = 0;
    controller.set_scheme_changed_handler([&notified](ColorScheme) { notified++; });
    // act
    controller.set_scheme(ColorScheme::Dark);
    // assert nothing happened, because the tree already carries that theme (§6.1)
    EXPECT_EQ(notified, 0u);
}

TEST(ThemeControllerChecks, AControllerWithNoTreeSurvivesAFlip) {
    // arrange a controller that was never installed, which is the state before composition root wiring (§6.1)
    ThemeController controller{Accents{}};
    // act, assert a flip is refused rather than a null dereference, because there is no subtree (§6.1)
    controller.toggle();
    // assert the scheme still moved, so the preference is recorded even before a tree exists (§6.1)
    EXPECT_EQ(controller.scheme(), ColorScheme::Light);
    EXPECT_EQ(controller.root(), nullptr);
}

TEST(ControlStateVisuals, ACanvasDrawsAPlateFromTheTheme) {
    // arrange a themed canvas, which had no plate of its own before this issue (§6.1)
    BindingEngine engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Canvas canvas(&engine);
    canvas.set_theme(&theme);
    canvas.set_intrinsic_size(Size{20.0f, 20.0f});
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    canvas.render(recorder);
    // assert the plate is drawn, because a state visual needs a surface to land on (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->command_count(), 1u);
    EXPECT_EQ(scene->commands()[0].paint().fill, theme.palette().canvas_background[state_index(ControlState::Normal)]);
}

TEST(ControlStateVisuals, ACanvasDrawsNoPlateWithoutATheme) {
    // arrange an unthemed canvas, which keeps its own transparent plate (§6.1)
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.set_intrinsic_size(Size{20.0f, 20.0f});
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    canvas.render(recorder);
    // assert nothing is recorded, because the existing cull drops a transparent plate (§6.1)
    EXPECT_EQ(recorder.scene()->command_count(), 0u);
}

TEST(ControlStateVisuals, ACanvasGoesHoveredFromAPointerEnter) {
    // arrange a canvas, which had no dispatch at all and so could never leave the normal state (§8.3)
    BindingEngine engine;
    Canvas canvas(&engine);
    Event enter{EventKind::PointerEnter};
    // act
    canvas.dispatch_event(enter);
    // assert the canvas reports hovered, because the transition now reaches it (§8.3)
    EXPECT_TRUE(canvas.hovered());
    EXPECT_EQ(canvas.state(), ControlState::Hovered);
}

TEST(ControlStateVisuals, ACanvasDrawsADifferentPlateWhenHovered) {
    // arrange
    BindingEngine engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Canvas canvas(&engine);
    canvas.set_theme(&theme);
    canvas.set_intrinsic_size(Size{20.0f, 20.0f});
    canvas.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    Event enter{EventKind::PointerEnter};
    // act
    canvas.dispatch_event(enter);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    canvas.render(recorder);
    // assert the hovered entry is drawn, because a state visual that never changes is not one (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->command_count(), 1u);
    EXPECT_EQ(scene->commands()[0].paint().fill, theme.palette().canvas_background[state_index(ControlState::Hovered)]);
    EXPECT_NE(theme.palette().canvas_background[state_index(ControlState::Hovered)], theme.palette().canvas_background[state_index(ControlState::Normal)]);
}

TEST(ControlStateVisuals, ALabelGoesHoveredFromAPointerEnter) {
    // arrange a label, which had no dispatch and so could never leave the normal state (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Text");
    Event enter{EventKind::PointerEnter};
    // act
    label.dispatch_event(enter);
    // assert the label reports hovered, because the transition now reaches it (§8.3)
    EXPECT_TRUE(label.hovered());
    EXPECT_EQ(label.state(), ControlState::Hovered);
}

TEST(ControlStateVisuals, ALabelResolvesTheHoveredForeground) {
    // arrange a themed label, so the state resolves through the palette (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Label label(&engine, &text_engine, "Text");
    label.set_theme(&theme);
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    Event enter{EventKind::PointerEnter};
    // act
    label.dispatch_event(enter);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    label.render(recorder);
    // assert the run took the hovered entry, which is what the state resolver is for (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->command_count(), 1u);
    EXPECT_EQ(scene->commands()[0].paint().fill, theme.palette().label_foreground[state_index(ControlState::Hovered)]);
}

TEST(ControlStateVisuals, ACheckBoxResolvesItsFocusedOutline) {
    // arrange a focused check box, which is the state the outline most needs (§6.1)
    BindingEngine engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    CheckBox box(&engine);
    box.set_theme(&theme);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    // act
    box.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    box.render(recorder);
    // assert the focused outline is drawn, because the palette now has an entry for it (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_GE(scene->command_count(), 1u);
    EXPECT_NE(theme.palette().checkbox_outline[state_index(ControlState::Focused)], theme.palette().checkbox_outline[state_index(ControlState::Normal)]);
}

TEST(ControlStateVisuals, ASliderResolvesItsFocusedTrack) {
    // arrange a focused slider, which is the state a keyboard user depends on (§6.1)
    BindingEngine engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Slider slider(&engine);
    slider.set_theme(&theme);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    // act
    slider.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    slider.render(recorder);
    // assert the slider drew something, because a focused control still paints its track (§6.1)
    EXPECT_GE(recorder.scene()->command_count(), 1u);
    EXPECT_EQ(slider.state(), ControlState::Focused);
}

TEST(FocusRingGuards, AFocusedControlWithNoAreaDrawsNoRing) {
    // arrange a focused control that was never arranged, because a ring around nothing is nothing (§6.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert no shape was recorded, because an unarranged control has no area to surround (§6.2)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    EXPECT_EQ(rect_commands(*scene).size(), 0u);
}

TEST(FocusRingGuards, AnUnfocusedControlWithAFocusedPaletteDrawsNoRing) {
    // arrange a themed control nobody focused, whose hovered ring entry is fully opaque (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    const Theme theme = ThemeBuilder::dark(Accents{});
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert only the plate is recorded, because a ring marks the keyboard focus alone (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    EXPECT_EQ(rect_commands(*scene).size(), 1u);
}

TEST(ControlStateVisuals, AHiddenCanvasTakesNoEvent) {
    // arrange a hidden canvas, which draws nothing to aim at (§8.3)
    BindingEngine engine;
    Canvas canvas(&engine);
    canvas.set_visible(false);
    Event enter{EventKind::PointerEnter};
    // act
    canvas.dispatch_event(enter);
    // assert the transition was skipped, because a hidden control is not a hover target (§8.3)
    EXPECT_FALSE(canvas.hovered());
}

TEST(ControlStateVisuals, ACanvasWithoutAThemeResolvesItsOwnPlate) {
    // arrange an unthemed canvas, whose plate table is unused (§6.1)
    BindingEngine engine;
    Canvas canvas(&engine);
    // act, assert the canvas keeps its own transparent plate, because no palette answers (§6.1)
    EXPECT_FLOAT_EQ(canvas.background().alpha, 0.0f);
}

TEST(ControlStateVisuals, AHiddenLabelTakesNoEvent) {
    // arrange a hidden label, which draws nothing to aim at (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Text");
    label.set_visible(false);
    Event enter{EventKind::PointerEnter};
    // act
    label.dispatch_event(enter);
    // assert the transition was skipped, because a hidden control is not a hover target (§8.3)
    EXPECT_FALSE(label.hovered());
}

TEST(FocusRingGuards, AThemeMayClearTheFocusRingItself) {
    // arrange a theme whose focused ring is fully clear, which is a way to turn the signal off (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme = ThemeBuilder::dark(Accents{});
    theme.set_focus_ring(ControlState::Focused, Color::rgba(0.0f, 0.0f, 0.0f, 0.0f));
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
    // act
    button.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    button.render(recorder);
    // assert only the plate is recorded, because a clear ring puts no ink down (§6.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    EXPECT_EQ(rect_commands(*scene).size(), 1u);
}

TEST(FocusStateChecks, AnUnchangedFocusChangesNothing) {
    // arrange a focused button, which is the early return under test (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    Button button(&engine, &text_engine, "Press");
    button.set_focused(true);
    button.clear_dirty();
    // act
    button.set_focused(true);
    // assert nothing asked for a frame, because the ring is already in that state (§8.3)
    EXPECT_FALSE(button.render_dirty());
    EXPECT_TRUE(button.focused());
}
