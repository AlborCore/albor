#include "style/theme.h"

#include <algorithm>

#include "style/theme_builder.h"

namespace albor
{
    void Theme::set_role_font_size(TextRole role, float size) {
        // the role needs no index lookup, because the hierarchy keeps one member per entry (§8.4)
        switch (role) {
        // the body size is where plain copy sits (§8.4)
        case TextRole::Body:
            m_typography.body = size;
            return;
        // the caption size is the smallest entry (§8.4)
        case TextRole::Caption:
            m_typography.caption = size;
            return;
        // the headline size is the largest entry (§8.4)
        case TextRole::Headline:
            m_typography.headline = size;
            return;
        // the subheadline size is one step stronger than body copy (§8.4)
        case TextRole::Subheadline:
            m_typography.subheadline = size;
            return;
        // the none role carries no size of its own, because the explicit size decides (§8.4)
        default:
            return;
        }
    }

    void Theme::set_secondary_opacity(float value) {
        // the token is what a secondary label dims its colour by, which is one value (§8.4)
        m_typography.secondary_opacity = value;
    }

    namespace
    {

        // the white glyph states a check mark reads over the accent fill with (§8.4)
        void apply_box_glyph(Theme& theme) {
            // the glyph is the strongest value on the accent, so every state carries the white mark (§8.4)
            const Color mark = Color::rgb(1.0f, 1.0f, 1.0f);
            theme.set_checkbox_glyph(ControlState::Normal, mark);
            theme.set_checkbox_glyph(ControlState::Hovered, mark);
            theme.set_checkbox_glyph(ControlState::Pressed, mark);
            theme.set_checkbox_glyph(ControlState::Disabled, Color::rgba(mark.red, mark.green, mark.blue, 0.5f));
            theme.set_checkbox_glyph(ControlState::Focused, mark);
        }
        // the measure override of the macos preset, which sizes a control the way the interface guide does (§8.4)
        void apply_macos_metrics(Theme& theme) {
            // a push control is twenty eight tall, which is the standard system metric (§8.4)
            theme.set_button_minimum(Size{24.0f, 28.0f});
            // the label sits twelve from either side of the plate, which is the padded metric (§8.2)
            theme.set_button_padding_horizontal(12.0f);
            // a hairline border separates the plate from the surface, which is the layer the control strokes (§8.4)
            theme.set_button_border_width(1.0f);
            // a round corner radius of five is the control corner the guide names (§8.4)
            theme.set_corner_radius(5.0f);
            // a field is twenty eight tall as well, which is what a form line reads as (§8.4)
            theme.set_text_field_minimum(Size{160.0f, 28.0f});
            // a field insets its value six from either side (§8.2)
            theme.set_text_field_padding(6.0f);
            // a check box is a fourteen point square with a single point outline (§8.4)
            theme.set_checkbox_box(Size{14.0f, 14.0f});
            theme.set_checkbox_outline(1.0f);
            // the box rounds two, which is the corner a small square carries (§8.4)
            theme.set_checkbox_corner_radius(2.0f);
            theme.set_checkbox_glyph_stroke(1.8f);
            // a macos check reads white over the accent fill, which is the mark the system carries (§8.4)
            apply_box_glyph(theme);
            // a slider spans two hundred twenty at twenty two tall, with a twenty two thumb (§8.4)
            theme.set_slider_extent(Size{220.0f, 22.0f});
            theme.set_slider_track_thickness(4.0f);
            theme.set_slider_thumb(22.0f);
            // the track rounds into a capsule and the thumb rounds into a circle (§8.4)
            theme.set_slider_track_radius(2.0f);
            theme.set_slider_thumb_radius(11.0f);
            // the ring stands one away and rounds with the plate it surrounds (§6.1, §6.2)
            theme.set_focus_ring_offset(1.0f);
            theme.set_focus_ring_radius(6.0f);
            // the hierarchy resolves at the sizes the system font is set at (§8.4)
            theme.set_role_font_size(TextRole::Body, 13.0f);
            theme.set_role_font_size(TextRole::Caption, 10.0f);
            theme.set_role_font_size(TextRole::Headline, 17.0f);
            theme.set_role_font_size(TextRole::Subheadline, 11.0f);
        }

