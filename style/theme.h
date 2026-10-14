#pragma once

#include <cstddef>

#include "core/color_scheme.h"
#include "core/geometry.h"
#include "core/paint.h"

namespace albor
{
    // the visual state a control is in, which selects its paint from the theme (§8, §6.1)
    enum class ControlState
    {
        // the pointer is elsewhere and the control is idle
        Normal,

        // the pointer is inside the control
        Hovered,

        // a press is held on the control
        Pressed,

        // the control refuses input, so its paint comes from the disabled variants
        Disabled,

        // the control holds the keyboard focus, so its paint carries the focus ring
        Focused
    };

    // the text hierarchy a label takes its metrics from, which is what a design system names (§8.4, §9.1)
    enum class TextRole
    {
        // the run is measured at the explicit size the label carries, which is the default
        None,

        // the size body copy is set at, which is where a value readout sits (§8.4)
        Body,

        // the size a caption or a secondary line is set at (§8.4)
        Caption,

        // the size a headline is set at (§8.4)
        Headline,

        // the size a subheadline is set at, which is stronger than body copy (§8.4)
        Subheadline
    };

    // the font sizes a design system sets body, caption, headline and subheadline copy at (§8.4, §9.1)
    struct Typography
    {
        // the size body copy is set at, which is where a plain label sits (§8.4)
        float body = 16.0f;

        // the size a caption is set at, which is the smallest hierarchy entry (§8.4)
        float caption = 11.0f;

        // the size a headline is set at, which is the largest hierarchy entry (§8.4)
        float headline = 20.0f;

        // the size a subheadline is set at, which is one step stronger than body copy (§8.4)
        float subheadline = 13.0f;

        // how much the alpha of a secondary text colour keeps, which is what a secondary label dims by (§8.4)
        float secondary_opacity = 0.65f;

        // compare two hierarchies component wise
        friend bool operator==(const Typography& lhs, const Typography& rhs) = default;
    };

    // the sizing a control takes from the theme, so the metrics are one decision rather than a literal per widget (§8.2)
    struct ControlMetrics
    {
        // the smallest extent a push control measures to, because it has to be grabbable (§8.2)
        Size button_minimum{24.0f, 16.0f};

        // how deep a push control's content sits on either side of its label, which is what a padded control is (§8.2)
        float button_padding_horizontal = 0.0f;

        // how wide a push control strokes its border, where zero draws no border at all (§8.4)
        float button_border_width = 0.0f;

        // the extent of a check box, which is a fixed square (§8.2)
        Size checkbox_box{16.0f, 16.0f};

        // the width of a check box outline, which is what makes an unchecked box visible (§6.1)
        float checkbox_outline = 2.0f;

        // the radius a check box carries at its corners, which is what a native box is (§8.4)
        float checkbox_corner_radius = 0.0f;

        // how wide a check mark strokes, where zero draws no glyph at all (§8.4)
        float checkbox_glyph_stroke = 0.0f;

        // the extent a slider measures to, because a track is thin (§8.2)
        Size slider_extent{120.0f, 16.0f};

        // the thickness of a slider track, which is drawn centred on the control (§6.1)
        float slider_track_thickness = 4.0f;

        // the extent of a slider thumb along the track, which is what the user grabs (§6.1)
        float slider_thumb = 12.0f;

        // the radius a slider track carries at its ends, which is what a rounded track is (§8.4)
        float slider_track_radius = 0.0f;

        // the radius a slider thumb carries, which a half extent turns into a circle (§8.4)
        float slider_thumb_radius = 0.0f;

        // how strong the thumb shadow lifts, where zero casts nothing (§8.4)
        float slider_thumb_shadow = 0.0f;

        // how long a discrete tick is drawn, where zero draws no ticks at all (§8.4)
        float slider_tick_length = 0.0f;

        // how wide a discrete tick strokes (§8.4)
        float slider_tick_width = 2.0f;

        // the amount one key press moves a slider value, because a semantic action carries no exact target (§10.1)
        float slider_step = 0.05f;

        // the corner radius a plate is drawn with, which is what makes a hover state read as a plate (§6.1)
        float corner_radius = 6.0f;

        // how wide a scrollbar is drawn, which overlays the content rather than insetting it (§6.1)
        float scrollbar_extent = 10.0f;

