#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "commands/command.h"
#include "commands/command_registry.h"
#include "commands/shortcut.h"
#include "core/constraints.h"
#include "focus/focus_manager.h"
#include "input/input_controller.h"
#include "input/input_dispatcher.h"
#include "layout/column_layout.h"
#include "layout/row_layout.h"
#include "render/frame_scene.h"
#include "render/image_resource.h"
#include "render/render_context.h"
#include "render/resource_factory.h"
#include "runtime/binding_engine.h"
#include "semantics/semantic_node.h"
#include "semantics/semantic_provider.h"
#include "style/theme_builder.h"
#include "ui/scene_builder.h"
#include "ui/semantic_tree_builder.h"
#include "widgets/button.h"
#include "widgets/canvas.h"
#include "widgets/checkbox.h"
#include "widgets/combo_box.h"
#include "widgets/image.h"
#include "widgets/label.h"
#include "widgets/menu.h"
#include "widgets/menu_bar.h"
#include "widgets/menu_item.h"
#include "widgets/numeric_stepper.h"
#include "widgets/panel.h"
#include "widgets/progress_bar.h"
#include "widgets/radio_button.h"
#include "widgets/radio_group.h"
#include "widgets/slider.h"
#include "widgets/tab_page.h"
#include "widgets/tab_title.h"
#include "widgets/tab_view.h"
#include "widgets/text_field.h"
#include "widgets/theme_controller.h"
#include "widgets/tool_bar.h"
#include "window/popup_host.h"
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

    // the text engine test double, which measures with a fixed advance so a test asserts on the widget (§9.1)
    class StubTextEngine final : public TextEngine
    {
    public:
        // the advance one byte of text contributes, in logical pixels (§9.1)
        static constexpr float ADVANCE = 10.0f;

        // the height the double reports for any run, in logical pixels (§9.1)
        static constexpr float HEIGHT = 16.0f;

        // how many measurements the widget asked for, which is mutable because the contract is const (§9.1)
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
            // the line covers the whole double text, which is what a decoration range indexes (§9.1)
            line.start_byte = 0;
            line.end_byte = text.size();
            // the line box and advance are the geometry the selection and caret read (§9.1)
            line.ascent = HEIGHT * 0.8f;
            line.descent = HEIGHT * 0.2f;
            // every character becomes one glyph placed a fixed advance apart, which is what the double measures (§9.1)
            for (const char character : text)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(character), .position = Point{.x = static_cast<float>(line.glyphs.size()) * ADVANCE, .y = 0.0f}, .cluster = static_cast<std::uint32_t>(line.glyphs.size())});
            // the advance counts characters, and the ceiling clamps the result the way a container expects (§8.2)
            const float advanced = static_cast<float>(text.size()) * ADVANCE;
            // the line width is what a caret past the last glyph resolves against (§9.1)
            line.width = advanced;
            // the shaped run travels beside the extents, which is what a measurement hands over (§9.1)
            return TextLayout{.run = ShapedRun::make({std::move(line)}, font_size), .size = Size{.width = max_width > 0.0f ? std::min(advanced, max_width) : advanced, .height = HEIGHT}, .ascent = HEIGHT * 0.8f, .descent = HEIGHT * 0.2f};
        }

        [[nodiscard]] bool ready() const override {
            // the double reports the flag a test sets, so an unusable engine can be exercised (§9.1)
            return usable;
        }

        // the last font selection a widget asked to measure with, which a test asserts on (§9.1, AD-34)
        mutable FontSpec last_font_spec;

        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size, const FontSpec& spec) const override {
            // the selection is captured, so a test proves the declarative surface reached the engine (§9.1)
            last_font_spec = spec;
            // the double shapes no face, so it measures exactly as the size only overload does (§9.1)
            return measure(text, max_width, font_size);
        }
    };

    // the popup host test double, which records what a control asks a native surface to do (§5.1)
    class RecordingPopupHost final : public PopupHost
    {
    public:
        // the handle every accepted surface is named by, or an invalid one when the host refuses (§5.1)
        WindowHandle handle = WindowHandle::from_id(1000);

        // the anchor the last opening named, which is the control that owns the list (§5.1)
        const UiNode* anchor = nullptr;

        // the trigger rectangle the last opening named (§5.1)
        Rect anchor_rect{};

        // the surface extent the last opening named (§5.1)
        Size size{};

        // the clear colour the last opening named (§6.1)
        Color background{};

        // how many openings, shows, hides and destructions arrived (§5.1)
        std::size_t opens = 0;
        std::size_t shows = 0;
        std::size_t closes = 0;
        std::size_t destroys = 0;

        // the dismissal work the control installed, which a test invokes as a native dismissal (§5.1)
        std::function<void()> on_closed;

        // the tree builder the host received, which a test invokes to cover the real native surface (§5.1)
        PopupRootBuilder builder;

        [[nodiscard]] WindowHandle open_popup(const UiNode& anchor_node, Rect anchor_bounds, Size surface_size, PopupRootBuilder root_builder, Color clear, std::function<void()> closed) override {
            // the request is recorded before the answer, because a test reads back what the control asked (§5.1)
            opens++;
            anchor = &anchor_node;
            anchor_rect = anchor_bounds;
            size = surface_size;
            background = clear;
            builder = std::move(root_builder);
            on_closed = std::move(closed);
            return handle;
        }

        void close_popup(WindowHandle) override {
            // the hide is counted, because a closed list is kept rather than destroyed (§5.1)
            closes++;
        }

        void show_popup(WindowHandle) override {
            // the show is counted, because a retained surface reopens without a new tree (§5.1)
            shows++;
        }

        void destroy_popup(WindowHandle) override {
            // the destruction is counted, because the control releases the surface when it dies (§5.1)
            destroys++;
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

TEST(LabelChecks, TheRunCentresVerticallyInATallerBox) {
    // arrange a label arranged into a box taller than its sixteen point run, which is what a list row is (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "row");
    Size ignored{};
    label.measure(SizeConstraints::unconstrained(), ignored);
    label.arrange(Rect::from_origin_size(Point{0.0f, 10.0f}, Size{100.0f, 40.0f}));
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f);
    SceneRecorder recorder(metrics);
    // act
    label.render(recorder);
    // assert the run sits on the middle of the box, which is what lines a list row's text up (§8.2, §9.1)
    ASSERT_EQ(text_commands(*recorder.scene()).size(), 1u);
    // the run is sixteen tall in a forty tall box, so twelve points sit above it and the box starts at ten (§9.1)
    EXPECT_FLOAT_EQ(text_commands(*recorder.scene()).front().text_geometry().origin.y, 22.0f);
}

TEST(LabelChecks, ARunThatFillsTheBoxSitsAtTheTop) {
    // arrange a label whose box is exactly its run height, which is the ordinary case (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "row");
    Size ignored{};
    label.measure(SizeConstraints::unconstrained(), ignored);
    label.arrange(Rect::from_origin_size(Point{0.0f, 10.0f}, Size{100.0f, 16.0f}));
    const WindowMetrics metrics = WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f);
    SceneRecorder recorder(metrics);
    // act
    label.render(recorder);
    // assert there is no slack to centre, so the run stays at the box top, which keeps existing layouts (§9.1)
    ASSERT_EQ(text_commands(*recorder.scene()).size(), 1u);
    EXPECT_FLOAT_EQ(text_commands(*recorder.scene()).front().text_geometry().origin.y, 10.0f);
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

TEST(LabelChecks, AChildWithoutSemanticsWalksItsSubtree) {
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
    // assert a plain layout is announced as a group, which is what a container is (§10.1)
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::Group);
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

TEST(CanvasChecks, MeasuresDeclarativeTextThroughTheInstalledEngine) {
    // arrange a canvas with the text engine a draw block measures through (§9.1, §6.4)
    BindingEngine engine;
    StubTextEngine text;
    Canvas canvas(&engine, &text);
    // act
    const TextLayout layout = canvas.measure_text("axis", 12.0f);
    // assert the engine measured the unbounded run, which is what lays the next drawing beside it (§6.4)
    EXPECT_FLOAT_EQ(layout.size.width, 4.0f * StubTextEngine::ADVANCE);
    EXPECT_EQ(text.measurements, 1U);
}

TEST(CanvasChecks, MeasuresNoTextWithoutAnEngine) {
    // arrange a canvas whose host installed no text engine (§9.1)
    BindingEngine engine;
    Canvas canvas(&engine);
    // act
    const TextLayout layout = canvas.measure_text("axis", 12.0f);
    // assert the empty layout stands in, which draws nothing rather than guessing (§9.1)
    EXPECT_TRUE(layout.empty());
    EXPECT_FLOAT_EQ(layout.size.width, 0.0f);
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

TEST(ThemeChecks, AChildBuiltAfterTheInstallInheritsTheTheme) {
    // arrange a themed widget whose child arrives after the theme was installed (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    Button button(&engine, &text_engine, "Press");
    apply_theme(button, theme);
    auto label = std::make_unique<Label>(&engine, &text_engine, "late");
    Label* const label_ptr = label.get();
    button.add_child(std::move(label));
    // assert the late child resolves the theme through its ancestor, which the walk could not reach (§6.1)
    EXPECT_EQ(label_ptr->theme(), &theme);
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
    Event enter{.kind = EventKind::PointerEnter};
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
    Event enter{.kind = EventKind::PointerEnter};
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
    Event enter{.kind = EventKind::PointerEnter};
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
    Event enter{.kind = EventKind::PointerEnter};
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
    Event enter{.kind = EventKind::PointerEnter};
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
    Event enter{.kind = EventKind::PointerEnter};
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
    Event enter{.kind = EventKind::PointerEnter};
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

TEST(NativeThemeChecks, AThemedButtonStrokesTheBorderLayer) {
    // arrange a theme whose border and border width are set, because the second layer is a theme decision (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_button_border(Color::rgb(1.0f, 0.0f, 0.0f));
    theme.set_button_border_width(1.0f);
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the plate carries the border, because the fill and the stroke are one layered draw (§8.4)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    const DrawCommand& plate = scene->commands()[0];
    EXPECT_EQ(plate.paint().stroke, Color::rgb(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(plate.paint().stroke_width, 1.0f);
}

TEST(NativeThemeChecks, AThemedButtonResolvesTheBorderByState) {
    // arrange a themed button whose hover border differs from the normal one (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_button_border(ControlState::Normal, Color::rgb(0.5f, 0.5f, 0.5f));
    theme.set_button_border(ControlState::Hovered, Color::rgb(1.0f, 0.0f, 0.0f));
    theme.set_button_border_width(1.0f);
    Button button(&engine, &text_engine, "Press");
    button.set_theme(&theme);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder resting(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act a resting draw resolves the normal entry, which is what a pointer elsewhere gives (§8.4)
    button.render(resting);
    // act a pointer enter moves the state, so the next draw resolves the hovered entry (§8.3, §8.4)
    Event enter{.kind = EventKind::PointerEnter, .position = Point{10.0f, 5.0f}};
    button.dispatch_event(enter);
    SceneRecorder hovered(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    button.render(hovered);
    // assert the two states are two strokes, which is what a lifted line reads as (§8.4)
    EXPECT_EQ(resting.scene()->commands()[0].paint().stroke, Color::rgb(0.5f, 0.5f, 0.5f));
    EXPECT_EQ(hovered.scene()->commands()[0].paint().stroke, Color::rgb(1.0f, 0.0f, 0.0f));
}

TEST(NativeThemeChecks, APaddedButtonMeasuresToTheLabelPlusThePadding) {
    // arrange a themed button with twelve point padding, which is what a native push control takes (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_button_padding_horizontal(12.0f);
    Button button(&engine, &text_engine, "x");
    button.set_theme(&theme);
    Size measured{};
    // act
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert the label plus its padding drives the width, while the height follows the theme (§8.2)
    EXPECT_FLOAT_EQ(measured.width, StubTextEngine::ADVANCE + 24.0f);
}

TEST(NativeThemeChecks, AThemedCheckBoxDrawsTheGlyphOverTheAccentFill) {
    // arrange a themed checked box, whose metrics carry a corner radius and a glyph stroke (§8.4)
    BindingEngine engine;
    Theme theme;
    theme.set_checkbox_corner_radius(2.0f);
    theme.set_checkbox_glyph_stroke(1.8f);
    CheckBox box(&engine, "box");
    box.set_theme(&theme);
    box.set_checked(true);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert the fill, the outline and the check mark are three records (§8.4)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->commands().size(), 3u);
    EXPECT_EQ(scene->commands()[2].kind(), DrawCommandKind::Polyline);
    // assert the glyph strokes with the colour the glyph role answers, which is readable over the accent (§8.4)
    EXPECT_FLOAT_EQ(scene->commands()[2].polyline_geometry().stroke.width, 1.8f);
    EXPECT_EQ(scene->commands()[2].polyline_geometry().stroke.color, theme.palette().checkbox_glyph[state_index(ControlState::Normal)]);
}

TEST(NativeThemeChecks, AThemedCheckBoxDrawsARoundedOutline) {
    // arrange a themed unchecked box whose corner radius is themed, because the shape is the theme decision (§8.4)
    BindingEngine engine;
    Theme theme;
    theme.set_checkbox_corner_radius(2.0f);
    CheckBox box(&engine, "box");
    box.set_theme(&theme);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert the outline is one rounded record with no fill of its own (§8.4)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->commands().size(), 1u);
    EXPECT_EQ(scene->commands()[0].kind(), DrawCommandKind::RoundedRect);
    EXPECT_FLOAT_EQ(scene->commands()[0].rect_geometry().radius, 2.0f);
}

TEST(NativeThemeChecks, AThemedCheckBoxCarriesTheMixedState) {
    // arrange an unchecked box that is set to the indeterminate state (§10.1)
    BindingEngine engine;
    CheckBox box(&engine, "box");
    box.set_indeterminate(true);
    // act the semantic node is read, which is what assistive technology announces (§10.1)
    const SemanticNode node = box.semantic_node();
    // assert the announced state is the mixed one (§10.1)
    EXPECT_EQ(node.check(), CheckState::Indeterminate);
}

TEST(NativeThemeChecks, AToggleOnAMixedBoxResolvesToChecked) {
    // arrange a mixed box whose toggle fires, which is what a press does (§10.1)
    BindingEngine engine;
    bool reported = false;
    CheckBox box(&engine, "box");
    box.set_on_toggle([&reported](bool checked) { reported = checked; });
    box.set_indeterminate(true);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    // act a press inside the box toggles it, which is the shared path a key press takes too (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{4.0f, 4.0f}};
    box.dispatch_event(press);
    // assert the mixed state resolved to the true edge, which is what a boolean does (§10.1)
    EXPECT_TRUE(box.checked());
    EXPECT_FALSE(box.indeterminate());
    EXPECT_TRUE(reported);
}

TEST(NativeThemeChecks, AThemedSliderDrawsTheFilledTrackAndTheRoundThumb) {
    // arrange a themed slider whose radius metrics are set, because the native shapes are the theme decision (§8.4)
    BindingEngine engine;
    Theme theme;
    theme.set_slider_track_radius(2.0f);
    theme.set_slider_thumb(16.0f);
    theme.set_slider_thumb_radius(8.0f);
    theme.set_slider_fill(Color::rgb(0.2f, 0.55f, 0.9f));
    Slider slider(&engine, "slider");
    slider.set_theme(&theme);
    slider.set_value(0.5f);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert the track, the fill and the thumb are three records (§8.4)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->commands().size(), 3u);
    EXPECT_EQ(scene->commands()[1].kind(), DrawCommandKind::RoundedRect);
    EXPECT_EQ(scene->commands()[2].kind(), DrawCommandKind::RoundedRect);
    // assert the fill runs from the left edge up to the thumb centre, which is where the accent ends (§8.4)
    EXPECT_FLOAT_EQ(scene->commands()[1].rect_geometry().rect.width, 60.0f);
    // assert the thumb carries the themed radius, which reads as a circle (§8.4)
    EXPECT_FLOAT_EQ(scene->commands()[2].rect_geometry().radius, 8.0f);
}

TEST(NativeThemeChecks, ATickedSliderDrawsADiscreteMarkPerStep) {
    // arrange a themed slider whose tick length is set, because a ticked slider is a theme decision (§8.4)
    BindingEngine engine;
    Theme theme;
    theme.set_slider_tick_length(4.0f);
    theme.set_slider_tick_width(2.0f);
    theme.set_slider_step(0.25f);
    theme.set_slider_fill(Color::rgb(0.2f, 0.55f, 0.9f));
    Slider slider(&engine, "slider");
    slider.set_theme(&theme);
    slider.set_value(0.0f);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act, assert the track, the five ticks, the fill at the minimum and the thumb are eight records (§8.4)
    slider.render(recorder);
    EXPECT_EQ(recorder.scene()->commands().size(), 8u);
}

TEST(NativeThemeChecks, AThemedSliderFocusRingSurroundsTheThumb) {
    // arrange a themed focused slider whose thumb is round, since the ring follows it (§8.4)
    BindingEngine engine;
    Theme theme;
    theme.set_slider_thumb(16.0f);
    theme.set_slider_thumb_radius(8.0f);
    Slider slider(&engine, "slider");
    slider.set_theme(&theme);
    slider.set_value(1.0f);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    slider.set_focused(true);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert the ring draws first, because the background phase precedes the content it surrounds (§8.4)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->commands().size(), 3u);
    const Rect& ring = scene->commands()[0].rect_geometry().rect;
    const Rect& thumb = scene->commands()[2].rect_geometry().rect;
    // assert the ring stands two away from the thumb on every side, because a ring surrounds rather than overlaps (§6.1)
    EXPECT_FLOAT_EQ(ring.x, thumb.x - theme.metrics().focus_ring_offset);
    EXPECT_FLOAT_EQ(ring.width, thumb.width + theme.metrics().focus_ring_offset * 2.0f);
    EXPECT_FLOAT_EQ(scene->commands()[0].rect_geometry().radius, theme.metrics().focus_ring_radius);
}

TEST(NativeThemeChecks, AThemedSliderDrawsAnElevatedThumb) {
    // arrange a themed slider whose shadow strength is set, because elevation is the themed decision (§8.4)
    BindingEngine engine;
    Theme theme;
    theme.set_slider_thumb_shadow(0.3f);
    Slider slider(&engine, "slider");
    slider.set_theme(&theme);
    slider.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    slider.render(recorder);
    // assert the cast is recorded between the track and the thumb, which is what sits under (§8.4)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(scene->commands().size(), 3u);
    EXPECT_EQ(scene->commands()[1].kind(), DrawCommandKind::Shadow);
    EXPECT_FLOAT_EQ(scene->commands()[1].shadow_geometry().shadow.color.alpha, 0.3f);
}

TEST(NativeThemeChecks, AThemedLabelResolvesTheRoleSize) {
    // arrange a themed label whose caption size the theme carries (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_role_font_size(TextRole::Caption, 9.0f);
    Label label(&engine, &text_engine, "Hi");
    label.set_theme(&theme);
    label.set_role(TextRole::Caption);
    Size measured{};
    // act
    label.measure(SizeConstraints::unconstrained(), measured);
    // assert the run was measured at nine, because the role resolved through the hierarchy (§8.4)
    EXPECT_FLOAT_EQ(label.layout().run->font_size(), 9.0f);
}

TEST(NativeThemeChecks, ASecondaryCaptionDimsThroughTheToken) {
    // arrange a themed caption whose secondary alpha keeps six tenths (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_role_font_size(TextRole::Caption, 9.0f);
    theme.set_secondary_opacity(0.6f);
    Label label(&engine, &text_engine, "Hi");
    label.set_theme(&theme);
    label.set_role(TextRole::Caption);
    label.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{60.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    label.render(recorder);
    // assert the run draws through the token rather than a literal, which is what flips with the scheme (§8.4)
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].paint().fill.alpha, 0.6f);
}

TEST(NativeThemeChecks, AnExplicitSizeReplacesTheRole) {
    // arrange a themed label at a caption role, whose run measured at nine (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_role_font_size(TextRole::Caption, 9.0f);
    Label label(&engine, &text_engine, "Hi");
    label.set_theme(&theme);
    label.set_role(TextRole::Caption);
    // act an explicit size is set, which is what an author chooses over the hierarchy (§8.4)
    label.set_font_size(11.0f);
    // assert the role cleared to none and the explicit size decided, which is the resolution order (§8.4)
    EXPECT_EQ(label.role(), TextRole::None);
}

TEST(ThemeControllerChecks, APresetBuildsTheNativeSchemes) {
    // arrange a controller built through the macos preset, which is what a platform native application installs (§8.4)
    ThemeController controller(ThemePreset::Macos);
    // assert the built dark scheme carries the macos metrics, which are the guide's control sizes (§8.4)
    EXPECT_EQ(controller.theme().metrics().button_minimum, (Size{24.0f, 28.0f}));
    EXPECT_FLOAT_EQ(controller.theme().metrics().button_padding_horizontal, 12.0f);
}

TEST(ThemeControllerChecks, AnAccentChangeKeepsThePreset) {
    // arrange a macos controller whose accents are rebuilt (§8.4)
    ThemeController controller(ThemePreset::Macos);
    // act
    controller.set_accents(Accents{}, Accents{});
    // assert the rebuilt theme is still the preset, because the accents only drive the derivation (§8.4)
    EXPECT_EQ(controller.theme().metrics().button_minimum, (Size{24.0f, 28.0f}));
}

TEST(NativeThemeChecks, AnUnthemedBoxDrawsItsOutlineWithoutAFill) {
    // arrange an unchecked box with no theme, whose fill the outline pass always left empty (§8.4)
    BindingEngine engine;
    CheckBox box(&engine, "box");
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert the unthemed outline carries no fill of its own, which is the transparent token (§8.4)
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].paint().fill.alpha, 0.0f);
    EXPECT_TRUE(recorder.scene()->commands()[0].paint().strokes());
}

