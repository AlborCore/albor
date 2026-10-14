// tests for the theme: a theme is a value holder, so every entry point is a write that a reader confirms

#include <gtest/gtest.h>

#include <memory>

#include "style/theme.h"
#include "style/theme_builder.h"
#include "widgets/slider.h"
#include "widgets/widget.h"

namespace albor
{
    namespace
    {

        // the five control states a palette keeps one entry per, in the order the resolver indexes (§6.1)
        constexpr ControlState kStates[] = {ControlState::Normal, ControlState::Hovered, ControlState::Pressed, ControlState::Disabled, ControlState::Focused};

        // how many states the resolver can return, which is what the tables are sized by (§6.1)
        constexpr std::size_t kStateCount = sizeof(kStates) / sizeof(kStates[0]);

    } // namespace

    TEST(Theme, AStatefulEntryLandsInItsOwnSlot) {
        // arrange
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        const Color green{0.0f, 1.0f, 0.0f, 1.0f};
        const Color blue{0.0f, 0.0f, 1.0f, 1.0f};
        const Color white{1.0f, 1.0f, 1.0f, 1.0f};
        // act each state is written on its own
        theme.set_button_plate(ControlState::Hovered, red);
        theme.set_button_plate(ControlState::Pressed, green);
        theme.set_button_plate(ControlState::Disabled, blue);
        theme.set_button_plate(white);
        // assert the state selects the entry, so no two controls share a colour (§6.1)
        EXPECT_EQ(theme.palette().button_plate[1], red);
        EXPECT_EQ(theme.palette().button_plate[2], green);
        EXPECT_EQ(theme.palette().button_plate[3], blue);
        // assert the plain entry point names the normal state, because that is the common case (§6.1)
        EXPECT_EQ(theme.palette().button_plate[0], white);
    }

    TEST(Theme, TheCommonCaseEntryPointsNameTheNormalState) {
        // arrange
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        const Color green{0.0f, 1.0f, 0.0f, 1.0f};
        const Color blue{0.0f, 0.0f, 1.0f, 1.0f};
        // act every control's plain entry point is called
        theme.set_button_label(red);
        theme.set_checkbox_fill(green);
        theme.set_checkbox_outline(blue);
        theme.set_slider_track(red);
        theme.set_slider_thumb(green);
        theme.set_label_foreground(blue);
        // assert each one wrote the normal entry, because the resolver reads index zero for the common case (§6.1)
        EXPECT_EQ(theme.palette().button_label[0], red);
        EXPECT_EQ(theme.palette().checkbox_fill[0], green);
        EXPECT_EQ(theme.palette().checkbox_outline[0], blue);
        EXPECT_EQ(theme.palette().slider_track[0], red);
        EXPECT_EQ(theme.palette().slider_thumb[0], green);
        EXPECT_EQ(theme.palette().label_foreground[0], blue);
    }