        // the smallest thumb a scrollbar draws, which is what keeps a very long content grabbable (§6.1)
        float scrollbar_thumb_minimum = 24.0f;

        // how far one wheel notch moves the content, which is the policy the scroll container owns (§8.2, §8.3)
        float scroll_line_extent = 40.0f;

        // the smallest extent an editable field measures to, because a field has to be seen as one (§8.2)
        Size text_field_minimum{120.0f, 20.0f};

        // how deep a field's text sits inside its plate on either side, which is what an inset is (§8.2)
        float text_field_padding = 4.0f;

        // how wide a caret is drawn, which is the thickness of the editing bar (§6.1)
        float text_field_caret_width = 1.0f;

        // how wide a focus ring strokes, because the ring is the only thing that says a control takes keys (§6.1)
        float focus_ring_width = 2.0f;

        // how far a focus ring sits outside its control, because a ring on the edge reads as a border (§6.1)
        float focus_ring_offset = 2.0f;

        // the radius a focus ring is drawn with, which a ring shares with the plate it surrounds (§6.2)
        float focus_ring_radius = 8.0f;
    };

    // the colours a theme resolves by role, which is what lets a widget ask instead of carry a literal (§6.1)
    struct Palette
    {
        // the plate of a control, per state, which is the fill behind its content (§6.1)
        Color button_plate[5] = {Color{0.20f, 0.55f, 0.90f, 1.0f}, Color{0.26f, 0.62f, 0.96f, 1.0f}, Color{0.14f, 0.42f, 0.74f, 1.0f}, Color{0.42f, 0.46f, 0.52f, 1.0f}, Color{0.22f, 0.57f, 0.92f, 1.0f}};

        // the border a plate strokes above its fill, per state, which is the second layer of a control (§8.4)
        Color button_border[5] = {Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)};

        // the label colour of a control, per state, which is what the disabled variant fades (§6.1)
        Color button_label[5] = {Color{0.96f, 0.97f, 0.99f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, Color{0.88f, 0.90f, 0.94f, 1.0f}, Color{0.72f, 0.74f, 0.78f, 1.0f}, Color{0.96f, 0.97f, 0.99f, 1.0f}};

        // the fill of a check box, per state, which is the box itself (§6.1)
        Color checkbox_fill[5] = {Color{0.20f, 0.55f, 0.90f, 1.0f}, Color{0.26f, 0.62f, 0.96f, 1.0f}, Color{0.14f, 0.42f, 0.74f, 1.0f}, Color{0.62f, 0.66f, 0.72f, 1.0f}, Color{0.22f, 0.57f, 0.92f, 1.0f}};

        // the outline of a check box, per state, which is what makes the empty box visible (§6.1)
        Color checkbox_outline[5] = {Color{0.45f, 0.50f, 0.58f, 1.0f}, Color{0.60f, 0.66f, 0.74f, 1.0f}, Color{0.34f, 0.40f, 0.48f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}, Color{0.52f, 0.57f, 0.65f, 1.0f}};

        // the glyph a checked box carries, per state, which is what reads over the accent fill (§8.4)
        Color checkbox_glyph[5] = {Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)};

        // the track of a slider, per state, which is the part the thumb has not covered yet (§6.1)
        Color slider_track[5] = {Color{0.24f, 0.27f, 0.32f, 1.0f}, Color{0.28f, 0.32f, 0.38f, 1.0f}, Color{0.20f, 0.23f, 0.28f, 1.0f}, Color{0.40f, 0.42f, 0.46f, 1.0f}, Color{0.26f, 0.29f, 0.34f, 1.0f}};