TEST(ThemeChecks, AThemeInstallRunsTheLayoutPasses) {
    // arrange a slider measured under no theme, whose extents the install invalidates (§8.4)
    BindingEngine engine;
    Slider slider(&engine, "slider");
    Size measured{};
    slider.measure(SizeConstraints::at_most(Size{400.0f, 400.0f}), measured);
    Theme theme;
    theme.set_slider_extent(Size{300.0f, 24.0f});
    // act the theme is installed over a laid out widget, which asks the layout passes to run again (§8.4)
    slider.set_theme(&theme);
    // assert the measure pass is dirty, because the theme now owns the extent (§8.4, §8.2)
    EXPECT_TRUE(slider.measure_dirty());
}

TEST(NativeThemeChecks, AnOpacityDimsTheCheckGlyphWithTheFill) {
    // arrange a themed checked box inside a node that drew at zero opacity (§6.1)
    BindingEngine engine;
    Theme theme;
    theme.set_checkbox_glyph_stroke(1.8f);
    CheckBox box(&engine, "box");
    box.set_theme(&theme);
    box.set_checked(true);
    box.set_opacity(0.0f);
    box.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{16.0f, 16.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    box.render(recorder);
    // assert the glyph stroke dims like the fill would, because an opacity owns the whole node (§6.1)
    EXPECT_FLOAT_EQ(recorder.scene()->commands().back().polyline_geometry().stroke.color.alpha, 0.0f);
}

TEST(NativeThemeChecks, AnExplicitSizeLeavesTheRoleEvenUnchanged) {
    // arrange a themed label at a caption role pointing at the same value the size carries (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_role_font_size(TextRole::Caption, 9.0f);
    Label label(&engine, &text_engine, "Hi");
    label.set_font_size(20.0f);
    label.set_role(TextRole::Caption);
    // act the same explicit size is set again, which is what a duplicated authoring passes (§8.4)
    label.set_font_size(20.0f);
    // assert the role cleared, because the explicit size is the resolution owner (§8.4)
    EXPECT_EQ(label.role(), TextRole::None);
}

TEST(LabelChecks, AFontSelectionRemeasuresAndInvalidates) {
    // arrange a label measured once, whose engine counts the measurements it is asked for (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Label label(&engine, &text_engine, "Albor");
    Size measured{};
    label.measure(SizeConstraints::unconstrained(), measured);
    label.clear_dirty();
    const std::size_t before = text_engine.measurements;
    // act a family, a weight and a fallback are each set, which remeasures through the engine (§9.1, AD-34)
    label.set_font_family("Inter");
    label.set_font_weight(FontWeight::Bold);
    label.set_font_fallback({"Fallback One", "Fallback Two"});
    // assert each setter asked the engine again, because the face may change with the selection (§9.1)
    EXPECT_GT(text_engine.measurements, before);
    // assert the label must be laid out again, because the metrics may have moved with the face (§8.2)
    EXPECT_TRUE(label.layout_dirty());
    // assert the selection reached the engine, which is what makes the declarative surface real (§9.1, AD-34)
    EXPECT_EQ(text_engine.last_font_spec.family, "Inter");
    EXPECT_EQ(text_engine.last_font_spec.weight, FontWeight::Bold);
    ASSERT_EQ(text_engine.last_font_spec.fallback.size(), 2u);
    // the first fallback family reached the engine in the order it was written (§9.1, AD-34)
    EXPECT_EQ(text_engine.last_font_spec.fallback.front(), "Fallback One");
    // the widget reports the selection it stores, which the generated facade's test reads (§9.1, AD-34)
    EXPECT_EQ(label.font_family(), "Inter");
    EXPECT_EQ(label.font_weight(), FontWeight::Bold);
    ASSERT_EQ(label.font_fallback().size(), 2u);
    EXPECT_EQ(label.font_fallback().front(), "Fallback One");
}

TEST(ProgressBarChecks, MeasuresTheDefaultExtent) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    Size measured{};
    // act
    bar.measure(SizeConstraints::unconstrained(), measured);
    // assert the bar measures to the themed default extent, because it has no content (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 160.0f);
    EXPECT_FLOAT_EQ(measured.height, 8.0f);
}

TEST(ProgressBarChecks, ClampsTheValueIntoTheUnitRange) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    // act
    bar.set_value(-0.25f);
    // assert
    EXPECT_FLOAT_EQ(bar.value(), 0.0f);
    bar.set_value(1.25f);
    EXPECT_FLOAT_EQ(bar.value(), 1.0f);
}

TEST(ProgressBarChecks, SetValueRedraws) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.clear_dirty();
    // act
    bar.set_value(0.5f);
    // assert
    EXPECT_TRUE(bar.render_dirty());
}

TEST(ProgressBarChecks, AnUnchangedValueRedrawsNothing) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.set_value(0.5f);
    bar.clear_dirty();
    // act
    bar.set_value(0.5f);
    // assert
    EXPECT_FALSE(bar.render_dirty());
}

TEST(ProgressBarChecks, PaintsATrackAndAFillForTheFraction) {
    // arrange a half full bar arranged over the default extent (§8)
    BindingEngine engine;
    ProgressBar bar(&engine);
    Size measured{};
    bar.measure(SizeConstraints::unconstrained(), measured);
    bar.arrange(Rect::from_origin_size(Point{10.0f, 20.0f}, measured));
    bar.set_value(0.5f);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    bar.render(recorder);
    // assert one track and one fill are recorded in paint order (§6.1)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_FLOAT_EQ(rects[0].width, 160.0f);
    EXPECT_FLOAT_EQ(rects[0].height, 8.0f);
    EXPECT_FLOAT_EQ(rects[1].width, 80.0f);
    EXPECT_FLOAT_EQ(rects[1].height, 8.0f);
    // the fill starts at the track's left edge, which is what makes the fraction read from the start (§8)
    EXPECT_FLOAT_EQ(rects[1].x, 10.0f);
}

TEST(ProgressBarChecks, AnEmptyBarDrawsOnlyItsTrack) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 8.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    bar.render(recorder);
    // assert
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
}

TEST(ProgressBarChecks, AFullBarFillsTheWholeTrack) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 8.0f}));
    bar.set_value(1.0f);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    bar.render(recorder);
    // assert
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_FLOAT_EQ(rects[1].width, rects[0].width);
}

TEST(ProgressBarChecks, TheThemeResolvesTheTrackAndFill) {
    // arrange a themed bar whose two colours are explicit, so the palette decides what is drawn (§6.1)
    BindingEngine engine;
    Theme theme;
    theme.set_progress_bar_track(Color::rgb(0.1f, 0.2f, 0.3f));
    theme.set_progress_bar_fill(Color::rgb(0.8f, 0.9f, 1.0f));
    theme.set_progress_bar_radius(4.0f);
    ProgressBar bar(&engine);
    bar.set_theme(&theme);
    bar.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 8.0f}));
    bar.set_value(0.5f);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    bar.render(recorder);
    // assert the two commands carry the themed colours and the themed capsule radius (§6.1, §8.4)
    ASSERT_EQ(recorder.scene()->commands().size(), 2u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().fill, Color::rgb(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(recorder.scene()->commands()[1].paint().fill, Color::rgb(0.8f, 0.9f, 1.0f));
    EXPECT_FLOAT_EQ(recorder.scene()->commands()[0].rect_geometry().radius, 4.0f);
}

TEST(ProgressBarChecks, SetTrackColorRedraws) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.clear_dirty();
    // act
    bar.set_track_color(Color::rgb(0.1f, 0.1f, 0.1f));
    // assert
    EXPECT_TRUE(bar.render_dirty());
}

TEST(ProgressBarChecks, SetFillColorRedraws) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.clear_dirty();
    // act
    bar.set_fill_color(Color::rgb(0.1f, 0.1f, 0.1f));
    // assert
    EXPECT_TRUE(bar.render_dirty());
}

TEST(ProgressBarChecks, AnnouncesTheProgressRoleAndItsFraction) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine, "download");
    bar.arrange(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{160.0f, 8.0f}));
    bar.set_value(0.5f);
    // act
    const SemanticNode node = bar.semantic_node();
    // assert
    EXPECT_EQ(node.role(), SemanticRole::ProgressBar);
    EXPECT_EQ(node.name(), "download 0.500000");
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{160.0f, 8.0f})));
    EXPECT_FALSE(node.disabled());
    // a progress bar takes no action, because the user only reads it (§10.1)
    EXPECT_EQ(node.action_count(), 0u);
}

TEST(ProgressBarChecks, ADisabledBarAnnouncesTheState) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine, "download");
    bar.set_disabled(true);
    // act
    const SemanticNode node = bar.semantic_node();
    // assert
    EXPECT_TRUE(node.disabled());
    EXPECT_TRUE(bar.disabled());
}

TEST(ProgressBarChecks, AnUnchangedDisabledStateRedrawsNothing) {
    // arrange
    BindingEngine engine;
    ProgressBar bar(&engine);
    bar.set_disabled(true);
    bar.clear_dirty();
    // act
    bar.set_disabled(true);
    // assert
    EXPECT_FALSE(bar.render_dirty());
}

TEST(TextFieldFontChecks, AFontSelectionRemeasuresAndInvalidates) {
    // arrange a field measured once, whose engine captures the selection it is asked for (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TextField field(&engine, &text_engine, "Albor");
    Size measured{};
    field.measure(SizeConstraints::unconstrained(), measured);
    field.clear_dirty();
    const std::size_t before = text_engine.measurements;
    // act a family, a weight and a fallback are each set, which remeasures through the engine (§9.1, AD-34)
    field.set_font_family("Inter");
    field.set_font_weight(FontWeight::Semibold);
    field.set_font_fallback({"Fallback One"});
    // assert each setter asked the engine again, because the face may change with the selection (§9.1)
    EXPECT_GT(text_engine.measurements, before);
    // assert the field must be laid out again and the selection reached the engine (§8.2, §9.1)
    EXPECT_TRUE(field.layout_dirty());
    EXPECT_EQ(text_engine.last_font_spec.family, "Inter");
    EXPECT_EQ(text_engine.last_font_spec.weight, FontWeight::Semibold);
}

TEST(TabPageChecks, MeasuresTheLabelForTheStrip) {
    // arrange a page whose label the strip draws (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabPage page(&engine, &text_engine, "Tab");
    // act
    page.measure_header(20.0f);
    // assert the run measures at the advance the double reports, which is what the strip sizes by (§9.1)
    EXPECT_FLOAT_EQ(page.label_layout().size.width, 30.0f);
    EXPECT_FLOAT_EQ(page.label_layout().size.height, 16.0f);
    // act the label changes, which invalidates the page and measures a new run (§9.1)
    page.set_label("Four");
    EXPECT_TRUE(page.layout_dirty());
    page.measure_header(20.0f);
    // assert the new run is the one the strip draws (§9.1)
    EXPECT_FLOAT_EQ(page.label_layout().size.width, 40.0f);
}

TEST(TabPageChecks, AnEmptyLabelMeasuresAsNothing) {
    // arrange a page with no label, which a strip uses only its minimum width for (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabPage page(&engine, &text_engine);
    // act
    page.measure_header(20.0f);
    // assert the empty run carries no layout, which the strip reads as no label (§9.1)
    EXPECT_TRUE(page.label_layout().empty());
}

TEST(TabPageChecks, ATitleIsMeasuredForTheStripAndKeptOutOfThePage) {
    // arrange a page with a title and a content child of different sizes (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabPage page(&engine, &text_engine, "Label");
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto icon = std::make_unique<WidgetChildNode>();
    icon->m_intrinsic = Size{80.0f, 20.0f};
    title->add_child(std::move(icon));
    page.add_child(std::move(title_owner));
    auto body = std::make_unique<WidgetChildNode>();
    body->m_intrinsic = Size{50.0f, 30.0f};
    page.add_child(std::move(body));
    // act the header measures the title, and the page measures only its content (§8.2)
    page.measure_header(13.0f);
    Size measured{};
    page.measure(SizeConstraints::unconstrained(), measured);
    // assert the title is the header's extent and the content child is the page's extent (§8.2)
    EXPECT_FLOAT_EQ(page.title_size().width, 80.0f);
    EXPECT_FLOAT_EQ(page.title_size().height, 20.0f);
    EXPECT_FLOAT_EQ(measured.width, 50.0f);
    EXPECT_FLOAT_EQ(measured.height, 30.0f);
    // act the page body is arranged, which leaves the title for the strip to place (§8.2)
    page.arrange(Rect::from_origin_size(Point{0.0f, 32.0f}, Size{200.0f, 68.0f}));
    // assert the page never placed the title, because the strip owns its header geometry (§8.2)
    EXPECT_TRUE(title->bounds().empty());
}