    TEST(Theme, EveryControlKeepsOneEntryPerState) {
        // arrange
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        const Color green{0.0f, 1.0f, 0.0f, 1.0f};
        const Color blue{0.0f, 0.0f, 1.0f, 1.0f};
        const Color white{1.0f, 1.0f, 1.0f, 1.0f};
        // act every state is written for every stateful control
        for (const ControlState state : kStates) {
            theme.set_button_plate(state, red);
            theme.set_button_label(state, green);
            theme.set_checkbox_fill(state, blue);
            theme.set_checkbox_outline(state, white);
            theme.set_slider_track(state, red);
            theme.set_slider_thumb(state, green);
            theme.set_label_foreground(state, blue);
            theme.set_scrollbar_track(state, white);
            theme.set_scrollbar_thumb(state, red);
            theme.set_text_field_text(state, red);
            theme.set_text_field_placeholder(state, green);
            theme.set_text_field_caret(state, blue);
            theme.set_text_field_selection(state, white);
            theme.set_focus_ring(state, red);
            theme.set_canvas_background(state, green);
        }
        // assert every state of every control took the write, so a hover and a press differ (§6.1)
        for (std::size_t index = 0; index < kStateCount; ++index) {
            EXPECT_EQ(theme.palette().button_plate[index], red);
            EXPECT_EQ(theme.palette().button_label[index], green);
            EXPECT_EQ(theme.palette().checkbox_fill[index], blue);
            EXPECT_EQ(theme.palette().checkbox_outline[index], white);
            EXPECT_EQ(theme.palette().slider_track[index], red);
            EXPECT_EQ(theme.palette().slider_thumb[index], green);
            EXPECT_EQ(theme.palette().label_foreground[index], blue);
            EXPECT_EQ(theme.palette().scrollbar_track[index], white);
            EXPECT_EQ(theme.palette().scrollbar_thumb[index], red);
            EXPECT_EQ(theme.palette().text_field_text[index], red);
            EXPECT_EQ(theme.palette().text_field_placeholder[index], green);
            EXPECT_EQ(theme.palette().text_field_caret[index], blue);
            EXPECT_EQ(theme.palette().text_field_selection[index], white);
            EXPECT_EQ(theme.palette().focus_ring[index], red);
            EXPECT_EQ(theme.palette().canvas_background[index], green);
        }
    }

    TEST(Theme, TheSurfaceRoundTrips) {
        // arrange
        Theme theme;
        const Color ink{0.1f, 0.1f, 0.12f, 1.0f};
        // act
        theme.set_surface(ink);
        // assert the reader hands back the write, because the surface is what everything else sits on (§6.1)
        EXPECT_EQ(theme.surface(), ink);
    }

    TEST(Theme, TheMetricsRoundTrip) {
        // arrange
        Theme theme;
        // act every metric is written with the value its control is measured by (§8.2)
        theme.set_button_minimum(Size{40.0f, 22.0f});
        theme.set_checkbox_box(Size{20.0f, 20.0f});
        theme.set_checkbox_outline(1.5f);
        theme.set_slider_extent(Size{200.0f, 18.0f});
        theme.set_slider_track_thickness(6.0f);
        theme.set_slider_thumb(16.0f);
        theme.set_slider_step(0.25f);
        theme.set_corner_radius(4.0f);
        theme.set_scrollbar_extent(12.0f);
        theme.set_scrollbar_thumb_minimum(28.0f);
        theme.set_scroll_line_extent(30.0f);
        // assert every measurement follows the theme, which is what makes one theme reskin the app (§8.2)
        EXPECT_EQ(theme.metrics().button_minimum, (Size{40.0f, 22.0f}));
        EXPECT_EQ(theme.metrics().checkbox_box, (Size{20.0f, 20.0f}));
        EXPECT_FLOAT_EQ(theme.metrics().checkbox_outline, 1.5f);
        EXPECT_EQ(theme.metrics().slider_extent, (Size{200.0f, 18.0f}));
        EXPECT_FLOAT_EQ(theme.metrics().slider_track_thickness, 6.0f);
        EXPECT_FLOAT_EQ(theme.metrics().slider_thumb, 16.0f);
        EXPECT_FLOAT_EQ(theme.metrics().slider_step, 0.25f);
        EXPECT_FLOAT_EQ(theme.metrics().corner_radius, 4.0f);
        EXPECT_FLOAT_EQ(theme.metrics().scrollbar_extent, 12.0f);
        EXPECT_FLOAT_EQ(theme.metrics().scrollbar_thumb_minimum, 28.0f);
        EXPECT_FLOAT_EQ(theme.metrics().scroll_line_extent, 30.0f);
    }

    TEST(Theme, ADefaultThemeIsUsableWithoutBeingTouched) {
        // arrange
        Theme theme;
        // act, assert the shipped defaults already describe a usable control, so an app needs no theme at all (§6.1)
        EXPECT_EQ(theme.palette().button_plate[0], (Color{0.20f, 0.55f, 0.90f, 1.0f}));
        EXPECT_EQ(theme.metrics().button_minimum, (Size{24.0f, 16.0f}));
        EXPECT_FLOAT_EQ(theme.metrics().scrollbar_thumb_minimum, 24.0f);
        EXPECT_FLOAT_EQ(theme.metrics().scroll_line_extent, 40.0f);
    }

