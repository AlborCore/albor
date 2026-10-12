#include "style/theme_builder.h"

namespace albor
{
    Color ThemeBuilder::lighten(Color value, float amount) {
        // each channel moves toward one by the amount, which is what a hover reads as (§6.1)
        return Color::rgba(value.red + (1.0f - value.red) * amount, value.green + (1.0f - value.green) * amount, value.blue + (1.0f - value.blue) * amount, value.alpha);
    }

    Color ThemeBuilder::darken(Color value, float amount) {
        // each channel moves toward zero by the amount, which is what a press reads as (§6.1)
        return Color::rgba(value.red * (1.0f - amount), value.green * (1.0f - amount), value.blue * (1.0f - amount), value.alpha);
    }

    Theme ThemeBuilder::build(const Accents& accents, ColorScheme scheme) {
        // the theme starts from the defaults, so an untouched role keeps a sane value (§6.1)
        Theme theme;
        // the surface is the one the accents chose, because everything else is read against it (§6.1)
        const Color surface = accents.surface;
        // the text is the one the accents chose, because it is the value with the most contrast (§6.1)
        const Color on_surface = accents.on_surface;
        // the accent is the one the accents chose, because it is where the states come from (§6.1)
        const Color accent = accents.accent;
        // the outline is the one the accents chose, because it reads as a line rather than a plate (§6.1)
        const Color outline = accents.outline;

        // on a dark surface a hover lifts toward the text colour and a press sinks (§6.1)
        const Color plate_hovered = scheme == ColorScheme::Dark ? lighten(accent, 0.12f) : lighten(accent, 0.08f);
        // the press always sinks, because pressing in is the direction the pointer reads (§6.1)
        const Color plate_pressed = darken(accent, 0.25f);
        // the disabled plate is the accent at half strength, which is the fade the button used to do inline (§6.1)
        const Color plate_disabled = Color::rgba(accent.red, accent.green, accent.blue, 0.45f);

        // the four states of a plate are one decision rather than four literals (§6.1)
        theme.set_button_plate(ControlState::Normal, accent);
        theme.set_button_plate(ControlState::Hovered, plate_hovered);
        theme.set_button_plate(ControlState::Pressed, plate_pressed);
        theme.set_button_plate(ControlState::Disabled, plate_disabled);
        // the label sits on the plate, so it is the text colour at four strengths (§6.1)
        theme.set_button_label(ControlState::Normal, on_surface);
        theme.set_button_label(ControlState::Hovered, on_surface);
        theme.set_button_label(ControlState::Pressed, on_surface);
        theme.set_button_label(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.5f));

        // a check box plate is the accent at four states, which is the same decision as a button (§6.1)
        theme.set_checkbox_fill(ControlState::Normal, accent);
        theme.set_checkbox_fill(ControlState::Hovered, plate_hovered);
        theme.set_checkbox_fill(ControlState::Pressed, plate_pressed);
        theme.set_checkbox_fill(ControlState::Disabled, plate_disabled);
        // an outline reads as a line, so its hover lifts and its press sinks (§6.1)
        theme.set_checkbox_outline(ControlState::Normal, outline);
        theme.set_checkbox_outline(ControlState::Hovered, lighten(outline, 0.12f));
        theme.set_checkbox_outline(ControlState::Pressed, darken(outline, 0.25f));
        theme.set_checkbox_outline(ControlState::Disabled, Color::rgba(outline.red, outline.green, outline.blue, 0.5f));

        // a track is a low contrast surface, so it is the accent sunk toward the surface (§6.1)
        const Color track = scheme == ColorScheme::Dark ? lighten(accent, 0.05f) : darken(outline, 0.30f);
        theme.set_slider_track(ControlState::Normal, track);
        theme.set_slider_track(ControlState::Hovered, lighten(track, 0.08f));
        theme.set_slider_track(ControlState::Pressed, darken(track, 0.15f));
        theme.set_slider_track(ControlState::Disabled, Color::rgba(track.red, track.green, track.blue, 0.5f));
        // the thumb is what the user grabs, so it is the strongest value on the control (§6.1)
        theme.set_slider_thumb(ControlState::Normal, on_surface);
        theme.set_slider_thumb(ControlState::Hovered, on_surface);
        theme.set_slider_thumb(ControlState::Pressed, lighten(on_surface, 0.15f));
        theme.set_slider_thumb(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.45f));

        // a plain label has no press of its own, so only its disabled variant differs (§6.1)
        theme.set_label_foreground(ControlState::Normal, on_surface);
        theme.set_label_foreground(ControlState::Hovered, on_surface);
        theme.set_label_foreground(ControlState::Pressed, on_surface);
        theme.set_label_foreground(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.45f));

        // an editable field draws the surface text, which is the value the contrast is chosen for (§6.1)
        theme.set_text_field_text(ControlState::Normal, on_surface);
        theme.set_text_field_text(ControlState::Hovered, on_surface);
        theme.set_text_field_text(ControlState::Pressed, on_surface);
        theme.set_text_field_text(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.45f));
        // a placeholder sits de-emphasised, so it draws the text colour at half strength (§6.1)
        theme.set_text_field_placeholder(ControlState::Normal, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.55f));
        theme.set_text_field_placeholder(ControlState::Hovered, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.62f));
        theme.set_text_field_placeholder(ControlState::Pressed, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.55f));
        theme.set_text_field_placeholder(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.35f));
        // a caret reads as a bar of the text itself, so it follows the same family (§6.1)
        theme.set_text_field_caret(ControlState::Normal, on_surface);
        theme.set_text_field_caret(ControlState::Hovered, lighten(on_surface, 0.1f));
        theme.set_text_field_caret(ControlState::Pressed, on_surface);
        theme.set_text_field_caret(ControlState::Disabled, Color::rgba(on_surface.red, on_surface.green, on_surface.blue, 0.4f));
        // a selection sits behind the value, so it is the accent at a strength that leaves the text readable (§6.1)
        theme.set_text_field_selection(ControlState::Normal, Color::rgba(accent.red, accent.green, accent.blue, 0.35f));
        theme.set_text_field_selection(ControlState::Hovered, Color::rgba(accent.red, accent.green, accent.blue, 0.42f));
        theme.set_text_field_selection(ControlState::Pressed, Color::rgba(accent.red, accent.green, accent.blue, 0.40f));
        theme.set_text_field_selection(ControlState::Disabled, Color::rgba(accent.red, accent.green, accent.blue, 0.30f));

        // the surface is offered so an application can read it back, which a frame background needs (§6.1)
        theme.set_surface(surface);
        // exit
        return theme;
    }

    Theme ThemeBuilder::light(const Accents& accents) {
        // a light theme is the same derivation with the light scheme (§6.1)
        return build(accents, ColorScheme::Light);
    }

    Theme ThemeBuilder::dark(const Accents& accents) {
        // a dark theme is the same derivation with the dark scheme (§6.1)
        return build(accents, ColorScheme::Dark);
    }
} // namespace albor