TEST(TabPageChecks, ATitleDoesNotEnterThePageSemantics) {
    // arrange a selected page whose title and body are both children (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabPage page(&engine, &text_engine, "Label");
    page.add_child(std::make_unique<TabTitle>(&engine));
    page.add_child(std::make_unique<WidgetChildNode>());
    page.set_selected(true);
    // act
    const std::size_t children = page.semantic_child_count();
    // assert only the body is a semantic child, because the title is the tab's visual name (§10.1)
    EXPECT_EQ(children, 1u);
}

TEST(TabViewChecks, MeasuresTheStripAndTheSelectedPage) {
    // arrange two pages under a strip, whose selected page carries a child of its own (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    auto content = std::make_unique<WidgetChildNode>();
    content->m_intrinsic = Size{40.0f, 30.0f};
    second->add_child(std::move(content));
    tabs.add_child(std::move(second_owner));
    tabs.set_selected(1);
    Size measured{};
    // act
    tabs.measure(SizeConstraints::unconstrained(), measured);
    // assert the strip is two minimum tabs wide and the selected page adds its content height (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 128.0f);
    EXPECT_FLOAT_EQ(measured.height, 62.0f);
}

TEST(TabViewChecks, ShowsOnlyTheSelectedPage) {
    // arrange two pages arranged under a strip (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    tabs.add_child(std::move(second_owner));
    // act
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the first page is shown and the second is hidden beside it (§8)
    EXPECT_TRUE(first->visible());
    EXPECT_FALSE(second->visible());
    EXPECT_TRUE(first->selected());
    EXPECT_FALSE(second->selected());
    // assert the headers are one minimum tab wide and sit side by side (§8.2)
    EXPECT_EQ(first->header_bounds(), (Rect::from_origin_size(Point{0.0f, 0.0f}, Size{64.0f, 32.0f})));
    EXPECT_EQ(second->header_bounds(), (Rect::from_origin_size(Point{64.0f, 0.0f}, Size{64.0f, 32.0f})));
    // assert the page body sits under the whole strip, which is what the reader sees (§8.2)
    EXPECT_EQ(second->bounds(), (Rect::from_origin_size(Point{0.0f, 32.0f}, Size{200.0f, 68.0f})));
    // act the second page is selected, which is what a press or a binding does (§8)
    tabs.set_selected(1);
    // assert the shown and hidden pages swap, because exactly one page is visible (§8)
    EXPECT_FALSE(first->visible());
    EXPECT_TRUE(second->visible());
    EXPECT_FALSE(first->selected());
    EXPECT_TRUE(second->selected());
}

TEST(TabViewChecks, APressOnAHeaderSelectsItsPage) {
    // arrange a view whose second header sits at x sixty four and whose first page is shown (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    Event event{.kind = EventKind::PointerDown, .position = Point{74.0f, 10.0f}};
    // act
    tabs.dispatch_event(event);
    // assert the press is claimed and the page it landed on is shown (§8.3)
    EXPECT_TRUE(event.handled);
    EXPECT_EQ(tabs.selected(), 1);
}

TEST(TabViewChecks, TheArrowKeysMoveTheSelection) {
    // arrange a two page view arranged and focused, whose pages are selected by the strip keys (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act the right arrow moves one page forward (§8.3)
    Event right{.kind = EventKind::KeyDown, .key = Key::ArrowRight};
    tabs.dispatch_event(right);
    // assert the next page is shown and the key was claimed (§8.3)
    EXPECT_TRUE(right.handled);
    EXPECT_EQ(tabs.selected(), 1);
    // act the left arrow moves one page back (§8.3)
    Event left{.kind = EventKind::KeyDown, .key = Key::ArrowLeft};
    tabs.dispatch_event(left);
    // assert the previous page is shown again (§8.3)
    EXPECT_TRUE(left.handled);
    EXPECT_EQ(tabs.selected(), 0);
    // act end and home name the two ends of the strip (§8.3)
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    tabs.dispatch_event(end);
    EXPECT_EQ(tabs.selected(), 1);
    Event home{.kind = EventKind::KeyDown, .key = Key::Home};
    tabs.dispatch_event(home);
    EXPECT_EQ(tabs.selected(), 0);
}

TEST(TabViewChecks, ClampsTheSelectionToThePagesItHolds) {
    // arrange two pages in a view whose selection can be written out of range (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    // act the selection lands past the end, which a binding can compute (§8)
    tabs.set_selected(99);
    // assert the last page is the one the view can show (§8)
    EXPECT_EQ(tabs.selected(), 1);
    // act the selection lands before the start, which a binding can compute too (§8)
    tabs.set_selected(-5);
    // assert the first page is the one the view can show (§8)
    EXPECT_EQ(tabs.selected(), 0);
}

TEST(TabViewChecks, RaisesTheSelectionSignal) {
    // arrange a view whose selection moves are recorded by the installed work (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    std::vector<int> changes;
    tabs.set_on_selection_changed([&changes](int index) { changes.push_back(index); });
    // act the selection moves once, then repeats and clamps without moving (§8.3)
    tabs.set_selected(1);
    tabs.set_selected(1);
    tabs.set_selected(99);
    tabs.set_selected(0);
    // assert only the two actual moves were reported, because the signal is a change not a write (§8.3)
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0], 1);
    EXPECT_EQ(changes[1], 0);
}

TEST(TabViewChecks, TheThemeSizesTheStripAndTheIndicator) {
    // arrange a theme whose strip is taller and whose indicator is thicker than the fallback (§8.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    Theme theme;
    theme.set_tab_strip_height(40.0f);
    theme.set_tab_padding_horizontal(10.0f);
    theme.set_tab_minimum_width(80.0f);
    theme.set_tab_indicator_height(3.0f);
    theme.set_tab_indicator(Color::rgb(0.9f, 0.1f, 0.2f));
    TabView tabs(&engine);
    tabs.set_theme(&theme);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    Size measured{};
    // act
    tabs.measure(SizeConstraints::unconstrained(), measured);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    tabs.render(recorder);
    // assert the strip takes the themed height and the two minimum tabs (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 160.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
    // assert the indicator is the themed height and sits on the strip's bottom edge (§6.1)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_FLOAT_EQ(rects[0].y, 37.0f);
    EXPECT_FLOAT_EQ(rects[0].height, 3.0f);
    EXPECT_FLOAT_EQ(rects[0].width, 60.0f);
    // assert the themed colour reached the one shape command (§6.1)
    ASSERT_EQ(recorder.scene()->commands().size(), 3u);
    EXPECT_EQ(recorder.scene()->commands()[0].paint().fill, Color::rgb(0.9f, 0.1f, 0.2f));
    // assert both labels were drawn, because the strip shows every title it holds (§9.1)
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 2u);
}

TEST(TabViewChecks, AnnouncesTheTabGroupAndItsPages) {
    // arrange a view with two identified pages, whose second page carries content (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.set_explicit_id("pages");
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One", "one");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two", "two");
    TabPage* second = second_owner.get();
    second->add_child(std::make_unique<WidgetChildNode>());
    tabs.add_child(std::move(second_owner));
    tabs.set_selected(1);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    tabs.set_focused(true);
    // act
    const SemanticNode group = tabs.semantic_node();
    const SemanticNode page_one = first->semantic_node();
    const SemanticNode page_two = second->semantic_node();
    // assert the group carries the tab view role and the actions that move its selection (§10.1)
    EXPECT_EQ(group.role(), SemanticRole::TabView);
    EXPECT_EQ(group.explicit_id(), "pages");
    EXPECT_TRUE(group.focused());
    EXPECT_EQ(group.action_count(), 4u);
    // assert the unselected page is a tab item under its own name and header (§10.1)
    EXPECT_EQ(page_one.role(), SemanticRole::TabItem);
    EXPECT_EQ(page_one.name(), "One");
    EXPECT_EQ(page_one.bounds(), first->header_bounds());
    EXPECT_FALSE(page_one.selected());
    EXPECT_EQ(page_one.action_count(), 1u);
    // assert the selected page repeats the selection and the focus, which the reader needs (§10.1)
    EXPECT_TRUE(page_two.selected());
    EXPECT_TRUE(page_two.focused());
    // assert only the selected page exposes its content to the walk (§10.1)
    EXPECT_EQ(first->semantic_child_count(), 0u);
    EXPECT_EQ(second->semantic_child_count(), 1u);
    // assert the group still exposes both page nodes, because a reader has to reach them (§10.1)
    ASSERT_EQ(tabs.semantic_child_count(), 2u);
    std::unique_ptr<SemanticProvider> child = tabs.semantic_child(0);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::TabItem);
}

TEST(TabViewChecks, ATabActivationSelectsItsPage) {
    // arrange a view showing the second page, whose first page carries the activate action (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    tabs.set_selected(1);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act
    const SemanticNode page = first->semantic_node();
    const bool activated = page.invoke(1);
    // assert the action selected the page it belongs to, which is what a reader presses (§10.1)
    EXPECT_TRUE(activated);
    EXPECT_EQ(tabs.selected(), 0);
}

TEST(TabViewChecks, VerticalTabsStackDownTheLeft) {
    // arrange a two page view whose strip runs down the left side (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    tabs.add_child(std::move(second_owner));
    tabs.set_vertical(true);
    Size measured{};
    // act
    tabs.measure(SizeConstraints::unconstrained(), measured);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the strip is one tab wide and as tall as both rows, while the page takes the rest (§8.2)
    EXPECT_TRUE(tabs.vertical());
    EXPECT_FLOAT_EQ(measured.width, 64.0f);
    EXPECT_FLOAT_EQ(measured.height, 64.0f);
    EXPECT_EQ(first->header_bounds(), (Rect::from_origin_size(Point{0.0f, 0.0f}, Size{64.0f, 32.0f})));
    EXPECT_EQ(second->header_bounds(), (Rect::from_origin_size(Point{0.0f, 32.0f}, Size{64.0f, 32.0f})));
    EXPECT_EQ(second->bounds(), (Rect::from_origin_size(Point{64.0f, 0.0f}, Size{136.0f, 100.0f})));
    // assert the indicator runs down the left edge of the selected row (§6.1)
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    tabs.render(recorder);
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_EQ(rects[0], (Rect::from_origin_size(Point{0.0f, 12.0f}, Size{2.0f, 8.0f})));
}

TEST(TabViewChecks, AVerticalStripSelectsByRow) {
    // arrange a vertical view with two headers stacked down the left side (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Two"));
    tabs.set_vertical(true);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 40.0f}};
    // act
    tabs.dispatch_event(press);
    // assert the press landed on the second row and selected its page (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(tabs.selected(), 1);
    // act the down arrow moves one row forward, which is the vertical reading order (§8.3)
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    tabs.dispatch_event(down);
    // assert the last row stays selected, because the strip has two ends (§8)
    EXPECT_TRUE(down.handled);
    EXPECT_EQ(tabs.selected(), 1);
    // act the up arrow moves one row back (§8.3)
    Event up{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    tabs.dispatch_event(up);
    // assert the first row is selected again (§8.3)
    EXPECT_EQ(tabs.selected(), 0);
}

TEST(TabViewChecks, AComplexTitleSizesAndDrawsItsHeader) {
    // arrange a second page whose title carries a composed icon rather than a plain run (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto icon = std::make_unique<WidgetChildNode>();
    icon->m_intrinsic = Size{80.0f, 20.0f};
    title->add_child(std::move(icon));
    second->add_child(std::move(title_owner));
    tabs.add_child(std::move(second_owner));
    Size measured{};
    // act
    tabs.measure(SizeConstraints::unconstrained(), measured);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    tabs.render(recorder);
    // assert the title sets the second header's width, which makes the strip wider than two minimum tabs (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 168.0f);
    EXPECT_EQ(second->header_bounds(), (Rect::from_origin_size(Point{64.0f, 0.0f}, Size{104.0f, 32.0f})));
    // assert the title was placed inside the header inset, at the height it measured to (§8.2)
    EXPECT_EQ(title->bounds(), (Rect::from_origin_size(Point{76.0f, 6.0f}, Size{80.0f, 20.0f})));
    // assert the title drew even though its page is not selected, because the strip shows every title (§8)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_EQ(rects[1], title->bounds());
}

TEST(TabViewChecks, AVerticalTitleSizesTheStripWidth) {
    // arrange a vertical view whose second title is wider than the minimum tab (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "One"));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    auto title_owner = std::make_unique<TabTitle>(&engine);
    auto icon = std::make_unique<WidgetChildNode>();
    icon->m_intrinsic = Size{120.0f, 20.0f};
    title_owner->add_child(std::move(icon));
    second->add_child(std::move(title_owner));
    tabs.add_child(std::move(second_owner));
    tabs.set_vertical(true);
    Size measured{};
    // act
    tabs.measure(SizeConstraints::unconstrained(), measured);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{300.0f, 100.0f}));
    // assert the widest title sets the strip width, which both rows share (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 144.0f);
    EXPECT_EQ(second->header_bounds(), (Rect::from_origin_size(Point{0.0f, 32.0f}, Size{144.0f, 32.0f})));
    // assert the page takes the area to the right of the strip (§8.2)
    EXPECT_EQ(second->bounds(), (Rect::from_origin_size(Point{144.0f, 0.0f}, Size{156.0f, 100.0f})));
}

TEST(TabViewChecks, ASameCountReplacementDropsTheRemovedPage) {
    // arrange two pages whose cache is populated before one page leaves (§8.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    tabs.add_child(std::move(second_owner));
    ASSERT_EQ(tabs.tab_count(), 2u);
    // act the first page leaves and a non-page child keeps the child count the same (§8.1)
    std::unique_ptr<UiNode> removed = tabs.remove_child(first);
    tabs.add_child(std::make_unique<WidgetChildNode>());
    // assert the strip holds the surviving page, because a count is not a structural identity (§8.1)
    EXPECT_EQ(removed.get(), first);
    EXPECT_EQ(tabs.tab_count(), 1u);
    EXPECT_EQ(tabs.tab_at(0), second);
}

TEST(TabViewChecks, ReorderingPagesKeepsTheStripInChildOrder) {
    // arrange two pages and a populated cache (§8.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second = second_owner.get();
    tabs.add_child(std::move(second_owner));
    ASSERT_EQ(tabs.tab_count(), 2u);
    // act the first page moves behind the second, which keeps the child count (§8.1)
    std::unique_ptr<UiNode> detached = tabs.remove_child(first);
    tabs.insert_child(1, std::move(detached));
    // assert the strip follows the new child order, not the cached pointer order (§8.1)
    EXPECT_EQ(tabs.tab_at(0), second);
    EXPECT_EQ(tabs.tab_at(1), first);
    // act the first header is pressed, which now belongs to the page in slot zero (§8.3)
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    tabs.dispatch_event(press);
    // assert the page in slot zero is the one selected, which proves the strip order followed the tree (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(tabs.selected(), 0);
    EXPECT_TRUE(second->selected());
    EXPECT_FALSE(first->selected());
}

TEST(TabPageChecks, ASameCountReplacementOfTheTitleClearsIt) {
    // arrange a page whose title cache is populated before the title leaves (§8.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabPage page(&engine, &text_engine, "Page");
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    page.add_child(std::move(title_owner));
    page.add_child(std::make_unique<WidgetChildNode>());
    page.measure_header(13.0f);
    ASSERT_EQ(page.title(), title);
    // act the title leaves and a non-title child keeps the child count the same (§8.1)
    std::unique_ptr<UiNode> removed = page.remove_child(title);
    page.add_child(std::make_unique<WidgetChildNode>());
    // assert the page no longer points at the removed title and its extent is gone (§8.1, §8.2)
    EXPECT_EQ(removed.get(), title);
    EXPECT_EQ(page.title(), nullptr);
    EXPECT_EQ(page.title_size(), Size{});
}

TEST(TabPageChecks, ATitleIsArrangedByTheHeaderNotThePage) {
    // arrange a page with a title and a body child of different sizes (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabPage page(&engine, &text_engine, "Page");
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto icon = std::make_unique<WidgetChildNode>();
    icon->m_intrinsic = Size{80.0f, 20.0f};
    title->add_child(std::move(icon));
    page.add_child(std::move(title_owner));
    page.measure_header(13.0f);
    page.arrange(Rect::from_origin_size(Point{0.0f, 32.0f}, Size{200.0f, 68.0f}));
    // assert the page did not place the title, because the strip owns the header bounds (§8.2)
    EXPECT_TRUE(title->bounds().empty());
}