    TEST(Theme, AWidgetReadsItsMetricsFromTheThemeItWasGiven) {
        // arrange
        BindingEngine engine;
        Slider slider(&engine);
        Theme theme;
        theme.set_slider_extent(Size{300.0f, 24.0f});
        theme.set_button_minimum(Size{60.0f, 30.0f});
        // act
        apply_theme(slider, theme);
        // assert the control measures to the theme, which is the whole point of a theme (§8.2)
        Size measured{};
        slider.measure(SizeConstraints::unconstrained(), measured);
        EXPECT_FLOAT_EQ(measured.width, 300.0f);
        EXPECT_EQ(slider.theme(), &theme);
    }

    TEST(Theme, TheFocusRingRoundTripsEveryState) {
        // arrange a ring colour per state, because a focus ring reads by state like any other role (§6.1)
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        const Color green{0.0f, 1.0f, 0.0f, 1.0f};
        const Color blue{0.0f, 0.0f, 1.0f, 1.0f};
        const Color white{1.0f, 1.0f, 1.0f, 1.0f};
        // act every state is written on its own
        theme.set_focus_ring(ControlState::Normal, red);
        theme.set_focus_ring(ControlState::Hovered, green);
        theme.set_focus_ring(ControlState::Pressed, blue);
        theme.set_focus_ring(ControlState::Disabled, white);
        theme.set_focus_ring(ControlState::Focused, blue);
        // assert each state took its own colour, so a hover and a press differ (§6.1)
        EXPECT_EQ(theme.palette().focus_ring[state_index(ControlState::Normal)], red);
        EXPECT_EQ(theme.palette().focus_ring[state_index(ControlState::Hovered)], green);
        EXPECT_EQ(theme.palette().focus_ring[state_index(ControlState::Pressed)], blue);
        EXPECT_EQ(theme.palette().focus_ring[state_index(ControlState::Disabled)], white);
        EXPECT_EQ(theme.palette().focus_ring[state_index(ControlState::Focused)], blue);
    }

    TEST(Theme, TheFocusRingShorthandWritesTheFocusedEntry) {
        // arrange
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        // act
        theme.set_focus_ring(red);
        // assert the shorthand wrote the focused entry, because the ring draws on the focus alone (§6.1)
        EXPECT_EQ(theme.palette().focus_ring[state_index(ControlState::Focused)], red);
        // assert the normal entry is untouched, because the ring is never drawn there (§6.1)
        EXPECT_NE(theme.palette().focus_ring[state_index(ControlState::Normal)], red);
    }

    TEST(Theme, TheCanvasBackgroundRoundTripsEveryState) {
        // arrange a plate colour per state, because a state visual reads by state (§6.1)
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        const Color green{0.0f, 1.0f, 0.0f, 1.0f};
        const Color blue{0.0f, 0.0f, 1.0f, 1.0f};
        const Color white{1.0f, 1.0f, 1.0f, 1.0f};
        // act
        theme.set_canvas_background(ControlState::Normal, red);
        theme.set_canvas_background(ControlState::Hovered, green);
        theme.set_canvas_background(ControlState::Pressed, blue);
        theme.set_canvas_background(ControlState::Disabled, white);
        theme.set_canvas_background(ControlState::Focused, green);
        // assert each state took its own colour, so a hover and a press differ (§6.1)
        EXPECT_EQ(theme.palette().canvas_background[state_index(ControlState::Normal)], red);
        EXPECT_EQ(theme.palette().canvas_background[state_index(ControlState::Hovered)], green);
        EXPECT_EQ(theme.palette().canvas_background[state_index(ControlState::Pressed)], blue);
        EXPECT_EQ(theme.palette().canvas_background[state_index(ControlState::Disabled)], white);
        EXPECT_EQ(theme.palette().canvas_background[state_index(ControlState::Focused)], green);
    }