        // the measure override of the fluent preset, which sizes a control the way the windows interface does (§8.4)
        void apply_fluent_metrics(Theme& theme) {
            // a push control is thirty two tall, which is the standard fluent control (§8.4)
            theme.set_button_minimum(Size{24.0f, 32.0f});
            theme.set_button_padding_horizontal(12.0f);
            // fluent strokes a one point outline, which is the control line the design language asks for (§8.4)
            theme.set_button_border_width(1.0f);
            // a four point radius is the fluent corner (§8.4)
            theme.set_corner_radius(4.0f);
            theme.set_text_field_minimum(Size{160.0f, 32.0f});
            theme.set_text_field_padding(6.0f);
            // a fluent box is eighteen square with a one and a half point outline (§8.4)
            theme.set_checkbox_box(Size{18.0f, 18.0f});
            theme.set_checkbox_outline(1.5f);
            theme.set_checkbox_corner_radius(4.0f);
            theme.set_checkbox_glyph_stroke(2.0f);
            // a fluent check reads white over the accent fill, which is the mark the language carries (§8.4)
            apply_box_glyph(theme);
            // a fluent slider is eighteen tall with a sixteen thumb (§8.4)
            theme.set_slider_extent(Size{220.0f, 18.0f});
            theme.set_slider_track_thickness(4.0f);
            theme.set_slider_thumb(16.0f);
            theme.set_slider_track_radius(2.0f);
            theme.set_slider_thumb_radius(8.0f);
            theme.set_slider_thumb_shadow(0.14f);
            // discrete steps on a fluent slider show their ticks (§8.4)
            theme.set_slider_tick_length(4.0f);
            theme.set_slider_tick_width(1.5f);
            theme.set_focus_ring_offset(1.0f);
            theme.set_focus_ring_radius(5.0f);
            // the hierarchy resolves at the sizes the system font is set at (§8.4)
            theme.set_role_font_size(TextRole::Body, 14.0f);
            theme.set_role_font_size(TextRole::Caption, 12.0f);
            theme.set_role_font_size(TextRole::Headline, 20.0f);
            theme.set_role_font_size(TextRole::Subheadline, 12.0f);
        }

        // the measure override of the pro dark preset, which sizes a control the way a tool surface does (§8.4)
        void apply_pro_dark_metrics(Theme& theme) {
            // a tool control is compact, twenty four tall, with eight point padding (§8.4)
            theme.set_button_minimum(Size{24.0f, 24.0f});
            theme.set_button_padding_horizontal(8.0f);
            theme.set_corner_radius(4.0f);
            theme.set_text_field_minimum(Size{160.0f, 26.0f});
            theme.set_text_field_padding(6.0f);
            theme.set_checkbox_box(Size{16.0f, 16.0f});
            theme.set_checkbox_outline(1.0f);
            theme.set_checkbox_corner_radius(2.0f);
            theme.set_checkbox_glyph_stroke(1.8f);
            // a tool check reads white over the accent fill, which is the mark the surface carries (§8.4)
            apply_box_glyph(theme);
            // a tool slider keeps its hairline track with a fourteen round thumb (§8.4)
            theme.set_slider_extent(Size{220.0f, 16.0f});
            theme.set_slider_track_thickness(3.0f);
            theme.set_slider_thumb(14.0f);
            theme.set_slider_track_radius(1.5f);
            theme.set_slider_thumb_radius(7.0f);
            theme.set_slider_thumb_shadow(0.14f);
            theme.set_focus_ring_offset(1.0f);
            theme.set_focus_ring_radius(5.0f);
            // the hierarchy resolves at the reading sizes a tool screen carries (§8.4)
            theme.set_role_font_size(TextRole::Body, 13.0f);
            theme.set_role_font_size(TextRole::Caption, 11.0f);
            theme.set_role_font_size(TextRole::Headline, 16.0f);
            theme.set_role_font_size(TextRole::Subheadline, 13.0f);
        }