TEST(TabViewChecks, AnInvisibleTitleFallsBackToTheLabel) {
    // arrange a page whose title is hidden, so the label is the header again (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto page_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* page = page_owner.get();
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto icon = std::make_unique<WidgetChildNode>();
    WidgetChildNode* icon_ptr = icon.get();
    title->add_child(std::move(icon));
    title->set_visible(false);
    page->add_child(std::move(title_owner));
    tabs.add_child(std::move(page_owner));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    tabs.render(recorder);
    // assert the hidden title's content is not drawn and the label is what the strip shows (§8)
    EXPECT_EQ(icon_ptr->draws, 0u);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
}

TEST(TabViewChecks, APressOutsideTheViewSelectsNothing) {
    // arrange a view with four tabs, whose last header overflows the view (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    for (int index = 0; index < 4; index++)
        tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "Tab"));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act the press lands outside the view but inside the overflow header (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{252.0f, 10.0f}};
    tabs.dispatch_event(press);
    // assert the view takes no press outside its bounds, which keeps an overflow header inert (§8.3)
    EXPECT_FALSE(press.handled);
    EXPECT_EQ(tabs.selected(), 0);
}

TEST(TabViewChecks, AVerticalStripStaysInsideTheView) {
    // arrange a vertical view whose title is wider than the view (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto page_owner = std::make_unique<TabPage>(&engine, &text_engine, "Page");
    TabPage* page = page_owner.get();
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto icon = std::make_unique<WidgetChildNode>();
    icon->m_intrinsic = Size{300.0f, 20.0f};
    title->add_child(std::move(icon));
    page->add_child(std::move(title_owner));
    tabs.add_child(std::move(page_owner));
    tabs.set_vertical(true);
    Size measured{};
    tabs.measure(SizeConstraints::unconstrained(), measured);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the strip and its title stay inside the view, so the clip is what hides the overflow (§8.2)
    EXPECT_LE(page->header_bounds().width, 200.0f);
    EXPECT_LE(title->bounds().x + title->bounds().width, 200.0f);
}

TEST(TabViewChecks, TheTitleIsPlacedBeforeTheFirstPaint) {
    // arrange a page with a composed title, measured and arranged but not yet painted (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto page_owner = std::make_unique<TabPage>(&engine, &text_engine, "Page");
    TabPage* page = page_owner.get();
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto icon = std::make_unique<WidgetChildNode>();
    icon->m_intrinsic = Size{80.0f, 20.0f};
    title->add_child(std::move(icon));
    page->add_child(std::move(title_owner));
    tabs.add_child(std::move(page_owner));
    Size measured{};
    // act
    tabs.measure(SizeConstraints::unconstrained(), measured);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the title already has its header geometry, so layout does not depend on a paint pass (§8.2)
    EXPECT_FALSE(title->bounds().empty());
    EXPECT_EQ(title->bounds(), (Rect::from_origin_size(Point{12.0f, 6.0f}, Size{80.0f, 20.0f})));
}

TEST(TabViewChecks, TheHiddenPageTakesNoPointerEvent) {
    // arrange two pages with a button each, and the first page selected (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* first_page = first_owner.get();
    auto first_button = std::make_unique<Button>(&engine, &text_engine, "A");
    Button* first_button_ptr = first_button.get();
    std::size_t first_clicks = 0;
    first_button->set_on_click([&first_clicks] { first_clicks++; });
    first_page->add_child(std::move(first_button));
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "Two");
    TabPage* second_page = second_owner.get();
    auto second_button = std::make_unique<Button>(&engine, &text_engine, "B");
    Button* second_button_ptr = second_button.get();
    std::size_t second_clicks = 0;
    second_button->set_on_click([&second_clicks] { second_clicks++; });
    second_page->add_child(std::move(second_button));
    tabs.add_child(std::move(second_owner));
    tabs.set_selected(0);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    InputController controller;
    controller.attach(tabs);
    Event press{.kind = EventKind::PointerDown, .position = Point{50.0f, 60.0f}};
    // act
    controller.deliver(press);
    // assert the selected page's button took the press and the hidden page's button did not (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(first_clicks, 1u);
    EXPECT_EQ(second_clicks, 0u);
    EXPECT_EQ(controller.focus().focused(), first_button_ptr);
    // act the second page is selected, which hides the first and clears its caret (§8.3)
    tabs.set_selected(1);
    // assert the hidden button loses the focus and cannot take it again until shown (§8.3)
    EXPECT_NE(controller.focus().focused(), first_button_ptr);
    EXPECT_FALSE(FocusManager::focusable(*first_button_ptr));
    EXPECT_TRUE(controller.focus().focused() == nullptr || controller.focus().focused() == second_page || controller.focus().focused() == second_button_ptr);
    // act a space key arrives after the hidden button lost the caret (§8.3)
    Event space{.kind = EventKind::KeyDown, .key = Key::Space};
    controller.deliver(space);
    // assert the hidden button did not activate, which a hidden page must never do (§8.3)
    EXPECT_EQ(first_clicks, 1u);
}

TEST(TabViewChecks, ATitleChildDoesNotTakeTheHeaderPress) {
    // arrange a selected page whose title holds an interactive child (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto page_owner = std::make_unique<TabPage>(&engine, &text_engine, "One");
    TabPage* page = page_owner.get();
    auto title_owner = std::make_unique<TabTitle>(&engine);
    TabTitle* title = title_owner.get();
    auto title_button = std::make_unique<Button>(&engine, &text_engine, "T");
    Button* title_button_ptr = title_button.get();
    std::size_t title_clicks = 0;
    title_button->set_on_click([&title_clicks] { title_clicks++; });
    title->add_child(std::move(title_button));
    page->add_child(std::move(title_owner));
    tabs.add_child(std::move(page_owner));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act the header is pressed, which is the tab's target rather than the title's (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 10.0f}};
    tabs.dispatch_event(press);
    // assert the tab selected and the title's button never ran (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(tabs.selected(), 0);
    EXPECT_EQ(title_clicks, 0u);
    // assert the title and its child are not focus stops, because the strip owns the interaction (§8.3)
    EXPECT_FALSE(title->hit_test(Point{10.0f, 10.0f}));
    EXPECT_FALSE(FocusManager::focusable(*title));
    EXPECT_FALSE(FocusManager::focusable(*title_button_ptr));
}

TEST(TabViewChecks, RemovingAPageBeforeTheSelectionKeepsThePageShown) {
    // arrange four pages with the third selected (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    auto first_owner = std::make_unique<TabPage>(&engine, &text_engine, "A");
    TabPage* first = first_owner.get();
    tabs.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "B");
    tabs.add_child(std::move(second_owner));
    auto third_owner = std::make_unique<TabPage>(&engine, &text_engine, "C");
    TabPage* third = third_owner.get();
    tabs.add_child(std::move(third_owner));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "D"));
    tabs.set_selected(2);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act the page before it leaves, which moves the selected index (§8)
    std::unique_ptr<UiNode> removed = tabs.remove_child(first);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the same page is still shown at its new index (§8)
    EXPECT_EQ(removed.get(), first);
    EXPECT_EQ(tabs.selected(), 1);
    EXPECT_TRUE(third->selected());
    EXPECT_TRUE(third->visible());
}

TEST(TabViewChecks, InsertingAPageBeforeTheSelectionKeepsThePageShown) {
    // arrange two pages with the second selected (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "A"));
    auto second_owner = std::make_unique<TabPage>(&engine, &text_engine, "B");
    TabPage* second = second_owner.get();
    tabs.add_child(std::move(second_owner));
    tabs.set_selected(1);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act a page is inserted before it (§8)
    tabs.insert_child(0, std::make_unique<TabPage>(&engine, &text_engine, "New"));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the same page is still shown at its new index (§8)
    EXPECT_EQ(tabs.selected(), 2);
    EXPECT_TRUE(second->selected());
    EXPECT_TRUE(second->visible());
}

TEST(TabViewChecks, RemovingTheSelectedLastPageDoesNotJumpOnTheNextAdd) {
    // arrange three pages with the last selected (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "A"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "B"));
    auto third_owner = std::make_unique<TabPage>(&engine, &text_engine, "C");
    TabPage* third = third_owner.get();
    tabs.add_child(std::move(third_owner));
    tabs.set_selected(2);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // act the selected page leaves, which clamps the selection (§8)
    std::unique_ptr<UiNode> removed = tabs.remove_child(third);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    EXPECT_EQ(removed.get(), third);
    EXPECT_EQ(tabs.selected(), 1);
    // act a page is added after the removal (§8)
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "D"));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the selection stayed at the clamped index instead of jumping to the added page (§8)
    EXPECT_EQ(tabs.selected(), 1);
}

TEST(TabViewChecks, AStructuralSelectionChangeRaisesTheSignal) {
    // arrange two pages with the second selected and a recording handler (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    TabView tabs(&engine);
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "A"));
    tabs.add_child(std::make_unique<TabPage>(&engine, &text_engine, "B"));
    tabs.set_selected(1);
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    std::vector<int> changes;
    tabs.set_on_selection_changed([&changes](int index) { changes.push_back(index); });
    // act a page is inserted before the selection, which moves the selected index (§8)
    tabs.insert_child(0, std::make_unique<TabPage>(&engine, &text_engine, "New"));
    tabs.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 100.0f}));
    // assert the model was told the new index, because the displayed page moved (§8.3)
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0], 2);
    EXPECT_EQ(tabs.selected(), 2);
}

TEST(RadioButtonChecks, MeasuresTheCircleAndLabel) {
    // arrange a choice whose label is measured by the shared text engine (§8.2, §9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "One");
    Size measured{};
    // act
    choice.measure(SizeConstraints::unconstrained(), measured);
    // assert the row is the circle, the gap and the run (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 52.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(RadioButtonChecks, AnEmptyLabelMeasuresOnlyTheCircle) {
    // arrange a choice with no label, which a caller might use beside a separate label (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine);
    Size measured{};
    // act
    choice.measure(SizeConstraints::unconstrained(), measured);
    // assert the gap is not added when there is no run to separate (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 16.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(RadioButtonChecks, ALongLabelWrapsInsideTheOfferedRow) {
    // arrange a label wider than the room the parent offers (§8.2, §9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "abcdefghij");
    Size measured{};
    // act
    choice.measure(SizeConstraints::at_most(Size{80.0f, 40.0f}), measured);
    // assert the row stays inside the offered ceiling, because the label is measured against it (§8.2)
    EXPECT_LE(measured.width, 80.0f);
    EXPECT_FLOAT_EQ(measured.width, 80.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(RadioButtonChecks, MeasuresFromTheInstalledTheme) {
    // arrange a theme that sizes the circle and the gap differently from the default (§8.4)
    Theme theme;
    theme.set_radio_button_size(Size{20.0f, 20.0f});
    theme.set_radio_button_gap(5.0f);
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "A");
    apply_theme(choice, theme);
    Size measured{};
    // act
    choice.measure(SizeConstraints::unconstrained(), measured);
    // assert the row follows the theme rather than the builtin literals (§8.2, §8.4)
    EXPECT_FLOAT_EQ(measured.width, 35.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(RadioButtonChecks, ASquareCircleFitsAShortRow) {
    // arrange a row shorter than the themed circle, which a shrunken group can produce (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "");
    choice.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{52.0f, 6.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{80.0f, 20.0f}, 1.0f));
    // act
    choice.render(recorder);
    // assert the outline is a six by six circle inside the row, not a sixteen high circle hanging out (§8.2)
    const std::vector<Rect> rects = rect_commands(*recorder.scene());
    ASSERT_FALSE(rects.empty());
    EXPECT_FLOAT_EQ(rects.front().width, 6.0f);
    EXPECT_FLOAT_EQ(rects.front().height, 6.0f);
    EXPECT_GE(rects.front().y, 0.0f);
    EXPECT_LE(rects.front().y + rects.front().height, 6.0f);
}

TEST(RadioButtonChecks, APressRunsTheInstalledSelection) {
    // arrange a choice with a recorded selection and an arranged row (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "One");
    choice.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{52.0f, 16.0f}));
    std::size_t selections = 0;
    choice.set_on_select([&selections] { selections++; });
    Event press{.kind = EventKind::PointerDown, .position = Point{20.0f, 8.0f}};
    // act
    choice.dispatch_event(press);
    // assert the row claims the press and runs the group's selection work (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(selections, 1u);
}

TEST(RadioButtonChecks, ASpaceRunsTheInstalledSelection) {
    // arrange a focused choice with a recorded selection (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "One");
    choice.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{52.0f, 16.0f}));
    std::size_t selections = 0;
    choice.set_on_select([&selections] { selections++; });
    Event space{.kind = EventKind::KeyDown, .key = Key::Space};
    // act
    choice.dispatch_event(space);
    // assert space is the keyboard selection, which the choice claims (§8.3)
    EXPECT_TRUE(space.handled);
    EXPECT_EQ(selections, 1u);
}

TEST(RadioButtonChecks, ADisabledChoiceRefusesThePress) {
    // arrange a disabled choice, which draws its state but takes no input (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "One");
    choice.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{52.0f, 16.0f}));
    choice.set_disabled(true);
    std::size_t selections = 0;
    choice.set_on_select([&selections] { selections++; });
    Event press{.kind = EventKind::PointerDown, .position = Point{20.0f, 8.0f}};
    // act
    choice.dispatch_event(press);
    // assert the press was not claimed and the choice was not selected (§8.3)
    EXPECT_FALSE(press.handled);
    EXPECT_EQ(selections, 0u);
}

TEST(RadioButtonChecks, AnnouncesItsLabelAndChosenState) {
    // arrange a chosen choice with a label and an installed selection (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 2, "One", "one");
    choice.arrange(Rect::from_origin_size(Point{1.0f, 2.0f}, Size{52.0f, 16.0f}));
    choice.set_checked(true);
    choice.set_on_select([] {});
    // act
    const SemanticNode node = choice.semantic_node();
    // assert the role, name, check state and selection are what a reader announces (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::RadioButton);
    EXPECT_EQ(node.name(), "One");
    EXPECT_EQ(node.check(), CheckState::Checked);
    EXPECT_TRUE(node.selected());
    EXPECT_EQ(node.bounds(), (Rect::from_origin_size(Point{1.0f, 2.0f}, Size{52.0f, 16.0f})));
    EXPECT_EQ(node.action_count(), 1u);
}

TEST(RadioButtonChecks, PaintsTheOutlineAndADotWhenChosen) {
    // arrange two rows drawn from the same theme (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "One");
    choice.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{52.0f, 16.0f}));
    SceneRecorder unchecked(WindowMetrics::from_logical(Size{80.0f, 40.0f}, 1.0f));
    choice.render(unchecked);
    // act
    choice.set_checked(true);
    SceneRecorder checked(WindowMetrics::from_logical(Size{80.0f, 40.0f}, 1.0f));
    choice.render(checked);
    // assert the unchecked row is one outline and the chosen row adds a fill and a dot (§8.4)
    EXPECT_EQ(rect_commands(*unchecked.scene()).size(), 1u);
    EXPECT_EQ(rect_commands(*checked.scene()).size(), 3u);
    // assert the label is one run in both states (§9.1)
    EXPECT_EQ(text_commands(*unchecked.scene()).size(), 1u);
    EXPECT_EQ(text_commands(*checked.scene()).size(), 1u);
}

TEST(RadioButtonChecks, SetLabelRemeasuresTheRow) {
    // arrange a measured choice with a short label (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioButton choice(&engine, &text_engine, 0, "A");
    Size measured{};
    choice.measure(SizeConstraints::unconstrained(), measured);
    const float before = measured.width;
    // act the label grows, which changes the row width (§8.2)
    choice.set_label("Four");
    choice.measure(SizeConstraints::unconstrained(), measured);
    // assert the row follows the new run (§8.2)
    EXPECT_GT(measured.width, before);
    EXPECT_TRUE(choice.layout_dirty());
}

TEST(RadioGroupChecks, MeasuresItsChoicesInARow) {
    // arrange two choices in a horizontal group, with the default gap (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 0, "One"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    Size measured{};
    // act
    group.measure(SizeConstraints::unconstrained(), measured);
    // assert the row is both choices and the gap between them (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 112.0f);
    EXPECT_FLOAT_EQ(measured.height, 16.0f);
}

TEST(RadioGroupChecks, MeasuresItsChoicesInAColumn) {
    // arrange two choices in a vertical group, with the default gap (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 0, "One"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    group.set_vertical(true);
    Size measured{};
    // act
    group.measure(SizeConstraints::unconstrained(), measured);
    // assert the column is the widest choice and both rows plus the gap (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 52.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
    EXPECT_TRUE(group.vertical());
}

TEST(RadioGroupChecks, SelectsTheChoiceWithTheValue) {
    // arrange a group with two values (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    auto first_owner = std::make_unique<RadioButton>(&engine, &text_engine, 0, "One");
    RadioButton* first = first_owner.get();
    group.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two");
    RadioButton* second = second_owner.get();
    group.add_child(std::move(second_owner));
    // act
    group.set_selected(1);
    group.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // assert exactly the choice with the value is checked (§8)
    EXPECT_EQ(group.selected(), 1);
    EXPECT_FALSE(first->checked());
    EXPECT_TRUE(second->checked());
}