    TEST(Theme, TheCanvasBackgroundNormalEntryHasItsOwnShorthand) {
        // arrange
        Theme theme;
        const Color red{1.0f, 0.0f, 0.0f, 1.0f};
        // act
        theme.set_canvas_background(red);
        // assert
        EXPECT_EQ(theme.palette().canvas_background[state_index(ControlState::Normal)], red);
    }

    TEST(Theme, TheFocusRingMetricsRoundTrip) {
        // arrange
        Theme theme;
        // act every ring measurement is written with the value it is drawn from (§6.1, §6.2)
        theme.set_focus_ring_width(3.0f);
        theme.set_focus_ring_offset(4.0f);
        theme.set_focus_ring_radius(5.0f);
        // assert
        EXPECT_FLOAT_EQ(theme.metrics().focus_ring_width, 3.0f);
        EXPECT_FLOAT_EQ(theme.metrics().focus_ring_offset, 4.0f);
        EXPECT_FLOAT_EQ(theme.metrics().focus_ring_radius, 5.0f);
    }

    TEST(Theme, ADerivedThemeGivesEveryStateItsOwnEntry) {
        // arrange the two derived themes, because a builder is what an application actually installs (§6.1)
        const Theme dark = ThemeBuilder::dark(Accents{});
        const Theme light = ThemeBuilder::light(Accents{});
        // act, assert the focused plate and ring differ from the normal one in both schemes (§6.1)
        EXPECT_NE(dark.palette().button_plate[state_index(ControlState::Focused)], dark.palette().button_plate[state_index(ControlState::Normal)]);
        EXPECT_NE(light.palette().button_plate[state_index(ControlState::Focused)], light.palette().button_plate[state_index(ControlState::Normal)]);
        EXPECT_NE(dark.palette().focus_ring[state_index(ControlState::Focused)], dark.palette().focus_ring[state_index(ControlState::Normal)]);
        EXPECT_NE(light.palette().focus_ring[state_index(ControlState::Focused)], light.palette().focus_ring[state_index(ControlState::Normal)]);
        // assert the resting ring is invisible in both schemes, because nothing is focused at rest (§6.1)
        EXPECT_FLOAT_EQ(dark.palette().focus_ring[state_index(ControlState::Normal)].alpha, 0.0f);
        EXPECT_FLOAT_EQ(light.palette().focus_ring[state_index(ControlState::Normal)].alpha, 0.0f);
    }

} // namespace albor

namespace albor
{
    TEST(ThemePresets, TheMacosPresetSizesAControlToTheGuide) {
        // arrange the default accent set, which is what an application that customises nothing takes (§8.4)
        const Theme macos = Theme::macos(Accents{}, ColorScheme::Dark);
        // assert the control metrics are the ones the interface guide names (§8.4)
        EXPECT_EQ(macos.metrics().button_minimum, (Size{24.0f, 28.0f}));
        EXPECT_FLOAT_EQ(macos.metrics().button_padding_horizontal, 12.0f);
        EXPECT_FLOAT_EQ(macos.metrics().button_border_width, 1.0f);
        EXPECT_FLOAT_EQ(macos.metrics().corner_radius, 5.0f);
        EXPECT_EQ(macos.metrics().checkbox_box, (Size{14.0f, 14.0f}));
        EXPECT_FLOAT_EQ(macos.metrics().checkbox_outline, 1.0f);
        EXPECT_FLOAT_EQ(macos.metrics().slider_thumb, 22.0f);
        EXPECT_FLOAT_EQ(macos.metrics().slider_thumb_radius, 11.0f);
        EXPECT_FLOAT_EQ(macos.metrics().slider_thumb_shadow, 0.30f);
        // the hierarchy sizes body copy at thirteen (§8.4)
        EXPECT_FLOAT_EQ(macos.typography().body, 13.0f);
    }