        // the macos preset plates, which read as control faces rather than colour blocks (§8.4)
        void apply_macos_plates(Theme& theme, const Color& surface, const Color& on_surface, ColorScheme scheme) {
            // a control face lifts slightly off the window surface, which is the depth the guide asks for (§8.4)
            const Color face = scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.16f) : ThemeBuilder::lighten(surface, 0.05f);
            // a hover lifts a little more, which is the gentle change a pointer asks for (§8.4)
            theme.set_button_plate(ControlState::Normal, face);
            theme.set_button_plate(ControlState::Hovered, ThemeBuilder::lighten(face, 0.05f));
            // a press sinks slightly, because pressing in is the direction the pointer reads (§8.4)
            theme.set_button_plate(ControlState::Pressed, ThemeBuilder::darken(face, 0.10f));
            theme.set_button_plate(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.45f));
            theme.set_button_plate(ControlState::Focused, ThemeBuilder::lighten(face, 0.03f));
            // the label sits on the neutral face, so it is the readable text colour at every state (§8.4)
            theme.set_button_label(ControlState::Normal, on_surface);
            theme.set_button_label(ControlState::Hovered, on_surface);
            theme.set_button_label(ControlState::Pressed, on_surface);
            theme.set_button_label(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.45f));
            theme.set_button_label(ControlState::Focused, on_surface);
            // the border is a hairline of the text colour, which separates a face that is close to the surface (§8.4)
            const Color hairline = Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.12f : 0.30f);
            theme.set_button_border(ControlState::Normal, hairline);
            theme.set_button_border(ControlState::Hovered, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.18f : 0.38f));
            theme.set_button_border(ControlState::Pressed, hairline);
            theme.set_button_border(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.06f : 0.14f));
            theme.set_button_border(ControlState::Focused, hairline);
            // the canvas plate lifts like a face of its own (§8.4)
            theme.set_canvas_background(ControlState::Normal, face);
            theme.set_canvas_background(ControlState::Hovered, face);
            theme.set_canvas_background(ControlState::Pressed, ThemeBuilder::darken(face, 0.06f));
            theme.set_canvas_background(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.5f));
            theme.set_canvas_background(ControlState::Focused, face);
            // a macos groove is a light grey line, which is what a native track reads as (§8.4)
            theme.set_slider_track(ControlState::Normal, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.16f) : Color::rgb(0.70f, 0.71f, 0.74f));
            theme.set_slider_track(ControlState::Hovered, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.20f) : Color::rgb(0.76f, 0.77f, 0.80f));
            theme.set_slider_track(ControlState::Pressed, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.12f) : Color::rgb(0.65f, 0.66f, 0.69f));
            theme.set_slider_track(ControlState::Disabled, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.10f) : Color::rgb(0.78f, 0.79f, 0.81f));
            theme.set_slider_track(ControlState::Focused, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.16f) : Color::rgb(0.70f, 0.71f, 0.74f));
            // the knob lifts on a whisper of a shadow, which is what a native knob casts (§8.4)
            theme.set_slider_thumb_shadow(0.14f);
        }

        // the neutral plate and its states in the fluent preset, which is a filled face over a one point outline (§8.4)
        void apply_fluent_plates(Theme& theme, const Color& surface, const Color& on_surface, ColorScheme scheme) {
            // a fluent control face is a low contrast lift of the surface, plus its stroke (§8.4)
            const Color face = scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.12f) : ThemeBuilder::lighten(surface, 0.03f);
            theme.set_button_plate(ControlState::Normal, face);
            theme.set_button_plate(ControlState::Hovered, face);
            // a press barely moves the face, because the border carries the interaction signal (§8.4)
            theme.set_button_plate(ControlState::Pressed, scheme == ColorScheme::Dark ? ThemeBuilder::darken(face, 0.06f) : ThemeBuilder::darken(face, 0.04f));
            theme.set_button_plate(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.5f));
            theme.set_button_plate(ControlState::Focused, face);
            // the label is readable at every state, which is what a face is for (§8.4)
            theme.set_button_label(ControlState::Normal, on_surface);
            theme.set_button_label(ControlState::Hovered, on_surface);
            theme.set_button_label(ControlState::Pressed, on_surface);
            theme.set_button_label(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.5f));
            theme.set_button_label(ControlState::Focused, on_surface);
            // the outline is the interaction layer, so it brightens on a hover and dims under a disabled face (§8.4)
            const Color line = Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.09f : 0.42f);
            theme.set_button_border(ControlState::Normal, line);
            theme.set_button_border(ControlState::Hovered, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.16f : 0.58f));
            theme.set_button_border(ControlState::Pressed, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.05f : 0.28f));
            theme.set_button_border(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, scheme == ColorScheme::Dark ? 0.04f : 0.18f));
            theme.set_button_border(ControlState::Focused, line);
            theme.set_canvas_background(ControlState::Normal, face);
            theme.set_canvas_background(ControlState::Hovered, face);
            theme.set_canvas_background(ControlState::Pressed, scheme == ColorScheme::Dark ? ThemeBuilder::darken(face, 0.04f) : face);
            theme.set_canvas_background(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.5f));
            theme.set_canvas_background(ControlState::Focused, face);
            // a fluent groove is a mid grey line, which is what the language draws (§8.4)
            theme.set_slider_track(ControlState::Normal, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.14f) : Color::rgb(0.60f, 0.60f, 0.62f));
            theme.set_slider_track(ControlState::Hovered, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.18f) : Color::rgb(0.66f, 0.66f, 0.68f));
            theme.set_slider_track(ControlState::Pressed, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.10f) : Color::rgb(0.55f, 0.55f, 0.57f));
            theme.set_slider_track(ControlState::Disabled, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.09f) : Color::rgb(0.70f, 0.70f, 0.72f));
            theme.set_slider_track(ControlState::Focused, scheme == ColorScheme::Dark ? ThemeBuilder::lighten(surface, 0.14f) : Color::rgb(0.60f, 0.60f, 0.62f));
            theme.set_slider_thumb_shadow(0.14f);
        }

        // the neutral plate and its states in the pro dark preset, which separates through contrast alone (§8.4)
        void apply_pro_dark_plates(Theme& theme, const Accents& accents) {
            // a tool face lifts a step off the surface, and no border separates the two layers (§8.4)
            const Color face = ThemeBuilder::lighten(accents.surface, 0.10f);
            theme.set_button_plate(ControlState::Normal, face);
            theme.set_button_plate(ControlState::Hovered, ThemeBuilder::lighten(face, 0.08f));
            theme.set_button_plate(ControlState::Pressed, ThemeBuilder::darken(face, 0.14f));
            theme.set_button_plate(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.45f));
            theme.set_button_plate(ControlState::Focused, ThemeBuilder::lighten(face, 0.04f));
            // the label is the text colour itself, which is what a tool surface keeps contrast with (§8.4)
            theme.set_button_label(ControlState::Normal, accents.on_surface);
            theme.set_button_label(ControlState::Hovered, accents.on_surface);
            theme.set_button_label(ControlState::Pressed, accents.on_surface);
            theme.set_button_label(ControlState::Disabled, Color::rgba(accents.on_surface.red, accents.on_surface.green, accents.on_surface.blue, 0.45f));
            theme.set_button_label(ControlState::Focused, accents.on_surface);
            // a press on a tool face carries no border, because the contrast already separates (§8.4)
            for (const ControlState state : {ControlState::Normal, ControlState::Hovered, ControlState::Pressed, ControlState::Disabled, ControlState::Focused})
                theme.set_button_border(state, Color::rgba(0.0f, 0.0f, 0.0f, 0.0f));
            // the canvas plate is the surface lifted like any face (§8.4)
            theme.set_canvas_background(ControlState::Normal, face);
            theme.set_canvas_background(ControlState::Hovered, ThemeBuilder::lighten(face, 0.04f));
            theme.set_canvas_background(ControlState::Pressed, ThemeBuilder::darken(face, 0.08f));
            theme.set_canvas_background(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.5f));
            theme.set_canvas_background(ControlState::Focused, ThemeBuilder::lighten(face, 0.02f));
        }

        // the dual layer ring states, where the halo carries the ring colour at a lower alpha (§8.4)
        void apply_derived_focus(Theme& theme, const Color& accent) {
            // the halo is the ring colour dimmed, which is the soft layer a native focus carries (§8.4)
            theme.set_focus_ring_outer(ControlState::Normal, Color::rgba(accent.red, accent.green, accent.blue, 0.0f));
            theme.set_focus_ring_outer(ControlState::Hovered, Color::rgba(accent.red, accent.green, accent.blue, 0.45f));
            theme.set_focus_ring_outer(ControlState::Pressed, Color::rgba(accent.red, accent.green, accent.blue, 0.30f));
            theme.set_focus_ring_outer(ControlState::Disabled, Color::rgba(accent.red, accent.green, accent.blue, 0.20f));
            theme.set_focus_ring_outer(ControlState::Focused, Color::rgba(accent.red, accent.green, accent.blue, 0.45f));
            // a track fills with the accent from the minimum up to the thumb (§8.4)
            theme.set_slider_fill(ControlState::Normal, accent);
            theme.set_slider_fill(ControlState::Hovered, ThemeBuilder::lighten(accent, 0.12f));
            theme.set_slider_fill(ControlState::Pressed, ThemeBuilder::darken(accent, 0.25f));
            theme.set_slider_fill(ControlState::Disabled, Color::rgba(accent.red, accent.green, accent.blue, 0.45f));
            theme.set_slider_fill(ControlState::Focused, accent);
            // a field plate is the surface itself with a hairline border, which is what a field reads as (§8.4)
            const Color face = theme.surface();
            theme.set_text_field_plate(ControlState::Normal, face);
            theme.set_text_field_plate(ControlState::Hovered, face);
            theme.set_text_field_plate(ControlState::Pressed, face);
            theme.set_text_field_plate(ControlState::Disabled, Color::rgba(face.red, face.green, face.blue, 0.5f));
            theme.set_text_field_plate(ControlState::Focused, face);
            // the border is the hairline the empty field carries, and the accent is the focused one (§8.4)
            theme.set_text_field_border(ControlState::Normal, theme.palette().button_border[state_index(ControlState::Normal)]);
            theme.set_text_field_border(ControlState::Hovered, theme.palette().button_border[state_index(ControlState::Hovered)]);
            theme.set_text_field_border(ControlState::Pressed, theme.palette().button_border[state_index(ControlState::Pressed)]);
            theme.set_text_field_border(ControlState::Disabled, theme.palette().button_border[state_index(ControlState::Disabled)]);
            // a focused field carries the accent line, which is what a form marks the active entry with (§8.4)
            theme.set_text_field_border(ControlState::Focused, accent);
        }
    } // namespace

    Theme Theme::macos(const Accents& accents, ColorScheme scheme) {
        // the states derive from the accents like any theme, so only the native shape differs (§8.4)
        Theme theme = ThemeBuilder::build(accents, scheme);
        // the plates are overridden to neutral control faces, which is what a macos push control reads as (§8.4)
        apply_macos_plates(theme, accents.surface, accents.on_surface, scheme);
        // the metrics are the ones the interface guide sizes (§8.4)
        apply_macos_metrics(theme);
        // the dual layer ring halo rides on every preset alike (§8.4)
        apply_derived_focus(theme, accents.accent);
        // the surface is what everything else sits on (§6.1)
        theme.set_surface(accents.surface);
        return theme;
    }

    Theme Theme::fluent(const Accents& accents, ColorScheme scheme) {
        // the states derive from the accents like any theme, so only the native shape differs (§8.4)
        Theme theme = ThemeBuilder::build(accents, scheme);
        // the plates are overridden to the fluent face and line style (§8.4)
        apply_fluent_plates(theme, accents.surface, accents.on_surface, scheme);
        // the metrics are the ones the windows interface sizes (§8.4)
        apply_fluent_metrics(theme);
        // the dual layer ring halo rides on every preset alike (§8.4)
        apply_derived_focus(theme, accents.accent);
        // the surface is what everything else sits on (§6.1)
        theme.set_surface(accents.surface);
        return theme;
    }

    Theme Theme::pro_dark(const Accents& accents) {
        // a pro surface is a dark one, which is what a tool screen carries (§8.4)
        Theme theme = ThemeBuilder::build(accents, ColorScheme::Dark);
        // the plates are overridden to the tool surface contrast (§8.4)
        apply_pro_dark_plates(theme, accents);
        // the metrics are the ones a tool screen sizes (§8.4)
        apply_pro_dark_metrics(theme);
        // the dual layer ring halo rides on every preset alike (§8.4)
        apply_derived_focus(theme, accents.accent);
        // the surface is what everything else sits on (§6.1)
        theme.set_surface(accents.surface);
        return theme;
    }

    const Palette& Theme::palette() const {
        // the colours are stored by value, so a caller reads the installed palette and not a copy (§6.1)
        return m_palette;
    }

    const ControlMetrics& Theme::metrics() const {
        // the sizing is stored by value, so a widget measures from the installed metrics (§8.2)
        return m_metrics;
    }

    const Typography& Theme::typography() const {
        // the hierarchy is stored by value, so a text role reads the installed one (§8.4)
        return m_typography;
    }

    void Theme::set_button_plate(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.button_plate[state_index(state)] = value;
    }

    void Theme::set_button_plate(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        set_button_plate(ControlState::Normal, value);
    }

    void Theme::set_button_label(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.button_label[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_button_label(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.button_label[state_index(state)] = value;
    }

    void Theme::set_checkbox_fill(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.checkbox_fill[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_checkbox_fill(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.checkbox_fill[state_index(state)] = value;
    }

    void Theme::set_checkbox_outline(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.checkbox_outline[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_checkbox_outline(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.checkbox_outline[state_index(state)] = value;
    }

    void Theme::set_checkbox_glyph(Color value) {
        // the normal state is the common case, so it gets its own entry point (§8.4)
        m_palette.checkbox_glyph[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_checkbox_glyph(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§8.4)
        m_palette.checkbox_glyph[state_index(state)] = value;
    }

    void Theme::set_slider_track(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.slider_track[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_slider_track(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.slider_track[state_index(state)] = value;
    }

    void Theme::set_slider_thumb(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.slider_thumb[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_slider_thumb(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.slider_thumb[state_index(state)] = value;
    }

    void Theme::set_label_foreground(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.label_foreground[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_label_foreground(ControlState state, Color value) {
        // the state selects the entry, because the palette keeps one colour per state in order (§6.1)
        m_palette.label_foreground[state_index(state)] = value;
    }

    void Theme::set_button_minimum(Size value) {
        // the extent is stored whole, because a control takes one minimum for both axes (§8.2)
        m_metrics.button_minimum = value;
    }

    void Theme::set_button_padding_horizontal(float value) {
        // the padding is one value because a control insets its label symmetrically (§8.2)
        m_metrics.button_padding_horizontal = value;
    }

    void Theme::set_button_border_width(float value) {
        // the width is one value because a border strokes evenly on every side (§8.4)
        m_metrics.button_border_width = value;
    }

    void Theme::set_button_border(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§8.4)
        m_palette.button_border[state_index(state)] = value;
    }

    void Theme::set_button_border(Color value) {
        // the normal state is the common case, which is the shorthand (§8.4)
        m_palette.button_border[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_checkbox_box(Size value) {
        // the extent is stored whole, because a check box is one square (§8.2)
        m_metrics.checkbox_box = value;
    }

    void Theme::set_checkbox_outline(float value) {
        // the outline is one width because the stroke has no per side override (§6.1)
        m_metrics.checkbox_outline = value;
    }

    void Theme::set_checkbox_corner_radius(float value) {
        // the radius is one value because a box rounds every corner the same (§8.4)
        m_metrics.checkbox_corner_radius = value;
    }

    void Theme::set_checkbox_glyph_stroke(float value) {
        // the stroke is one value because a check mark draws as one polyline (§8.4)
        m_metrics.checkbox_glyph_stroke = value;
    }

    void Theme::set_slider_extent(Size value) {
        // the extent is stored whole, because the control measures to one box (§8.2)
        m_metrics.slider_extent = value;
    }

    void Theme::set_slider_track_thickness(float value) {
        // the thickness is one value because the track is drawn as a single band (§6.1)
        m_metrics.slider_track_thickness = value;
    }

    void Theme::set_slider_thumb(float value) {
        // the thumb is one value because it is a square (§6.1)
        m_metrics.slider_thumb = value;
    }

    void Theme::set_slider_track_radius(float value) {
        // the radius is one value because a track rounds every end the same (§8.4)
        m_metrics.slider_track_radius = value;
    }

    void Theme::set_slider_thumb_radius(float value) {
        // the radius is one value because a thumb is one shape (§8.4)
        m_metrics.slider_thumb_radius = value;
    }

    void Theme::set_slider_thumb_shadow(float value) {
        // the strength is one value because a thumb casts one shadow whatever its state (§8.4)
        m_metrics.slider_thumb_shadow = value;
    }

    void Theme::set_slider_tick_length(float value) {
        // the length is one value because a tick draws the same on every end (§8.4)
        m_metrics.slider_tick_length = value;
    }

    void Theme::set_slider_tick_width(float value) {
        // the width is one value because a tick strokes evenly (§8.4)
        m_metrics.slider_tick_width = value;
    }

    void Theme::set_slider_step(float value) {
        // the step is one value because a key press moves the value symmetrically (§10.1)
        m_metrics.slider_step = value;
    }

    Color Theme::surface() const {
        // the surface is stored by value, so a frame background reads the installed one (§6.1)
        return m_surface;
    }

    void Theme::set_surface(Color value) {
        // the surface is one value because the whole frame shares it (§6.1)
        m_surface = value;
    }

    void Theme::set_corner_radius(float value) {
        // the radius is one value because a plate has no per corner override in this first theme (§6.1)
        m_metrics.corner_radius = value;
    }

    void Theme::set_scrollbar_extent(float value) {
        // the width is one value because a bar has the same thickness on both edges (§6.1)
        m_metrics.scrollbar_extent = value;
    }

    void Theme::set_scrollbar_thumb_minimum(float value) {
        // the floor is one value because a thumb has no per state minimum (§6.1)
        m_metrics.scrollbar_thumb_minimum = value;
    }

    void Theme::set_scroll_line_extent(float value) {
        // the policy is one value because every notch moves the content the same distance (§8.2, §8.3)
        m_metrics.scroll_line_extent = value;
    }

    void Theme::set_scrollbar_track(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.scrollbar_track[state_index(state)] = value;
    }

    void Theme::set_scrollbar_thumb(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.scrollbar_thumb[state_index(state)] = value;
    }

    void Theme::set_focus_ring(Color value) {
        // the ring draws on the focus alone, so the focused slot is the one a caller means (§6.1)
        m_palette.focus_ring[state_index(ControlState::Focused)] = value;
    }

    void Theme::set_focus_ring(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.focus_ring[state_index(state)] = value;
    }

    void Theme::set_focus_ring_outer(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§8.4)
        m_palette.focus_ring_outer[state_index(state)] = value;
    }

    void Theme::set_focus_ring_outer(Color value) {
        // the halo draws on the focus alone, so the focused slot is the one a caller means (§8.4)
        m_palette.focus_ring_outer[state_index(ControlState::Focused)] = value;
    }

    void Theme::set_slider_fill(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§8.4)
        m_palette.slider_fill[state_index(state)] = value;
    }

    void Theme::set_slider_fill(Color value) {
        // the normal state is the common case, which is the shorthand (§8.4)
        m_palette.slider_fill[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_canvas_background(Color value) {
        // the normal state is the common case, which is the shorthand (§6.1)
        m_palette.canvas_background[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_canvas_background(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.canvas_background[state_index(state)] = value;
    }

    void Theme::set_focus_ring_width(float value) {
        // the width is one value because a ring strokes evenly on every side (§6.1)
        m_metrics.focus_ring_width = value;
    }

    void Theme::set_focus_ring_offset(float value) {
        // the offset is one value because the ring stands off a control evenly (§6.1)
        m_metrics.focus_ring_offset = value;
    }

    void Theme::set_focus_ring_radius(float value) {
        // the radius is one value because the ring follows the plate it surrounds (§6.2)
        m_metrics.focus_ring_radius = value;
    }

    void Theme::set_text_field_text(Color value) {
        // the normal state is the common case, which is the shorthand (§6.1)
        m_palette.text_field_text[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_text_field_text(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.text_field_text[state_index(state)] = value;
    }

    void Theme::set_text_field_placeholder(Color value) {
        // the normal state is the common case, which is the shorthand (§6.1)
        m_palette.text_field_placeholder[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_text_field_placeholder(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.text_field_placeholder[state_index(state)] = value;
    }

    void Theme::set_text_field_caret(Color value) {
        // the normal state is the common case, which is the shorthand (§6.1)
        m_palette.text_field_caret[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_text_field_caret(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.text_field_caret[state_index(state)] = value;
    }

    void Theme::set_text_field_plate(Color value) {
        // the normal state is the common case, which is the shorthand (§8.4)
        m_palette.text_field_plate[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_text_field_plate(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§8.4)
        m_palette.text_field_plate[state_index(state)] = value;
    }

    void Theme::set_text_field_border(Color value) {
        // the normal state is the common case, which is the shorthand (§8.4)
        m_palette.text_field_border[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_text_field_border(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§8.4)
        m_palette.text_field_border[state_index(state)] = value;
    }

    void Theme::set_text_field_selection(Color value) {
        // the normal state is the common case, which is the shorthand (§6.1)
        m_palette.text_field_selection[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_text_field_selection(ControlState state, Color value) {
        // the entry is written by its state index, which is what the resolver reads (§6.1)
        m_palette.text_field_selection[state_index(state)] = value;
    }

    void Theme::set_text_field_minimum(Size value) {
        // the extent is stored whole, because a field takes one minimum for both axes (§8.2)
        m_metrics.text_field_minimum = value;
    }

    void Theme::set_text_field_padding(float value) {
        // the inset is one value because a field inserts the same depth on both sides (§8.2)
        m_metrics.text_field_padding = value;
    }
} // namespace albor