TEST(RadioGroupChecks, AShrunkGroupKeepsEveryChoiceMeasured) {
    // arrange a group of three choices that needs more width than the parent gives it (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 0, "One"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 2, "Six"));
    // act
    group.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 20.0f}));
    // assert every choice keeps a positive width inside the group, because no choice may vanish or fall outside (§8.2)
    ASSERT_EQ(group.child_count(), 3U);
    EXPECT_FLOAT_EQ(group.child_at(0)->bounds().width, 28.0f);
    EXPECT_FLOAT_EQ(group.child_at(1)->bounds().width, 28.0f);
    EXPECT_FLOAT_EQ(group.child_at(2)->bounds().width, 28.0f);
    EXPECT_FLOAT_EQ(group.child_at(1)->bounds().x, 36.0f);
    EXPECT_FLOAT_EQ(group.child_at(2)->bounds().x, 72.0f);
    EXPECT_LE(group.child_at(2)->bounds().x + group.child_at(2)->bounds().width, 100.0f);
}

TEST(RadioGroupChecks, UsesTheThemeSpacingWhenNoneIsWritten) {
    // arrange a theme that spaces choices wider than the builtin default (§8.4)
    Theme theme;
    theme.set_radio_group_spacing(20.0f);
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 0, "One"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    apply_theme(group, theme);
    Size measured{};
    // act
    group.measure(SizeConstraints::unconstrained(), measured);
    // assert an unwritten gap comes from the theme (§8.2, §8.4)
    EXPECT_FLOAT_EQ(group.spacing(), 20.0f);
    EXPECT_FLOAT_EQ(measured.width, 124.0f);
}

TEST(RadioGroupChecks, AChoiceValueChangeReappliesTheSelection) {
    // arrange a group whose selected value is the second choice (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    auto first_owner = std::make_unique<RadioButton>(&engine, &text_engine, 0, "One");
    RadioButton* first = first_owner.get();
    group.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two");
    RadioButton* second = second_owner.get();
    group.add_child(std::move(second_owner));
    group.set_selected(1);
    group.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act the first choice takes the selected value, which the group must reapply (§8)
    first->set_value(1);
    // assert the new match is the one checked choice, because the first match wins (§8)
    EXPECT_TRUE(first->checked());
    EXPECT_FALSE(second->checked());
}

TEST(RadioGroupChecks, ASelectedValueWrittenBeforeTheChildrenMarksTheChoice) {
    // arrange a group whose selected value is written before the choices are adopted (§8, §11.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.set_selected(1);
    auto first_owner = std::make_unique<RadioButton>(&engine, &text_engine, 0, "One");
    RadioButton* first = first_owner.get();
    group.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two");
    RadioButton* second = second_owner.get();
    group.add_child(std::move(second_owner));
    // assert adding the matching choice marks it before any layout or semantic read (§8)
    EXPECT_TRUE(second->checked());
    // act the semantic tree is built before the first layout pass (§10.1)
    const SemanticNode node = group.semantic_node();
    // assert the group announces its selection and the matching choice carries the check state (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::RadioGroup);
    EXPECT_FALSE(first->checked());
    EXPECT_TRUE(second->checked());
    std::unique_ptr<SemanticProvider> child = group.semantic_child(1);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().check(), CheckState::Checked);
}

TEST(RadioGroupChecks, AChoicePressSelectsItsValue) {
    // arrange an arranged group with a recorded selection signal (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 0, "One"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    group.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    std::vector<int> changes;
    group.set_on_selection_changed([&changes](int value) { changes.push_back(value); });
    Event press{.kind = EventKind::PointerDown, .position = Point{65.0f, 8.0f}};
    // act
    InputDispatcher::dispatch(group, press);
    // assert the second choice claimed the press and became the selected value (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_EQ(group.selected(), 1);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0], 1);
}

TEST(RadioGroupChecks, AChoiceCannotReachADestroyedGroup) {
    // arrange a choice detached from its group, which keeps the callback the group installed (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    auto group = std::make_unique<RadioGroup>(&engine);
    auto choice = std::make_unique<RadioButton>(&engine, &text_engine, 0, "One");
    RadioButton* raw = choice.get();
    group->add_child(std::move(choice));
    group->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    std::unique_ptr<UiNode> detached = group->remove_child(raw);
    ASSERT_NE(detached, nullptr);
    // act the detached choice is pressed while the group is still alive (§8.3)
    Event detached_press{.kind = EventKind::PointerDown, .position = Point{10.0f, 8.0f}};
    raw->dispatch_event(detached_press);
    // assert the removed choice cannot drive the live group (§8.3)
    EXPECT_EQ(group->selected(), 0);
    // act the detached choice is cleared, the group is destroyed, and the choice is pressed afterwards (§8.3, AD-09)
    raw->set_checked(false);
    group.reset();
    Event press{.kind = EventKind::PointerDown, .position = Point{10.0f, 8.0f}};
    raw->dispatch_event(press);
    // assert the choice is still a valid control and the press is claimed without calling the dead group (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_FALSE(raw->checked());
}

TEST(RadioGroupChecks, DuplicateValuesSelectTheFirstChoice) {
    // arrange two choices with the same value, which the group selects by value (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    auto first_owner = std::make_unique<RadioButton>(&engine, &text_engine, 0, "One");
    RadioButton* first = first_owner.get();
    group.add_child(std::move(first_owner));
    auto second_owner = std::make_unique<RadioButton>(&engine, &text_engine, 0, "Two");
    RadioButton* second = second_owner.get();
    group.add_child(std::move(second_owner));
    std::vector<int> changes;
    group.set_on_selection_changed([&changes](int value) { changes.push_back(value); });
    // act the second duplicate is pressed, which carries the same selected value (§8)
    group.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{65.0f, 8.0f}};
    InputDispatcher::dispatch(group, press);
    // assert the first matching choice owns the check and no new selection moved (§8)
    EXPECT_TRUE(first->checked());
    EXPECT_FALSE(second->checked());
    EXPECT_TRUE(changes.empty());
}

TEST(RadioGroupChecks, TheSignalReportsOnlyActualChanges) {
    // arrange a group with a recorded signal (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 0, "One"));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    std::vector<int> changes;
    group.set_on_selection_changed([&changes](int value) { changes.push_back(value); });
    // act the same value is written twice, which is one actual move (§8.3)
    group.set_selected(1);
    group.set_selected(1);
    // assert only the move was reported (§8.3)
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0], 1);
}

TEST(RadioGroupChecks, ExposesItsChoicesAsRadioButtons) {
    // arrange a group with one selected choice (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    RadioGroup group(&engine);
    group.set_explicit_id("modes");
    auto first_owner = std::make_unique<RadioButton>(&engine, &text_engine, 0, "One");
    RadioButton* first = first_owner.get();
    group.add_child(std::move(first_owner));
    group.add_child(std::make_unique<RadioButton>(&engine, &text_engine, 1, "Two"));
    group.set_selected(0);
    group.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 20.0f}));
    // act
    const SemanticNode node = group.semantic_node();
    // assert the group and its choice hierarchy are announced (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::RadioGroup);
    EXPECT_EQ(node.explicit_id(), "modes");
    EXPECT_EQ(node.action_count(), 2u);
    EXPECT_EQ(group.semantic_child_count(), 2u);
    std::unique_ptr<SemanticProvider> child = group.semantic_child(0);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::RadioButton);
    EXPECT_EQ(first->semantic_node().check(), CheckState::Checked);
    EXPECT_FALSE(group.accepts_focus());
}

TEST(NumericStepperChecks, MeasuresTheDefaultPlate) {
    // arrange a stepper whose value is a short run (§8.2, §9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    Size measured{};
    // act
    stepper.measure(SizeConstraints::unconstrained(), measured);
    // assert the themed extent is the floor the run fits inside (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 120.0f);
    EXPECT_FLOAT_EQ(measured.height, 32.0f);
}

TEST(NumericStepperChecks, MeasuresFromTheInstalledTheme) {
    // arrange a theme that sizes the plate and its step zones (§8.4)
    Theme theme;
    theme.set_numeric_stepper_extent(Size{200.0f, 40.0f});
    theme.set_numeric_stepper_button_width(30.0f);
    theme.set_numeric_stepper_padding(5.0f);
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    apply_theme(stepper, theme);
    Size measured{};
    // act
    stepper.measure(SizeConstraints::unconstrained(), measured);
    // assert the plate takes the themed extent, because the run fits inside it (§8.2, §8.4)
    EXPECT_FLOAT_EQ(measured.width, 200.0f);
    EXPECT_FLOAT_EQ(measured.height, 40.0f);
}

TEST(NumericStepperChecks, AWidenedRangeRecoversTheAuthoredValue) {
    // arrange a stepper whose zero default range clamps the authored thirty to zero (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_maximum(0.0f);
    stepper.set_value(30.0f);
    // assert the value is clamped while the range is zero wide (§8)
    EXPECT_FLOAT_EQ(stepper.value(), 0.0f);
    // act the range widens after the value was requested, which happens when a bound host value settles (§7.3, §8)
    stepper.set_maximum(100.0f);
    // assert the request survives the temporary clamp, so the control shows the authored thirty (§8)
    EXPECT_FLOAT_EQ(stepper.value(), 30.0f);
    // act a user write replaces the request, which must not jump back when the range moves again (§8)
    stepper.set_value(0.0f);
    stepper.set_maximum(50.0f);
    // assert the later range move keeps the user's value rather than the old authored request (§8)
    EXPECT_FLOAT_EQ(stepper.value(), 0.0f);
}

TEST(NumericStepperChecks, ClampsTheValueIntoItsRange) {
    // arrange a stepper with a ten to twenty range (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_minimum(10.0f);
    stepper.set_maximum(20.0f);
    // act the value lands below and above the range (§8)
    stepper.set_value(5.0f);
    // assert the lower bound is what the control shows (§8)
    EXPECT_FLOAT_EQ(stepper.value(), 10.0f);
    stepper.set_value(25.0f);
    // assert the upper bound is what the control shows (§8)
    EXPECT_FLOAT_EQ(stepper.value(), 20.0f);
}

TEST(NumericStepperChecks, APressOnThePlusZoneStepsUp) {
    // arrange an arranged stepper with a recorded value signal (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    std::vector<float> changes;
    stepper.set_on_change([&changes](float value) { changes.push_back(value); });
    Event press{.kind = EventKind::PointerDown, .position = Point{110.0f, 16.0f}};
    // act
    stepper.dispatch_event(press);
    // assert the increment zone steps one unit and reports it (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 1.0f);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_FLOAT_EQ(changes[0], 1.0f);
}

TEST(NumericStepperChecks, TheStepAmountMovesTheValue) {
    // arrange a half-unit step between zero and ten (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_maximum(10.0f);
    stepper.set_step(0.5f);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{110.0f, 16.0f}};
    stepper.dispatch_event(press);
    // assert the stored step is what the zone moves by (§8.3)
    EXPECT_FLOAT_EQ(stepper.value(), 0.5f);
    EXPECT_FLOAT_EQ(stepper.step(), 0.5f);
}

TEST(NumericStepperChecks, TheMinusZoneStepsDown) {
    // arrange a stepper carrying five in the middle of its range (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_value(5.0f);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    // act a press on the left zone (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{5.0f, 16.0f}};
    stepper.dispatch_event(press);
    // assert the decrement zone moves the value back by one step (§8.3)
    EXPECT_TRUE(press.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 4.0f);
}

TEST(NumericStepperChecks, ADownWheelNotchStepsTheValueDown) {
    // arrange a stepper carrying five (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_value(5.0f);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    Event wheel{.kind = EventKind::Wheel, .position = Point{60.0f, 16.0f}, .scroll = -1.0f};
    // act
    stepper.dispatch_event(wheel);
    // assert the notch direction decides the step (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 4.0f);
}

TEST(NumericStepperChecks, ThePageKeysMoveTenSteps) {
    // arrange a stepper at five with unit steps (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_value(5.0f);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    // act
    Event up{.kind = EventKind::KeyDown, .key = Key::PageUp};
    stepper.dispatch_event(up);
    // assert a page is ten steps forward (§8.3)
    EXPECT_TRUE(up.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 15.0f);
    Event down{.kind = EventKind::KeyDown, .key = Key::PageDown};
    stepper.dispatch_event(down);
    // assert a page back is ten steps, which mirrors the forward move (§8.3)
    EXPECT_TRUE(down.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 5.0f);
}

TEST(NumericStepperChecks, AWheelNotchStepsTheValue) {
    // arrange a stepper with a recorded value signal (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    std::vector<float> changes;
    stepper.set_on_change([&changes](float value) { changes.push_back(value); });
    Event wheel{.kind = EventKind::Wheel, .position = Point{40.0f, 16.0f}, .scroll = 1.0f};
    // act
    stepper.dispatch_event(wheel);
    // assert the notch steps up and claims the event (§8.3)
    EXPECT_TRUE(wheel.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 1.0f);
    ASSERT_EQ(changes.size(), 1u);
}

TEST(NumericStepperChecks, ANarrowPlatePlacesThePlusMarkInThePlusZone) {
    // arrange a plate narrower than two ideal step zones, where the old left check overlapped the right zone (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{40.0f, 32.0f}));
    // act the press lands under the plus mark, at twenty six points (§8.3)
    Event plus{.kind = EventKind::PointerDown, .position = Point{26.0f, 16.0f}};
    stepper.dispatch_event(plus);
    // assert the plus mark increments, because the two zones split at the middle (§8.3)
    EXPECT_TRUE(plus.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 1.0f);
    // act the left half decrements, because the zones are disjoint (§8.3)
    Event minus{.kind = EventKind::PointerDown, .position = Point{5.0f, 16.0f}};
    stepper.dispatch_event(minus);
    // assert the decrement zone still steps the value back (§8.3)
    EXPECT_TRUE(minus.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 0.0f);
}

TEST(NumericStepperChecks, ANonFiniteBindingIsIgnored) {
    // arrange a stepper carrying a finite value, bound, and step (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_value(5.0f);
    // act each input is non finite, which no valid range or step can carry (§8)
    stepper.set_value(std::numeric_limits<float>::quiet_NaN());
    stepper.set_minimum(std::numeric_limits<float>::quiet_NaN());
    stepper.set_maximum(std::numeric_limits<float>::infinity());
    stepper.set_step(std::numeric_limits<float>::infinity());
    // assert the finite state survives, because a poisoned value would break every later clamp (§8)
    EXPECT_FLOAT_EQ(stepper.value(), 5.0f);
    EXPECT_FLOAT_EQ(stepper.minimum(), 0.0f);
    EXPECT_FLOAT_EQ(stepper.maximum(), 100.0f);
    EXPECT_FLOAT_EQ(stepper.step(), 1.0f);
}

TEST(NumericStepperChecks, ANegativeStepUsesItsMagnitude) {
    // arrange a written negative step, which must not reverse the buttons (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_step(-2.0f);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    // act
    Event press{.kind = EventKind::PointerDown, .position = Point{110.0f, 16.0f}};
    stepper.dispatch_event(press);
    // assert the stored step is the magnitude and the plus zone still increments (§8.3)
    EXPECT_FLOAT_EQ(stepper.step(), 2.0f);
    EXPECT_FLOAT_EQ(stepper.value(), 2.0f);
}

TEST(NumericStepperChecks, TheHomeAndEndKeysReachTheBounds) {
    // arrange a stepper arranged for key input (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_minimum(2.0f);
    stepper.set_maximum(12.0f);
    stepper.set_value(5.0f);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    // act
    Event end{.kind = EventKind::KeyDown, .key = Key::End};
    stepper.dispatch_event(end);
    // assert end names the upper bound (§8.3)
    EXPECT_TRUE(end.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 12.0f);
    Event home{.kind = EventKind::KeyDown, .key = Key::Home};
    stepper.dispatch_event(home);
    // assert home names the lower bound (§8.3)
    EXPECT_TRUE(home.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 2.0f);
}

TEST(NumericStepperChecks, AnnouncesItsValueAndSteppingActions) {
    // arrange a stepper carrying a formatted fraction (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine, "effort");
    stepper.set_value(3.5f);
    // act
    const SemanticNode node = stepper.semantic_node();
    // assert the role, id, value and the two actions a reader can drive (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::NumericStepper);
    EXPECT_EQ(node.explicit_id(), "effort");
    EXPECT_EQ(node.value(), "3.5");
    EXPECT_EQ(node.action_count(), 2u);
    const bool incremented = node.invoke(1);
    // assert the increment action is the one the plate's plus zone runs (§10.1)
    EXPECT_TRUE(incremented);
    EXPECT_FLOAT_EQ(stepper.value(), 4.5f);
}

TEST(NumericStepperChecks, AWholeNumberFormatsWithoutTrailingDecimals) {
    // arrange a stepper with a whole value (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_value(12.0f);
    // act
    const SemanticNode node = stepper.semantic_node();
    // assert the value run is the shortest integer form (§9.1)
    EXPECT_EQ(node.value(), "12");
}

