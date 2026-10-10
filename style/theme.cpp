#include "style/theme.h"

namespace albor
{
    const Palette& Theme::palette() const {
        // the colours are stored by value, so a caller reads the installed palette and not a copy (§6.1)
        return m_palette;
    }

    const ControlMetrics& Theme::metrics() const {
        // the sizing is stored by value, so a widget measures from the installed metrics (§8.2)
        return m_metrics;
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

    void Theme::set_checkbox_box(Size value) {
        // the extent is stored whole, because a check box is one square (§8.2)
        m_metrics.checkbox_box = value;
    }

    void Theme::set_checkbox_outline(float value) {
        // the outline is one width because the stroke has no per side override (§6.1)
        m_metrics.checkbox_outline = value;
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
} // namespace albor