    TEST(ThemePresets, TheMacosPresetCarriesAHairlineBorderAndASubtleFace) {
        // arrange
        const Theme macos = Theme::macos(Accents{}, ColorScheme::Dark);
        // assert the border resolves a real colour rather than none, which is the second layer (§8.4)
        EXPECT_GT(macos.palette().button_border[state_index(ControlState::Normal)].alpha, 0.0f);
        // assert the face is a neutral lift rather than the accent block (§8.4)
        EXPECT_NE(macos.palette().button_plate[state_index(ControlState::Normal)], Accents{}.accent);
        // assert the halo under the ring is the accent dimmed, which is the dual layer (§8.4)
        EXPECT_FLOAT_EQ(macos.palette().focus_ring_outer[state_index(ControlState::Focused)].alpha, 0.45f);
    }

    TEST(ThemePresets, TheFluentPresetSizesAControlToTheLanguage) {
        // arrange
        const Theme fluent = Theme::fluent(Accents{}, ColorScheme::Light);
        // assert the control metrics are the ones the windows language names (§8.4)
        EXPECT_EQ(fluent.metrics().button_minimum, (Size{24.0f, 32.0f}));
        EXPECT_FLOAT_EQ(fluent.metrics().corner_radius, 4.0f);
        EXPECT_FLOAT_EQ(fluent.metrics().checkbox_outline, 1.5f);
        EXPECT_FLOAT_EQ(fluent.metrics().slider_tick_length, 4.0f);
        EXPECT_FLOAT_EQ(fluent.typography().body, 14.0f);
    }

    TEST(ThemePresets, TheProDarkPresetKeepsTheToolComp) {
        // arrange
        const Theme pro_dark = Theme::pro_dark(Accents{});
        // assert the control metrics are the ones a tool surface sizes (§8.4)
        EXPECT_EQ(pro_dark.metrics().button_minimum, (Size{24.0f, 24.0f}));
        EXPECT_FLOAT_EQ(pro_dark.metrics().button_padding_horizontal, 8.0f);
        // assert the face lifts off the surface, which is the contrast a tool separates with (§8.4)
        EXPECT_GT(pro_dark.palette().button_plate[state_index(ControlState::Normal)].red, Accents{}.surface.red);
    }

    TEST(ThemeTypography, ARoleSizeRoundTrips) {
        // arrange
        Theme theme;
        // act
        theme.set_role_font_size(TextRole::Caption, 9.5f);
        theme.set_secondary_opacity(0.5f);
        // assert the hierarchy carries the writes, because a role resolves through it (§8.4)
        EXPECT_FLOAT_EQ(theme.typography().caption, 9.5f);
        EXPECT_FLOAT_EQ(theme.typography().secondary_opacity, 0.5f);
    }

    TEST(ThemeTypography, TheSecondLayersDefaultToInvisible) {
        // arrange
        Theme theme;
        // assert every new layer is silent by default, so no theme changes any widget until it decides (§8.4)
        EXPECT_FLOAT_EQ(theme.palette().slider_fill[state_index(ControlState::Normal)].alpha, 0.0f);
        EXPECT_FLOAT_EQ(theme.palette().button_border[state_index(ControlState::Normal)].alpha, 0.0f);
        EXPECT_FLOAT_EQ(theme.palette().focus_ring_outer[state_index(ControlState::Focused)].alpha, 0.0f);
        // assert the native metric tokens are silent by default too (§8.4)
        EXPECT_FLOAT_EQ(theme.metrics().button_padding_horizontal, 0.0f);
        EXPECT_FLOAT_EQ(theme.metrics().button_border_width, 0.0f);
        EXPECT_FLOAT_EQ(theme.metrics().checkbox_glyph_stroke, 0.0f);
        EXPECT_FLOAT_EQ(theme.metrics().slider_thumb_shadow, 0.0f);
        EXPECT_FLOAT_EQ(theme.metrics().slider_tick_length, 0.0f);
    }
} // namespace albor