TEST(NumericStepperChecks, ATinyNegativeValueDoesNotFormatAsNegativeZero) {
    // arrange a value that rounds to zero at the displayed precision (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_minimum(-1.0f);
    stepper.set_value(-0.0000004f);
    // act
    const SemanticNode node = stepper.semantic_node();
    // assert the signed tiny value reads as zero rather than the misleading -0 (§9.1)
    EXPECT_FLOAT_EQ(stepper.value(), -0.0000004f);
    EXPECT_EQ(node.value(), "0");
}

TEST(NumericStepperChecks, ADisabledStepperRefusesThePress) {
    // arrange a disabled stepper (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_disabled(true);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{110.0f, 16.0f}};
    // act
    stepper.dispatch_event(press);
    // assert the press is not claimed and the value does not move (§8.3)
    EXPECT_FALSE(press.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 0.0f);
    EXPECT_TRUE(stepper.disabled());
}

TEST(NumericStepperChecks, ADisabledStepperRefusesTheWheelAndTheKey) {
    // arrange a disabled stepper with a value to protect (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.set_value(5.0f);
    stepper.set_disabled(true);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    // act
    Event wheel{.kind = EventKind::Wheel, .position = Point{60.0f, 16.0f}, .scroll = 1.0f};
    stepper.dispatch_event(wheel);
    Event key{.kind = EventKind::KeyDown, .key = Key::ArrowUp};
    stepper.dispatch_event(key);
    // assert neither path is claimed and the value stays put (§8.3)
    EXPECT_FALSE(wheel.handled);
    EXPECT_FALSE(key.handled);
    EXPECT_FLOAT_EQ(stepper.value(), 5.0f);
}

TEST(NumericStepperChecks, ThePlateAndTheTwoMarksArePainted) {
    // arrange an arranged stepper drawn from the theme (§6.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    NumericStepper stepper(&engine, &text_engine);
    stepper.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{120.0f, 32.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{160.0f, 60.0f}, 1.0f));
    // act
    stepper.render(recorder);
    // assert the plate, the minus and the two plus bars are four shapes and the value is one run (§8.4)
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 4u);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
}

TEST(ImageChecks, AnImageMeasuresToTheNaturalPixelExtent) {
    // arrange a three by two image, whose extent is what an unconstrained layout asks for (§6.4, §8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[3 * 2 * 4]{};
    const std::shared_ptr<const ImageResource> source = factory.create_image(3, 2, ImageFormat::RGBA8, pixels);
    Image image(&engine);
    image.set_source(source);
    Size measured{};
    // act
    image.measure(SizeConstraints::unconstrained(), measured);
    // assert the image offers its pixel extent, because no explicit size was written (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 3.0f);
    EXPECT_FLOAT_EQ(measured.height, 2.0f);
}

TEST(ImageChecks, ExplicitDimensionsReplaceTheNaturalExtentIndependently) {
    // arrange a three by two image with one explicit dimension at a time (§8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[3 * 2 * 4]{};
    Image image(&engine);
    image.set_source(factory.create_image(3, 2, ImageFormat::RGBA8, pixels));
    Size measured{};
    // act the width alone, then both dimensions, then a zero width
    image.set_width(30.0f);
    image.measure(SizeConstraints::unconstrained(), measured);
    // assert the height keeps the natural extent while the width follows the author (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 2.0f);
    image.set_height(10.0f);
    image.measure(SizeConstraints::unconstrained(), measured);
    // assert both dimensions now follow the author (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 30.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
    image.set_width(0.0f);
    image.measure(SizeConstraints::unconstrained(), measured);
    // assert zero restores the natural width while the explicit height stays (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 3.0f);
    EXPECT_FLOAT_EQ(measured.height, 10.0f);
}

TEST(ImageChecks, AnExplicitDimensionRequestsLayout) {
    // arrange an image whose source already settled its natural extent (§8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[2 * 2 * 4]{};
    Image image(&engine);
    image.set_source(factory.create_image(2, 2, ImageFormat::RGBA8, pixels));
    image.clear_dirty();
    // act set each explicit dimension in turn, because either one changes the offered extent (§8.2)
    image.set_width(12.0f);
    // assert a written width invalidates the measure pass, because the offered extent changed (§8.2)
    EXPECT_TRUE(image.layout_dirty());
    image.clear_dirty();
    image.set_height(8.0f);
    // assert the height does the same, so one axis cannot move without a measure (§8.2)
    EXPECT_TRUE(image.layout_dirty());
}

TEST(ImageChecks, ConstraintsClampTheNaturalExtent) {
    // arrange a four by three image under a parent that offers less room (§8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[4 * 3 * 4]{};
    Image image(&engine);
    image.set_source(factory.create_image(4, 3, ImageFormat::RGBA8, pixels));
    Size measured{};
    // act
    image.measure(SizeConstraints::at_most(Size{2.0f, 2.0f}), measured);
    // assert the parent decision is final, even against the natural extent (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 2.0f);
    EXPECT_FLOAT_EQ(measured.height, 2.0f);
}

TEST(ImageChecks, TightConstraintsRaiseTheNaturalExtent) {
    // arrange a two by two image under a parent that demands a larger rectangle (§8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[2 * 2 * 4]{};
    Image image(&engine);
    image.set_source(factory.create_image(2, 2, ImageFormat::RGBA8, pixels));
    Size measured{};
    // act
    image.measure(SizeConstraints::tight(Size{5.0f, 4.0f}), measured);
    // assert the minimum raises both axes, because a tight parent decides the extent (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 5.0f);
    EXPECT_FLOAT_EQ(measured.height, 4.0f);
}

TEST(ImageChecks, ANegativeDimensionMeansTheNaturalExtent) {
    // arrange an image whose width is written below zero, which is no size at all (§8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[2 * 2 * 4]{};
    Image image(&engine);
    image.set_source(factory.create_image(2, 2, ImageFormat::RGBA8, pixels));
    Size measured{};
    // act
    image.set_width(-12.0f);
    image.measure(SizeConstraints::unconstrained(), measured);
    // assert the width clamps to zero, which means the natural pixel width (§8.2)
    EXPECT_FLOAT_EQ(image.width(), 0.0f);
    EXPECT_FLOAT_EQ(measured.width, 2.0f);
}

TEST(ImageChecks, ANewSourceRemeasuresTheImage) {
    // arrange an image measured from one source and then replaced (§6.4)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[4 * 3 * 4]{};
    std::uint8_t small[4]{};
    Image image(&engine);
    image.set_source(factory.create_image(4, 3, ImageFormat::RGBA8, pixels));
    Size measured{};
    image.measure(SizeConstraints::unconstrained(), measured);
    image.clear_dirty();
    // act
    image.set_source(factory.create_image(1, 1, ImageFormat::RGBA8, small));
    // assert a new extent invalidates the layout, because the offered size changed (§6.4, §8.2)
    EXPECT_TRUE(image.layout_dirty());
    image.measure(SizeConstraints::unconstrained(), measured);
    EXPECT_FLOAT_EQ(measured.width, 1.0f);
    EXPECT_FLOAT_EQ(measured.height, 1.0f);
}

TEST(ImageChecks, TheSameSourceRequestsNothing) {
    // arrange an image already showing one retained handle (§6.4)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[4]{};
    const std::shared_ptr<const ImageResource> source = factory.create_image(1, 1, ImageFormat::RGBA8, pixels);
    Image image(&engine);
    image.set_source(source);
    image.clear_dirty();
    // act
    image.set_source(source);
    // assert the identical handle changes nothing, because the picture is the same (§6.4)
    EXPECT_FALSE(image.layout_dirty());
    EXPECT_FALSE(image.render_dirty());
}

TEST(ImageChecks, ASameExtentSourceRedrawsWithoutRemeasuring) {
    // arrange an image whose next source has the same extent but different pixels (§6.4)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t first[4]{};
    std::uint8_t second[4]{255, 0, 0, 255};
    Image image(&engine);
    image.set_source(factory.create_image(1, 1, ImageFormat::RGBA8, first));
    image.clear_dirty();
    // act
    image.set_source(factory.create_image(1, 1, ImageFormat::RGBA8, second));
    // assert the frame redraws without a measure pass, because the offered extent did not change (§6.4, §8.2)
    EXPECT_TRUE(image.render_dirty());
    EXPECT_FALSE(image.layout_dirty());
}

TEST(ImageChecks, TheSourceIsRecordedIntoTheArrangedBounds) {
    // arrange an image with a source, arranged at a rectangle the parent chose (§6.4, §8.2)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[2 * 2 * 4]{255, 0, 0, 255};
    const std::shared_ptr<const ImageResource> source = factory.create_image(2, 2, ImageFormat::RGBA8, pixels);
    Image image(&engine);
    image.set_source(source);
    const Rect bounds = Rect::from_origin_size(Point{10.0f, 20.0f}, Size{30.0f, 40.0f});
    image.arrange(bounds);
    SceneRecorder recorder(WindowMetrics::from_logical(Size{100.0f, 100.0f}, 1.0f));
    // act
    image.render(recorder);
    // assert one image command carries the source into the arranged rectangle (§6.4)
    std::vector<DrawCommand> commands;
    for (const DrawCommand& command : recorder.scene()->commands())
        if (command.kind() == DrawCommandKind::Image)
            commands.push_back(command);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands.front().image_geometry().image, source);
    EXPECT_EQ(commands.front().image_geometry().destination, bounds);
    // assert the paint is white, which leaves every sampled texel unmodified (§6.4)
    EXPECT_EQ(commands.front().paint().fill, Color::rgb(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(rect_commands(*recorder.scene()).empty());
}

TEST(ImageChecks, AnEmptySourceRecordsNothing) {
    // arrange an image with no source, arranged where it would draw (§6.4)
    BindingEngine engine;
    Image image(&engine);
    image.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{20.0f, 20.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{40.0f, 40.0f}, 1.0f));
    // act
    image.render(recorder);
    // assert there is no command, because there are no pixels to map (§6.4)
    for (const DrawCommand& command : recorder.scene()->commands())
        EXPECT_NE(command.kind(), DrawCommandKind::Image);
}

TEST(ImageChecks, ASourceWithNoExtentIsAnnouncedDisabled) {
    // arrange an image whose source exists but carries no pixels, which is what an empty picture is (§6.4, §10.1)
    BindingEngine engine;
    Image image(&engine);
    image.set_source(std::make_shared<ImageResource>(0, 0, 0, ImageFormat::RGBA8, std::span<const std::uint8_t>{}));
    // act
    const SemanticNode node = image.semantic_node();
    // assert a source with no pixels is disabled, because there is nothing to describe (§10.1)
    EXPECT_TRUE(node.disabled());
}

TEST(ImageChecks, AnImageAnnouncesItsDescriptionAndBounds) {
    // arrange an arranged image with an author id and a written description (§10.1, AD-11)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[2 * 2 * 4]{};
    Image image(&engine, "logo");
    image.set_source(factory.create_image(2, 2, ImageFormat::RGBA8, pixels));
    image.set_description("Company logo");
    image.arrange(Rect::from_origin_size(Point{4.0f, 6.0f}, Size{24.0f, 12.0f}));
    // act
    const SemanticNode node = image.semantic_node();
    // assert the role, identity, name and bounds reach the semantic tree (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::Image);
    EXPECT_EQ(node.explicit_id(), "logo");
    EXPECT_EQ(node.name(), "Company logo");
    EXPECT_EQ(node.bounds(), image.bounds());
    EXPECT_FALSE(node.disabled());
    EXPECT_EQ(node.check(), CheckState::NotCheckable);
}

TEST(ImageChecks, AnEmptyDescriptionFallsBackToTheAuthorIdentity) {
    // arrange an image whose description is empty, which leaves the author id as its name (§10.1, AD-11)
    BindingEngine engine;
    ResourceFactory factory;
    std::uint8_t pixels[4]{};
    Image image(&engine, "logo");
    image.set_source(factory.create_image(1, 1, ImageFormat::RGBA8, pixels));
    // act
    const SemanticNode node = image.semantic_node();
    // assert the identity names the image when the author wrote no description (§10.1)
    EXPECT_EQ(node.name(), "logo");
}

TEST(ImageChecks, AnImageWithNoPixelsIsDisabledAndIsNotAFocusStop) {
    // arrange an image with no source, which has nothing to describe or focus (§6.4, §8.3)
    BindingEngine engine;
    Image image(&engine);
    // act
    const SemanticNode node = image.semantic_node();
    // assert the empty image is disabled and the widget declines the caret (§8.3, §10.1)
    EXPECT_TRUE(node.disabled());
    EXPECT_FALSE(image.accepts_focus());
}

TEST(PanelChecks, AnAuthoredFaceWinsEvenWhenItMatchesTheFallback) {
    // arrange a themed panel whose author writes the fallback colour explicitly (§6.1)
    BindingEngine engine;
    Panel panel(&engine);
    Theme theme;
    theme.set_panel_background(Color::rgb(0.9f, 0.1f, 0.1f));
    panel.set_theme(&theme);
    const Color authored = Color::rgb(0.13f, 0.15f, 0.19f);
    // act
    panel.set_background(authored);
    // assert the explicit face is not mistaken for an unchanged default (§6.1)
    EXPECT_EQ(panel.background(), authored);
}

TEST(PanelChecks, RendersItsFaceAndBorderThroughTheTheme) {
    // arrange a themed panel with a distinct face and hairline (§6.1, §8.4)
    BindingEngine engine;
    Panel panel(&engine);
    Theme theme;
    theme.set_panel_background(Color::rgb(0.2f, 0.3f, 0.4f));
    theme.set_panel_border(Color::rgb(0.5f, 0.6f, 0.7f));
    panel.set_theme(&theme);
    panel.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 40.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 200.0f}, 1.0f));
    // act
    panel.render(recorder);
    // assert the panel records one themed face with its hairline (§6.1)
    EXPECT_EQ(panel.background(), Color::rgb(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(panel.border_color(), Color::rgb(0.5f, 0.6f, 0.7f));
    EXPECT_FLOAT_EQ(panel.corner_radius(), 6.0f);
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    ASSERT_EQ(rect_commands(*scene).size(), 1U);
    const DrawCommand& plate = scene->commands().front();
    EXPECT_EQ(plate.kind(), DrawCommandKind::RoundedRect);
    EXPECT_EQ(plate.paint().fill, Color::rgb(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(plate.paint().stroke, Color::rgb(0.5f, 0.6f, 0.7f));
}

TEST(PanelChecks, MeasuresChildrenInsideThePaddingAndSpacing) {
    // arrange two fixed children, which make the panel extent arithmetic (§8.2)
    BindingEngine engine;
    Panel panel(&engine, "settings");
    auto first = std::make_unique<WidgetChildNode>();
    first->m_intrinsic = Size{40.0f, 20.0f};
    auto second = std::make_unique<WidgetChildNode>();
    second->m_intrinsic = Size{60.0f, 30.0f};
    panel.add_child(std::move(first));
    panel.add_child(std::move(second));
    panel.set_padding(10.0f);
    panel.set_spacing(4.0f);
    Size measured{};
    // act
    panel.measure(SizeConstraints::unconstrained(), measured);
    // assert the inset frames both axes and the gap joins the two children (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 80.0f);
    EXPECT_FLOAT_EQ(measured.height, 74.0f);
}

TEST(PanelChecks, ArrangesChildrenInsideThePadding) {
    // arrange a panel with one child and a ten point inset (§8.2)
    BindingEngine engine;
    Panel panel(&engine);
    auto child = std::make_unique<WidgetChildNode>();
    child->m_intrinsic = Size{40.0f, 20.0f};
    WidgetChildNode* leaf = child.get();
    panel.add_child(std::move(child));
    panel.set_padding(10.0f);
    Size measured{};
    panel.measure(SizeConstraints::unconstrained(), measured);
    // act
    panel.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 60.0f}));
    // assert the child sits inside the inset and takes the content width (§8.2)
    EXPECT_EQ(leaf->bounds(), Rect::from_origin_size(Point{10.0f, 10.0f}, Size{60.0f, 20.0f}));
}

TEST(PanelChecks, AnnouncesAGroupWithItsAuthorIdentity) {
    // arrange a panel with an author id, which is what automation selects by (AD-11)
    BindingEngine engine;
    Panel panel(&engine, "settings");
    panel.arrange(Rect::from_origin_size(Point{4.0f, 6.0f}, Size{80.0f, 60.0f}));
    // act
    const SemanticNode node = panel.semantic_node();
    // assert the role, identity and bounds reach the semantic tree (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::Group);
    EXPECT_EQ(node.explicit_id(), "settings");
    EXPECT_EQ(node.bounds(), panel.bounds());
    EXPECT_FALSE(panel.accepts_focus());
}

