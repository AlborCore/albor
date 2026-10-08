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

    void Theme::set_checkbox_fill(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.checkbox_fill[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_checkbox_outline(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.checkbox_outline[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_slider_track(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.slider_track[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_slider_thumb(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.slider_thumb[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_label_foreground(Color value) {
        // the normal state is the common case, so it gets its own entry point (§6.1)
        m_palette.label_foreground[state_index(ControlState::Normal)] = value;
    }

    void Theme::set_button_minimum(Size value) {
        // the extent is stored whole, because a control takes one minimum for both axes (§8.2)
        m_metrics.button_minimum = value;
    }

    void Theme::set_corner_radius(float value) {
        // the radius is one value because a plate has no per corner override in this first theme (§6.1)
        m_metrics.corner_radius = value;
    }
} // namespace albor