        // the filled track of a slider, per state, which is the part from the minimum up to the thumb (§8.4)
        Color slider_fill[5] = {Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)};

        // the thumb of a slider, per state, which is what the user grabs (§6.1)
        Color slider_thumb[5] = {Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, Color{0.78f, 0.82f, 0.88f, 1.0f}, Color{0.66f, 0.68f, 0.72f, 1.0f}, Color{0.92f, 0.94f, 0.96f, 1.0f}};

        // the text colour of a plain label, per state, which a label has no press of its own (§6.1)
        Color label_foreground[5] = {Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{0.96f, 0.97f, 0.99f, 1.0f}, Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}, Color{0.92f, 0.94f, 0.96f, 1.0f}};

        // the text an editable field draws, per state, which is the value the user edits (§6.1)
        Color text_field_text[5] = {Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{0.96f, 0.97f, 0.99f, 1.0f}, Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}, Color{0.92f, 0.94f, 0.96f, 1.0f}};

        // the placeholder an empty field draws, per state, which sits de-emphasised behind the value (§6.1)
        Color text_field_placeholder[5] = {Color{0.58f, 0.60f, 0.64f, 1.0f}, Color{0.62f, 0.64f, 0.68f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}, Color{0.42f, 0.44f, 0.48f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}};

        // the caret an editable field draws, per state, which is the bar the user edits between (§6.1)
        Color text_field_caret[5] = {Color{0.94f, 0.96f, 0.98f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, Color{0.86f, 0.89f, 0.93f, 1.0f}, Color{0.52f, 0.55f, 0.60f, 1.0f}, Color{0.94f, 0.96f, 0.98f, 1.0f}};

        // the plate an editable field draws behind its value, per state, which is what a field is seen through (§8.4)
        Color text_field_plate[5] = {Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)};

        // the border an editable field strokes, per state, which is what marks an empty field editable (§8.4)
        Color text_field_border[5] = {Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)};

        // the highlight a selection draws, per state, which the value sits over (§6.1)
        Color text_field_selection[5] = {Color{0.20f, 0.55f, 0.90f, 0.35f}, Color{0.26f, 0.62f, 0.96f, 0.42f}, Color{0.14f, 0.42f, 0.74f, 0.40f}, Color{0.42f, 0.46f, 0.52f, 0.30f}, Color{0.22f, 0.57f, 0.92f, 0.38f}};

        // the groove a scrollbar thumb rides in, per state, which is what shows the travel (§6.1)
        Color scrollbar_track[5] = {Color{0.16f, 0.18f, 0.22f, 0.0f}, Color{0.18f, 0.20f, 0.24f, 0.0f}, Color{0.16f, 0.18f, 0.22f, 0.0f}, Color{0.24f, 0.26f, 0.30f, 0.0f}, Color{0.16f, 0.18f, 0.22f, 0.0f}};

        // the thumb of a scrollbar, per state, which is what the user drags (§6.1)
        Color scrollbar_thumb[5] = {Color{0.42f, 0.46f, 0.52f, 1.0f}, Color{0.58f, 0.62f, 0.68f, 1.0f}, Color{0.72f, 0.76f, 0.82f, 1.0f}, Color{0.28f, 0.30f, 0.34f, 1.0f}, Color{0.42f, 0.46f, 0.52f, 1.0f}};

        // the ring a focused control strokes around itself, per state, whose normal entry is never read (§6.1)
        Color focus_ring[5] = {Color{0.20f, 0.55f, 0.90f, 0.0f}, Color{0.26f, 0.62f, 0.96f, 1.0f}, Color{0.14f, 0.42f, 0.74f, 1.0f}, Color{0.42f, 0.46f, 0.52f, 0.55f}, Color{0.20f, 0.55f, 0.90f, 1.0f}};

        // the halo a focused control strokes under its ring, per state, which is the second layer of the focus (§8.4)
        Color focus_ring_outer[5] = {Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f)};

        // the plate a canvas draws behind its content, per state, which is what a state visual needs to land on (§6.1)
        Color canvas_background[5] = {Color{0.11f, 0.12f, 0.15f, 1.0f}, Color{0.15f, 0.17f, 0.21f, 1.0f}, Color{0.09f, 0.10f, 0.13f, 1.0f}, Color{0.18f, 0.19f, 0.22f, 1.0f}, Color{0.13f, 0.14f, 0.18f, 1.0f}};
    };

    // the platform preset a theme is built through, which decides the face style and the metrics (§8.4)
    enum class ThemePreset
    {
        // the derived palette with no native shaping, which is the portable baseline (§6.1)
        Derived,

        // the macos preset, which reads as the macos control set (§8.4)
        Macos,

        // the fluent preset, which reads as the windows control set (§8.4)
        Fluent,

        // the pro dark preset, which reads as a professional tool surface (§8.4)
        ProDark
    };

    // the theme that binds a palette to control metrics, which the widget base resolves through (§6.1, §8.2)
    class Theme
    {
    public:
        // build the default theme, which is what an application gets before it installs one of its own (§6.1)
        Theme() = default;

        // hand out the colours, which the widget base resolves a role through (§6.1)
        [[nodiscard]] const Palette& palette() const;

        // hand out the sizing, which the widget base measures from (§8.2)
        [[nodiscard]] const ControlMetrics& metrics() const;

        // hand out the type hierarchy, which a text role resolves its size and its secondary dim through (§8.4)
        [[nodiscard]] const Typography& typography() const;

        // build the macos preset from the accent set, which shapes every state the high level interface sizes (§8.4)
        [[nodiscard]] static Theme macos(const struct Accents& accents, ColorScheme scheme);

        // build the fluent preset from the accent set, which shapes every state the windows interface sizes (§8.4)
        [[nodiscard]] static Theme fluent(const struct Accents& accents, ColorScheme scheme);

        // build the pro dark preset from the accent set, which is the professional tool surface style (§8.4)
        [[nodiscard]] static Theme pro_dark(const struct Accents& accents);

        // replace one control state, which is how a theme overrides a single entry (§6.1)
        void set_button_plate(ControlState state, Color value);

        // replace the plate of the normal state, which is the common case (§6.1)
        void set_button_plate(Color value);

        // replace the label colour of the normal state (§6.1)
        void set_button_label(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_button_label(ControlState state, Color value);

        // replace the fill of the normal state of a check box (§6.1)
        void set_checkbox_fill(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_checkbox_fill(ControlState state, Color value);

        // replace the outline of the normal state of a check box (§6.1)
        void set_checkbox_outline(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_checkbox_outline(ControlState state, Color value);

        // replace the glyph of the normal state of a check box, which is what reads over the fill (§8.4)
        void set_checkbox_glyph(Color value);

        // replace one state of the glyph, which is what a derived palette writes (§8.4)
        void set_checkbox_glyph(ControlState state, Color value);

        // replace the track of the normal state of a slider (§6.1)
        void set_slider_track(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_slider_track(ControlState state, Color value);

        // replace the thumb of the normal state of a slider (§6.1)
        void set_slider_thumb(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_slider_thumb(ControlState state, Color value);

        // replace one state of the filled track a slider draws from the minimum to the thumb (§8.4)
        void set_slider_fill(ControlState state, Color value);

        // replace the fill of the normal state of the covered track, which is the common case (§8.4)
        void set_slider_fill(Color value);

        // replace the text colour of the normal state of a label (§6.1)
        void set_label_foreground(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_label_foreground(ControlState state, Color value);

        // replace the text colour of the normal state of an editable field (§6.1)
        void set_text_field_text(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_text_field_text(ControlState state, Color value);

        // replace the placeholder colour of the normal state of an editable field (§6.1)
        void set_text_field_placeholder(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_text_field_placeholder(ControlState state, Color value);

        // replace the caret colour of the normal state of an editable field (§6.1)
        void set_text_field_caret(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_text_field_caret(ControlState state, Color value);

        // replace the plate of the normal state of an editable field, which is what a field reads through (§8.4)
        void set_text_field_plate(Color value);

        // replace one state of the field plate, which is what a derived palette writes (§8.4)
        void set_text_field_plate(ControlState state, Color value);

        // replace the border of the normal state of an editable field, which is what marks it editable (§8.4)
        void set_text_field_border(Color value);

        // replace one state of the field border, which is what a derived palette writes (§8.4)
        void set_text_field_border(ControlState state, Color value);

        // replace the selection colour of the normal state of an editable field (§6.1)
        void set_text_field_selection(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_text_field_selection(ControlState state, Color value);

        // replace the smallest push control extent, which is a sizing decision rather than a colour (§8.2)
        void set_button_minimum(Size value);

        // replace how deep a push control's content sits beside its label, which is a sizing decision (§8.2)
        void set_button_padding_horizontal(float value);

        // replace how wide a push control strokes its border, which is the second layer one asks for (§8.4)
        void set_button_border_width(float value);

        // replace one state of the plate border, which is the second layer a control resolves (§8.4)
        void set_button_border(ControlState state, Color value);

        // replace the border of the normal state of a plate, which is the common case (§8.4)
        void set_button_border(Color value);

        // replace the corner radius a plate is drawn with (§6.1)
        void set_corner_radius(float value);

        // replace the extent a slider measures to (§8.2)
        void set_slider_extent(Size value);

        // replace the thickness a slider track is drawn at (§6.1)
        void set_slider_track_thickness(float value);

        // replace the extent of a slider thumb, which is what the user grabs (§6.1)
        void set_slider_thumb(float value);

        // replace the radius a slider track carries at its ends (§8.4)
        void set_slider_track_radius(float value);

        // replace the radius a slider thumb is drawn with (§8.4)
        void set_slider_thumb_radius(float value);

        // replace how strong the thumb shadow lifts, which is what elevation means to a thumb (§8.4)
        void set_slider_thumb_shadow(float value);

        // replace how long a discrete tick is drawn, which is what a ticked slider is (§8.4)
        void set_slider_tick_length(float value);

        // replace how wide a discrete tick strokes (§8.4)
        void set_slider_tick_width(float value);

        // replace the amount one key press moves a slider value (§10.1)
        void set_slider_step(float value);

        // replace the extent a check box measures to, which the control reads instead of a literal (§8.2)
        void set_checkbox_box(Size value);

        // replace the width a check box outline strokes at (§6.1)
        void set_checkbox_outline(float value);

        // replace the radius a check box carries at its corners (§8.4)
        void set_checkbox_corner_radius(float value);

        // replace how wide a check mark strokes, which is what makes the glyph visible (§8.4)
        void set_checkbox_glyph_stroke(float value);

        // replace how wide a scrollbar is drawn, which is the gutter a scroll container reserves (§6.1)
        void set_scrollbar_extent(float value);

        // replace the smallest thumb a scrollbar draws (§6.1)
        void set_scrollbar_thumb_minimum(float value);

        // replace how far one wheel notch moves the content, which is the policy the scroll container owns (§8.2, §8.3)
        void set_scroll_line_extent(float value);

        // replace one state of the scrollbar groove, which is what the travel is drawn with (§6.1)
        void set_scrollbar_track(ControlState state, Color value);

        // replace one state of the scrollbar thumb, which is what the user drags (§6.1)
        void set_scrollbar_thumb(ControlState state, Color value);

        // replace the ring colour a focused control strokes, which is the shorthand an application writes (§6.1)
        void set_focus_ring(Color value);

        // replace one state of the ring, so a hover and a press change the ring with the plate (§6.1)
        void set_focus_ring(ControlState state, Color value);

        // replace one state of the halo a focused control strokes under its ring (§8.4)
        void set_focus_ring_outer(ControlState state, Color value);

        // replace the halo colour of the focused state, which is the shorthand an application writes (§8.4)
        void set_focus_ring_outer(Color value);

        // replace the plate of the normal state a canvas draws behind its content (§6.1)
        void set_canvas_background(Color value);

        // replace one state of the canvas plate, which is what a state visual reads (§6.1)
        void set_canvas_background(ControlState state, Color value);

        // replace how wide a focus ring strokes, which is what makes it readable (§6.1)
        void set_focus_ring_width(float value);

        // replace how far a focus ring sits outside its control, because a ring on the edge reads as a border (§6.1)
        void set_focus_ring_offset(float value);

        // replace the radius a focus ring is drawn with, which it shares with the plate (§6.2)
        void set_focus_ring_radius(float value);

        // replace the smallest extent an editable field measures to, which the control reads instead of a literal (§8.2)
        void set_text_field_minimum(Size value);

        // replace how deep a field's text sits inside its plate, which is the inset an inset is (§8.2)
        void set_text_field_padding(float value);

        // replace the size one text role is set at, which is what a hierarchy carries (§8.4)
        void set_role_font_size(TextRole role, float size);

        // replace how much alpha a secondary text colour keeps, which is what a secondary role dims by (§8.4)
        void set_secondary_opacity(float value);

        // hand out the surface colour, which a frame background resolves through (§6.1)
        [[nodiscard]] Color surface() const;

        // replace the surface colour, which is what a derived palette sets (§6.1)
        void set_surface(Color value);

    private:
        // the colours, which the overrides above write (§6.1)
        Palette m_palette;

        // the sizing, which the overrides above write (§8.2)
        ControlMetrics m_metrics;

        // the type hierarchy, which the role overrides above write (§8.4)
        Typography m_typography;

        // the surface colour, which a frame background resolves through (§6.1)
        Color m_surface{0.07f, 0.08f, 0.10f, 1.0f};
    };

    // the index a state maps to, because the palette stores one colour per state in declaration order (§6.1)
    [[nodiscard]] constexpr std::size_t state_index(ControlState state) { return static_cast<std::size_t>(state); }
} // namespace albor