TEST(PanelChecks, AThemeResolvesThePanelTokens) {
    // arrange a themed panel with distinct face, border and inset tokens (§6.1, §8.2)
    BindingEngine engine;
    Panel panel(&engine);
    Theme theme;
    theme.set_panel_background(Color::rgb(0.1f, 0.2f, 0.3f));
    theme.set_panel_border(Color::rgb(0.4f, 0.5f, 0.6f));
    theme.set_panel_padding(5.0f);
    theme.set_panel_radius(7.0f);
    theme.set_panel_border_width(2.0f);
    panel.set_theme(&theme);
    // act
    panel.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 60.0f}));
    // assert the panel reads the theme rather than the fallback literals (§6.1, §8.4)
    EXPECT_EQ(panel.background(), Color::rgb(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(panel.border_color(), Color::rgb(0.4f, 0.5f, 0.6f));
    EXPECT_FLOAT_EQ(panel.padding(), 5.0f);
    EXPECT_FLOAT_EQ(panel.corner_radius(), 7.0f);
    EXPECT_FLOAT_EQ(panel.border_width(), 2.0f);
}

TEST(ComboBoxChecks, SelectsAnItemAndAnnouncesTheChoice) {
    // arrange a combo box with three options and a selected second option (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine, "choice");
    combo.set_items({"Alpha", "Beta", "Gamma"});
    combo.set_selected(1);
    combo.set_label("Mode");
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    // act
    const SemanticNode node = combo.semantic_node();
    // assert the role, identity, stable label, selected state and item children reach the tree (§10.1)
    EXPECT_EQ(node.role(), SemanticRole::ComboBox);
    EXPECT_EQ(node.explicit_id(), "choice");
    EXPECT_EQ(node.name(), "Mode");
    EXPECT_TRUE(node.selected());
    EXPECT_FALSE(node.expanded());
    EXPECT_EQ(combo.semantic_child_count(), 3u);
    const std::unique_ptr<SemanticProvider> child = combo.semantic_child(1);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->semantic_node().role(), SemanticRole::ListItem);
    EXPECT_EQ(child->semantic_node().name(), "Beta");
    EXPECT_TRUE(child->semantic_node().selected());
}

TEST(ComboBoxChecks, OpensOnTheKeyboardAndCommitsTheHighlightedOption) {
    // arrange a closed combo box with a selection signal (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta", "Gamma"});
    combo.set_selected(0);
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    int chosen = -1;
    combo.set_on_selection_changed([&chosen](int index) { chosen = index; });
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    // act the down arrow opens the list and moves the highlight
    combo.dispatch_event(down);
    // assert the popup is expanded but the arrow only moved the highlight, not the committed value (§8.3)
    EXPECT_TRUE(combo.expanded());
    EXPECT_EQ(combo.selected(), 0);
    EXPECT_TRUE(down.handled);
    Event commit{.kind = EventKind::KeyDown, .key = Key::Return};
    combo.dispatch_event(commit);
    // assert return commits the highlighted option and closes the popup (§8.3)
    EXPECT_FALSE(combo.expanded());
    EXPECT_EQ(chosen, 1);
    EXPECT_TRUE(commit.handled);
}

TEST(ComboBoxChecks, TheTriggerPaintsTheSelectedValueAndMarker) {
    // arrange a selected combo box at a known rectangle (§8.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta"});
    combo.set_selected(1);
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{300.0f, 100.0f}, 1.0f));
    // act
    combo.render(recorder);
    // assert the selected run and the disclosure marker both record, while the popup stays closed (§6.1)
    const std::vector<DrawCommand> texts = text_commands(*recorder.scene());
    ASSERT_EQ(texts.size(), 2U);
    EXPECT_EQ(texts.front().text_geometry().layout.run->glyph_count(), 4U);
    EXPECT_FALSE(combo.expanded());
}

TEST(ComboBoxChecks, ANativeHostOwnsThePopupUntilTheControlDies) {
    // arrange a combo box in a tree and a host that accepts the native surface (§5.1)
    RecordingPopupHost host;
    PopupHost* previous = window_popup_host();
    set_window_popup_host(&host);
    {
        BindingEngine engine;
        StubTextEngine text_engine;
        UiNode root;
        auto column = std::make_unique<ColumnLayout>();
        auto combo = std::make_unique<ComboBox>(&engine, &text_engine);
        ComboBox* choice = combo.get();
        choice->set_items({"Alpha", "Beta", "Gamma"});
        choice->set_selected(0);
        column->add_child(std::move(combo));
        root.add_child(std::move(column));
        Size measured{};
        root.measure(SizeConstraints::unconstrained(), measured);
        root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 200.0f}));
        // act open and close the list, then reopen the retained surface (§5.1)
        choice->open();
        EXPECT_EQ(host.opens, 1u);
        EXPECT_EQ(host.anchor, choice);
        EXPECT_EQ(host.anchor_rect, choice->bounds());
        EXPECT_GT(host.size.width, 0.0f);
        EXPECT_GT(host.size.height, 0.0f);
        EXPECT_TRUE(choice->expanded());
        choice->close();
        EXPECT_EQ(host.closes, 1u);
        EXPECT_FALSE(choice->expanded());
        choice->open();
        EXPECT_EQ(host.opens, 1u);
        EXPECT_EQ(host.shows, 1u);
        // act dismiss the native surface, which closes the control without dropping the tree (§5.1)
        ASSERT_TRUE(host.on_closed);
        host.on_closed();
        EXPECT_FALSE(choice->expanded());
        EXPECT_EQ(host.closes, 1u);
        // act reopen after the dismissal, which shows the same surface again (§5.1)
        choice->open();
        EXPECT_EQ(host.shows, 2u);
        EXPECT_TRUE(choice->expanded());
    }
    // assert the control destroyed the surface it opened when its tree unwound (§5.1)
    EXPECT_EQ(host.destroys, 1u);
    set_window_popup_host(previous);
}

TEST(ComboBoxChecks, TheNativeSurfaceBuildsACommittingList) {
    // arrange a combo box whose host records the real native surface builder (§5.1)
    RecordingPopupHost host;
    PopupHost* previous = window_popup_host();
    set_window_popup_host(&host);
    {
        BindingEngine engine;
        StubTextEngine text_engine;
        UiNode root;
        auto column = std::make_unique<ColumnLayout>();
        auto combo = std::make_unique<ComboBox>(&engine, &text_engine, "mode");
        ComboBox* choice = combo.get();
        choice->set_items({"Alpha", "Beta"});
        choice->set_selected(0);
        column->add_child(std::move(combo));
        root.add_child(std::move(column));
        Size measured{};
        root.measure(SizeConstraints::unconstrained(), measured);
        root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 200.0f}));
        choice->open();
        ASSERT_TRUE(host.builder);
        // act build and lay out the surface exactly as the native window would (§5.1)
        std::unique_ptr<UiNode> surface = host.builder(engine, text_engine);
        ASSERT_NE(surface, nullptr);
        ASSERT_EQ(surface->child_count(), 1u);
        auto* list = dynamic_cast<ListBox*>(surface->child_at(0));
        ASSERT_NE(list, nullptr);
        Size surface_size{};
        surface->measure(SizeConstraints::tight(host.size), surface_size);
        surface->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, surface_size));
        // act press the first row, which is already the selected one (§8.3)
        const Rect list_bounds = list->bounds();
        Event press{.kind = EventKind::PointerDown, .position = Point{list_bounds.x + 4.0f, list_bounds.y + list->row_height() * 0.5f}};
        list->dispatch_event(press);
        // assert the real list committed the row and closed the chooser (§8.3)
        EXPECT_TRUE(press.handled);
        EXPECT_EQ(choice->selected(), 0);
        EXPECT_FALSE(choice->expanded());
    }
    // assert the control released the surface when the tree unwound (§5.1)
    EXPECT_EQ(host.destroys, 1u);
    set_window_popup_host(previous);
}

TEST(ComboBoxChecks, AThemeChangeRebuildsTheRetainedNativeSurface) {
    // arrange a combo box whose native popup was built under the default theme (§5.1)
    RecordingPopupHost host;
    PopupHost* previous = window_popup_host();
    set_window_popup_host(&host);
    {
        BindingEngine engine;
        StubTextEngine text_engine;
        UiNode root;
        auto column = std::make_unique<ColumnLayout>();
        auto combo = std::make_unique<ComboBox>(&engine, &text_engine);
        ComboBox* choice = combo.get();
        choice->set_items({"Alpha", "Beta"});
        column->add_child(std::move(combo));
        root.add_child(std::move(column));
        Size measured{};
        root.measure(SizeConstraints::unconstrained(), measured);
        root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 200.0f}));
        choice->open();
        choice->close();
        ASSERT_EQ(host.opens, 1u);
        ASSERT_EQ(host.shows, 0u);
        // act change the theme and reopen, which cannot reuse the old rows (§6.1, §9.1)
        Theme changed;
        choice->set_theme(&changed);
        choice->open();
        // assert the stale surface was destroyed and a new one was built instead of shown (§5.1)
        EXPECT_EQ(host.destroys, 1u);
        EXPECT_EQ(host.opens, 2u);
        EXPECT_EQ(host.shows, 0u);
    }
    // assert the replacement surface is released with the control (§5.1)
    EXPECT_EQ(host.destroys, 2u);
    set_window_popup_host(previous);
}

TEST(ComboBoxChecks, AHostThatRefusesTheSurfaceFallsBackToTheOverlay) {
    // arrange a host that declines the surface, which leaves the tree layer as the only popup (§5.1)
    RecordingPopupHost host;
    host.handle = WindowHandle{};
    PopupHost* previous = window_popup_host();
    set_window_popup_host(&host);
    {
        BindingEngine engine;
        StubTextEngine text_engine;
        UiNode root;
        auto column = std::make_unique<ColumnLayout>();
        auto combo = std::make_unique<ComboBox>(&engine, &text_engine);
        ComboBox* choice = combo.get();
        choice->set_items({"Alpha", "Beta"});
        column->add_child(std::move(combo));
        root.add_child(std::move(column));
        Size measured{};
        root.measure(SizeConstraints::unconstrained(), measured);
        root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 200.0f}));
        // act
        choice->open();
        // assert the host was asked and the tree layer opened instead, because a refusal is a fallback (§5.1)
        EXPECT_EQ(host.opens, 1u);
        EXPECT_TRUE(choice->expanded());
        EXPECT_EQ(root.overlays().size(), 1u);
        choice->close();
        EXPECT_FALSE(choice->expanded());
        EXPECT_EQ(host.closes, 0u);
    }
    // assert a refused surface is never destroyed, because the host never owned one (§5.1)
    EXPECT_EQ(host.destroys, 0u);
    set_window_popup_host(previous);
}

TEST(ComboBoxChecks, ATriggerPressTogglesThePopup) {
    // arrange a closed combo box arranged at a known rectangle (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta"});
    combo.arrange(Rect::from_origin_size(Point{10.0f, 20.0f}, Size{160.0f, 28.0f}));
    Event press{.kind = EventKind::PointerDown, .position = Point{20.0f, 30.0f}};
    // act
    combo.dispatch_event(press);
    // assert the trigger opens the popup and claims the press (§8.3)
    EXPECT_TRUE(combo.expanded());
    EXPECT_TRUE(press.handled);
    Event second{.kind = EventKind::PointerDown, .position = Point{20.0f, 30.0f}};
    combo.dispatch_event(second);
    // assert the second press closes the popup again (§8.3)
    EXPECT_FALSE(combo.expanded());
    EXPECT_TRUE(second.handled);
}

TEST(ComboBoxChecks, APressOnAnOptionChoosesIt) {
    // arrange an open combo box, arranged where the first popup row is known (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta"});
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    combo.open();
    int chosen = -1;
    combo.set_on_selection_changed([&chosen](int index) { chosen = index; });
    Event press{.kind = EventKind::PointerDown, .position = Point{5.0f, 33.0f}};
    // act
    combo.dispatch_event(press);
    // assert the row press commits the first option and closes the popup (§8.3)
    EXPECT_EQ(combo.selected(), 0);
    EXPECT_EQ(chosen, 0);
    EXPECT_FALSE(combo.expanded());
    EXPECT_TRUE(press.handled);
}

TEST(ComboBoxChecks, AnOutsidePressClosesWithoutChoosing) {
    // arrange an open combo box with a selected option (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta"});
    combo.set_selected(0);
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    combo.open();
    Event press{.kind = EventKind::PointerDown, .position = Point{-10.0f, -10.0f}};
    // act
    combo.dispatch_event(press);
    // assert the outside press closes the popup and the chosen value stands (§8.3)
    EXPECT_FALSE(combo.expanded());
    EXPECT_EQ(combo.selected(), 0);
    EXPECT_TRUE(press.handled);
}

TEST(ComboBoxChecks, EscapeClosesWithoutChoosing) {
    // arrange an open combo box with a selected option and a signal counter (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta"});
    combo.set_selected(0);
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    combo.open();
    int chosen = -1;
    combo.set_on_selection_changed([&chosen](int index) { chosen = index; });
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act
    combo.dispatch_event(escape);
    // assert escape closes the list without committing a new value (§8.3)
    EXPECT_FALSE(combo.expanded());
    EXPECT_EQ(chosen, -1);
    EXPECT_TRUE(escape.handled);
}

TEST(ComboBoxChecks, EscapeThroughTheInputControllerClosesThePopup) {
    // arrange a focused combo box inside a tree, which is the real key routing path (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    UiNode root;
    auto choice = std::make_unique<ComboBox>(&engine, &text_engine);
    choice->set_items({"Alpha", "Beta"});
    choice->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    ComboBox* control = choice.get();
    root.add_child(std::move(choice));
    InputController controller;
    controller.attach(root);
    controller.focus().request_focus(control);
    control->open();
    ASSERT_TRUE(control->expanded());
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    // act the controller routes escape, which must reach the control before it is swallowed (§8.3)
    controller.deliver(escape);
    // assert the focused control closed its popup and the key was consumed (§8.3)
    EXPECT_FALSE(control->expanded());
    EXPECT_TRUE(escape.handled);
}

TEST(ComboBoxChecks, ArrowsChangeOnlyTheHighlightUntilReturnCommits) {
    // arrange a combo box with a committed selection and a signal counter (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta", "Gamma"});
    combo.set_selected(0);
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    int chosen = -1;
    combo.set_on_selection_changed([&chosen](int index) { chosen = index; });
    // act two arrows and escape, which is navigation rather than a commit (§8.3)
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    combo.dispatch_event(down);
    combo.dispatch_event(down);
    Event escape{.kind = EventKind::KeyDown, .key = Key::Escape};
    combo.dispatch_event(escape);
    // assert the committed value and the handler stayed untouched (§8.3)
    EXPECT_EQ(combo.selected(), 0);
    EXPECT_EQ(chosen, -1);
    EXPECT_FALSE(combo.expanded());
}

TEST(ComboBoxChecks, TheHighlightedRowIsWhatReturnCommits) {
    // arrange an open combo box with a selected first row (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha", "Beta", "Gamma"});
    combo.set_selected(0);
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    combo.open();
    int chosen = -1;
    combo.set_on_selection_changed([&chosen](int index) { chosen = index; });
    // act the pointer highlights the third row, then return commits the highlight (§8.3)
    Event hover{.kind = EventKind::PointerMove, .position = Point{5.0f, 28.0f + 4.0f + 56.0f + 1.0f}};
    combo.dispatch_event(hover);
    Event commit{.kind = EventKind::KeyDown, .key = Key::Return};
    combo.dispatch_event(commit);
    // assert the hovered row is the one committed, not the old selection (§8.3)
    EXPECT_EQ(chosen, 2);
    EXPECT_EQ(combo.selected(), 2);
    EXPECT_FALSE(combo.expanded());
}

TEST(ComboBoxChecks, ASelectionWrittenBeforeTheOptionsSurvivesTheList) {
    // arrange a declaration order that writes selected before items (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    // act
    combo.set_selected(1);
    combo.set_items({"Alpha", "Beta"});
    // assert the pending index applies once the list arrives (§8.3)
    EXPECT_EQ(combo.selected(), 1);
}

TEST(ComboBoxChecks, ANullTextEngineStillPaintsThePopup) {
    // arrange a control whose application supplied no text engine (§9.1)
    BindingEngine engine;
    ComboBox combo(&engine, nullptr);
    combo.set_items({"Alpha", "Beta"});
    combo.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    combo.open();
    SceneRecorder recorder(WindowMetrics::from_logical(Size{300.0f, 300.0f}, 1.0f));
    // act
    combo.render(recorder);
    // assert the popup face still records without a shaped option run (§9.1)
    EXPECT_GT(rect_commands(*recorder.scene()).size(), 0u);
    EXPECT_EQ(combo.selected(), -1);
}

TEST(ComboBoxChecks, APopupPressWinsOverALaterSiblingThroughTheRouter) {
    // arrange a column where a button follows the combo box and would cover its popup (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    UiNode root;
    auto column = std::make_unique<ColumnLayout>();
    auto combo = std::make_unique<ComboBox>(&engine, &text_engine);
    auto button = std::make_unique<Button>(&engine, &text_engine, "Below");
    ComboBox* choice = combo.get();
    choice->set_items({"Alpha", "Beta"});
    Button* below = button.get();
    std::size_t clicks = 0;
    below->set_on_click([&clicks] { clicks++; });
    column->add_child(std::move(combo));
    column->add_child(std::move(button));
    root.add_child(std::move(column));
    Size measured{};
    root.measure(SizeConstraints::unconstrained(), measured);
    root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 200.0f}));
    choice->arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{160.0f, 28.0f}));
    below->arrange(Rect::from_origin_size(Point{0.0f, 28.0f}, Size{160.0f, 28.0f}));
    choice->open();
    ASSERT_TRUE(choice->expanded());
    ASSERT_NE(choice->parent(), nullptr);
    ASSERT_EQ(root.overlays().size(), 1U);
    // act a press on the first popup row goes through the real dispatcher (§8.3)
    Event press{.kind = EventKind::PointerDown, .position = Point{5.0f, 33.0f}};
    InputDispatcher::dispatch(root, press);
    // assert the popup layer received it and the button did not (§8.3)
    EXPECT_EQ(choice->selected(), 0);
    EXPECT_EQ(clicks, 0u);
    EXPECT_TRUE(press.handled);
    // act an outside press through the same router also reaches the popup layer (§8.3)
    choice->open();
    Event outside{.kind = EventKind::PointerDown, .position = Point{180.0f, 180.0f}};
    InputDispatcher::dispatch(root, outside);
    // assert the outside press closes the popup and leaves the committed choice alone (§8.3)
    EXPECT_FALSE(choice->expanded());
    EXPECT_TRUE(outside.handled);
}

TEST(ComboBoxChecks, ADisabledControlCannotOpen) {
    // arrange a disabled combo box with options (§8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    ComboBox combo(&engine, &text_engine);
    combo.set_items({"Alpha"});
    combo.set_disabled(true);
    combo.open();
    // assert the disabled control refuses the popup and the semantic action (§8.3, §10.1)
    EXPECT_FALSE(combo.expanded());
    EXPECT_TRUE(combo.semantic_node().disabled());
}

TEST(ButtonIconChecks, ANamedIconMeasuresBesideTheLabel) {
    // arrange a button with a ten by ten icon and a two character label (§8.2, §6.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    ResourceFactory factory;
    std::uint8_t pixels[10 * 10 * 4]{};
    Button button(&engine, &text_engine, "Go");
    button.set_icon(factory.create_image(10, 10, ImageFormat::RGBA8, pixels));
    button.set_icon_size(20.0f);
    Size measured{};
    // act
    button.measure(SizeConstraints::unconstrained(), measured);
    // assert the icon, gap and label determine the content width (§8.2)
    EXPECT_FLOAT_EQ(measured.width, 46.0f);
    EXPECT_FLOAT_EQ(measured.height, 20.0f);
}

TEST(ButtonIconChecks, AnIconOnlyButtonUsesItsAuthorIdentityAsTheName) {
    // arrange an icon-only button with an author id, which is its accessible name (§10.1, AD-11)
    BindingEngine engine;
    StubTextEngine text_engine;
    ResourceFactory factory;
    std::uint8_t pixels[4 * 4 * 4]{};
    Button button(&engine, &text_engine, "", "toolbar.open");
    button.set_icon(factory.create_image(4, 4, ImageFormat::RGBA8, pixels));
    // act
    const SemanticNode node = button.semantic_node();
    // assert the label is empty and the author identity names the button (§10.1)
    EXPECT_EQ(node.name(), "toolbar.open");
    EXPECT_TRUE(button.label().empty());
}

TEST(ButtonIconChecks, AnIconOnlyButtonPaintsNoTextAndClearsBackToTheLabelPath) {
    // arrange an icon-only button with a ten point icon (§6.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    ResourceFactory factory;
    std::uint8_t pixels[10 * 10 * 4]{};
    Button button(&engine, &text_engine);
    button.set_icon(factory.create_image(10, 10, ImageFormat::RGBA8, pixels));
    button.set_icon_size(20.0f);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{80.0f, 24.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the icon-only path records the image and no run (§6.4)
    std::size_t images = 0;
    for (const DrawCommand& command : recorder.scene()->commands())
        if (command.kind() == DrawCommandKind::Image)
            images++;
    EXPECT_EQ(images, 1U);
    EXPECT_TRUE(text_commands(*recorder.scene()).empty());
    // act and assert clearing the icon restores the text-only path (§8.2)
    button.set_label("Go");
    button.set_icon(nullptr);
    SceneRecorder labelled(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    button.render(labelled);
    EXPECT_EQ(text_commands(*labelled.scene()).size(), 1U);
}

TEST(ButtonIconChecks, TheIconAndLabelAreBothRecorded) {
    // arrange a button with an icon and a label, arranged at a known rectangle (§6.1, §6.4)
    BindingEngine engine;
    StubTextEngine text_engine;
    ResourceFactory factory;
    std::uint8_t pixels[8 * 8 * 4]{};
    Button button(&engine, &text_engine, "Go");
    button.set_icon(factory.create_image(8, 8, ImageFormat::RGBA8, pixels));
    button.set_icon_size(20.0f);
    button.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 24.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    button.render(recorder);
    // assert the image and the label both reach the scene (§6.1)
    std::size_t images = 0;
    for (const DrawCommand& command : recorder.scene()->commands())
        if (command.kind() == DrawCommandKind::Image)
            images++;
    EXPECT_EQ(images, 1u);
    EXPECT_EQ(text_commands(*recorder.scene()).size(), 1u);
}

TEST(TextFieldDecorationChecks, AStyledRangeAddsForegroundAndUnderlineCommands) {
    // arrange a field with one styled range over its first byte (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TextField field(&engine, &text_engine, "abc");
    TextDecoration decoration;
    decoration.start = 0;
    decoration.end = 1;
    decoration.foreground = Color::rgb(0.9f, 0.1f, 0.1f);
    decoration.underline = true;
    field.set_decorations({decoration});
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 24.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the base run, the coloured subset and the underline all reach the scene (§9.1)
    EXPECT_GE(text_commands(*recorder.scene()).size(), 2u);
    EXPECT_EQ(rect_commands(*recorder.scene()).size(), 1u);
    EXPECT_EQ(field.decorations().size(), 1u);
}

TEST(TextFieldDecorationChecks, ASelectionStaysVisibleOverABackgroundRange) {
    // arrange a field whose selection overlaps a range with a background band (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TextField field(&engine, &text_engine, "abc");
    TextDecoration decoration;
    decoration.start = 0;
    decoration.end = 3;
    decoration.background = Color::rgb(0.9f, 0.1f, 0.1f);
    field.set_decorations({decoration});
    field.set_selection(0, 3);
    field.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{100.0f, 24.0f}));
    SceneRecorder recorder(WindowMetrics::from_logical(Size{200.0f, 100.0f}, 1.0f));
    // act
    field.render(recorder);
    // assert the band is recorded before the selection, so the highlight is not erased (§9.1)
    const std::shared_ptr<const FrameScene> scene = recorder.scene();
    const std::vector<DrawCommand>& commands = scene->commands();
    std::size_t band = commands.size();
    std::size_t selection = commands.size();
    for (std::size_t index = 0; index < commands.size(); index++) {
        const DrawCommand& command = commands[index];
        if (command.kind() != DrawCommandKind::Rect)
            continue;
        const Color fill = command.paint().fill;
        if (fill == decoration.background)
            band = index;
        else if (fill == Color::rgba(0.20f, 0.55f, 0.90f, 0.35f))
            selection = index;
    }
    ASSERT_LT(band, commands.size());
    ASSERT_LT(selection, commands.size());
    EXPECT_LT(band, selection);
}

TEST(TextFieldDecorationChecks, ClearingTheRangesRemovesTheDecorationPath) {
    // arrange a field already carrying a range (§9.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    TextField field(&engine, &text_engine, "abc");
    field.set_decorations({TextDecoration{.start = 0, .end = 1, .foreground = Color::rgb(1.0f, 0.0f, 0.0f)}});
    // act
    field.clear_decorations();
    // assert the field returns to the single colour path (§9.1)
    EXPECT_TRUE(field.decorations().empty());
}

TEST(MenuChecks, OneCommandFeedsAMenuItemAButtonAndAnAccelerator) {
    // arrange one command that a menu row, a button and a shortcut all bind to (§18.1, §18.5)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> command = registry.declare("file.save", "Save");
    const Shortcut shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}};
    ASSERT_TRUE(registry.bind_shortcut(shortcut, "file.save"));
    Menu menu(&engine, &text_engine);
    MenuItem& item = menu.add_item(*command, &registry.shortcuts(), shortcut);
    Button button(&engine, &text_engine);
    button.set_command(command.get());
    std::size_t invocations = 0;
    InvocationSource source = InvocationSource::Handler;
    command->set_action([&invocations, &source](const CommandInvocation& invocation) {
        // the route is recorded, so the semantic action can prove it used the same command (§18.3)
        invocations++;
        source = invocation.source;
    });
    // act assert one label and one accelerator reach both consumers from the same object (§18.2, §18.5)
    EXPECT_EQ(item.label(), "Save");
    EXPECT_EQ(button.label(), "Save");
    EXPECT_EQ(item.shortcut_text(), registry.shortcuts().display(shortcut));
    EXPECT_EQ(item.shortcut_text(), "Cmd+S");
    // act withholding permission on the one object disables both consumers (§18.7)
    command->set_enabled(false);
    // assert the menu row and the button report the same answer, because neither holds a copy (§18.7)
    EXPECT_TRUE(item.disabled());
    EXPECT_TRUE(button.disabled());
    // act the command is permitted again and assistive technology activates the row (§10.1, §18.3)
    command->set_enabled(true);
    const SemanticNode node = item.semantic_node();
    ASSERT_TRUE(node.invoke("activate"));
    // assert the semantic route ran the command the pointer would run (§10.1, §18.3)
    EXPECT_EQ(invocations, 1U);
    EXPECT_EQ(source, InvocationSource::Accessibility);
}

TEST(MenuChecks, AHiddenCommandRemovesItsRowFromTheMenuLayout) {
    // arrange a menu of three rows, where the middle one can be hidden by its command (§18.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> first = registry.declare("file.open", "Open");
    std::unique_ptr<Command> second = registry.declare("file.recent", "Recent");
    std::unique_ptr<Command> third = registry.declare("file.quit", "Quit");
    Menu menu(&engine, &text_engine);
    MenuItem& middle = menu.add_item(*second);
    menu.add_item(*first);
    menu.add_item(*third);
    Size before{};
    menu.measure(SizeConstraints::unconstrained(), before);
    // act
    second->set_visible(false);
    Size after{};
    menu.measure(SizeConstraints::unconstrained(), after);
    // assert the invisible row is gone from layout and the visible rows are still menued (§18.2)
    EXPECT_FALSE(middle.visible());
    EXPECT_LT(after.height, before.height);
    EXPECT_TRUE(menu.visible());
}

TEST(MenuChecks, ADisabledRowStaysInPlaceAndLeavesFocusTraversal) {
    // arrange a menu whose second row is disabled, which must stay visible and unreachable by arrows (§18.2)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> first = registry.declare("edit.copy", "Copy");
    std::unique_ptr<Command> second = registry.declare("edit.paste", "Paste");
    Menu menu(&engine, &text_engine);
    MenuItem& copy = menu.add_item(*first);
    MenuItem& paste = menu.add_item(*second);
    Size unused{};
    menu.measure(SizeConstraints::unconstrained(), unused);
    menu.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 80.0f}));
    second->set_enabled(false);
    copy.set_focused(true);
    // act the down arrow asks for the next focus stop (§8.3)
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    menu.dispatch_event(down);
    // assert the disabled row keeps its place but is skipped by traversal (§18.2)
    EXPECT_TRUE(paste.visible());
    EXPECT_GT(paste.bounds().height, 0.0f);
    EXPECT_FALSE(paste.focused());
    EXPECT_TRUE(copy.focused());
    EXPECT_TRUE(down.handled);
}

TEST(MenuChecks, AToolBarPlacesItsVisibleControlsInARow) {
    // arrange a strip with two controls, which is the container shape a shell hosts (§8)
    BindingEngine engine;
    StubTextEngine text_engine;
    ToolBar bar(&engine);
    bar.add_child(std::make_unique<Button>(&engine, &text_engine, "One"));
    bar.add_child(std::make_unique<Button>(&engine, &text_engine, "Two"));
    Size measured{};
    // act
    bar.measure(SizeConstraints::unconstrained(), measured);
    bar.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, measured));
    // assert both controls measure and sit side by side in one strip (§8.2)
    auto* first = dynamic_cast<Button*>(bar.child_at(0));
    auto* second = dynamic_cast<Button*>(bar.child_at(1));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_GT(measured.width, first->measured_size().width);
    EXPECT_GE(second->bounds().x, first->bounds().x + first->bounds().width);
}

TEST(MenuChecks, ADestroyedRowLeavesNoCommandObserver) {
    // arrange a command-bound row that is destroyed before the command (§18.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> command = registry.declare("view.temporary", "Temporary");
    auto row = std::make_unique<MenuItem>(&engine, &text_engine);
    row->set_command(command.get());
    EXPECT_EQ(command->observer_count(), 1U);
    // act
    row.reset();
    // assert the observer left with the row, so a publish cannot reach freed memory (§18.1)
    EXPECT_EQ(command->observer_count(), 0U);
    command->set_enabled(false);
}

TEST(MenuChecks, AHiddenRowLeavesTheSemanticTree) {
    // arrange a menu whose second command hides its row (§18.2, §10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> first = registry.declare("view.grid", "Grid");
    std::unique_ptr<Command> second = registry.declare("view.rulers", "Rulers");
    Menu menu(&engine, &text_engine);
    MenuItem& shown = menu.add_item(*first);
    MenuItem& hidden = menu.add_item(*second);
    shown.set_explicit_id("shown_row");
    hidden.set_explicit_id("hidden_row");
    second->set_visible(false);
    // act
    const SemanticTree tree = build_semantic_tree(menu);
    // assert the hidden row is absent, because a reader must not reach what the layout removed (§10.1)
    bool shown_found = false;
    bool hidden_found = false;
    for (std::size_t index = 0; index < tree.size(); index++) {
        shown_found = shown_found || tree.at(index)->explicit_id() == "shown_row";
        hidden_found = hidden_found || tree.at(index)->explicit_id() == "hidden_row";
    }
    EXPECT_TRUE(shown_found);
    EXPECT_FALSE(hidden_found);
}

TEST(MenuChecks, AFocusedRowThatBecomesDisabledLosesTheFocus) {
    // arrange a menu focused on its first row, which is then disabled (§18.2, §8.3)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> first = registry.declare("view.first", "First");
    std::unique_ptr<Command> second = registry.declare("view.second", "Second");
    Menu menu(&engine, &text_engine);
    MenuItem& disabled = menu.add_item(*first);
    MenuItem& next = menu.add_item(*second);
    Size unused{};
    menu.measure(SizeConstraints::unconstrained(), unused);
    menu.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, Size{200.0f, 80.0f}));
    disabled.set_focused(true);
    first->set_enabled(false);
    // act the next arrow asks for the next stop (§8.3)
    Event down{.kind = EventKind::KeyDown, .key = Key::ArrowDown};
    menu.dispatch_event(down);
    // assert the stale row dropped its ring and the next enabled row took it (§8.3)
    EXPECT_FALSE(disabled.focused());
    EXPECT_TRUE(next.focused());
    EXPECT_TRUE(down.handled);
}

TEST(MenuChecks, ASeparatorIsAbsentFromTheSemanticTree) {
    // arrange a menu with one row and a separator, which carries no meaning to a reader (§18.2, §10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    CommandRegistry registry;
    std::unique_ptr<Command> command = registry.declare("view.only", "Only");
    Menu menu(&engine, &text_engine);
    menu.add_item(*command);
    menu.add_separator();
    // act
    const SemanticTree tree = build_semantic_tree(menu);
    // assert the separator is skipped instead of becoming an unnamed unknown element (§10.1)
    EXPECT_EQ(tree.count_role(SemanticRole::None), 0U);
}

TEST(SemanticWalkChecks, AContainerWithoutAProviderWalksItsSubtree) {
    // arrange a panel whose only child is a plain layout holding a button, which is how a composed page is built (§10.1)
    BindingEngine engine;
    StubTextEngine text_engine;
    Panel panel(&engine);
    auto column = std::make_unique<ColumnLayout>();
    auto button = std::make_unique<Button>(&engine, &text_engine, "Deep");
    button->set_explicit_id("deep_button");
    column->add_child(std::move(button));
    panel.add_child(std::move(column));
    // act the semantic walk starts at the panel, which is a provider itself (§10.1, AD-09)
    const SemanticTree tree = build_semantic_tree(panel);
    // assert the button is reachable through the layout rather than hidden behind a role none leaf (§10.1)
    const SemanticNode* found = nullptr;
    for (std::size_t index = 0; index < tree.size(); index++)
        if (tree.at(index)->explicit_id() == "deep_button")
            found = tree.at(index);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->role(), SemanticRole::Button);
    EXPECT_EQ(found->name(), "Deep");
}
