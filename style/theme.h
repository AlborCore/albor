#pragma once

#include <cstddef>

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
        Disabled
    };

    // the sizing a control takes from the theme, so the metrics are one decision rather than a literal per widget (§8.2)
    struct ControlMetrics
    {
        // the smallest extent a push control measures to, because it has to be grabbable (§8.2)
        Size button_minimum{24.0f, 16.0f};

        // the extent of a check box, which is a fixed square (§8.2)
        Size checkbox_box{16.0f, 16.0f};

        // the width of a check box outline, which is what makes an unchecked box visible (§6.1)
        float checkbox_outline = 2.0f;

        // the extent a slider measures to, because a track is thin (§8.2)
        Size slider_extent{120.0f, 16.0f};

        // the thickness of a slider track, which is drawn centred on the control (§6.1)
        float slider_track_thickness = 4.0f;

        // the extent of a slider thumb along the track, which is what the user grabs (§6.1)
        float slider_thumb = 12.0f;

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
    };

    // the colours a theme resolves by role, which is what lets a widget ask instead of carry a literal (§6.1)
    struct Palette
    {
        // the plate of a control, per state, which is the fill behind its content (§6.1)
        Color button_plate[4] = {Color{0.20f, 0.55f, 0.90f, 1.0f}, Color{0.26f, 0.62f, 0.96f, 1.0f}, Color{0.14f, 0.42f, 0.74f, 1.0f}, Color{0.42f, 0.46f, 0.52f, 1.0f}};

        // the label colour of a control, per state, which is what the disabled variant fades (§6.1)
        Color button_label[4] = {Color{0.96f, 0.97f, 0.99f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, Color{0.88f, 0.90f, 0.94f, 1.0f}, Color{0.72f, 0.74f, 0.78f, 1.0f}};

        // the fill of a check box, per state, which is the box itself (§6.1)
        Color checkbox_fill[4] = {Color{0.20f, 0.55f, 0.90f, 1.0f}, Color{0.26f, 0.62f, 0.96f, 1.0f}, Color{0.14f, 0.42f, 0.74f, 1.0f}, Color{0.62f, 0.66f, 0.72f, 1.0f}};

        // the outline of a check box, per state, which is what makes the empty box visible (§6.1)
        Color checkbox_outline[4] = {Color{0.45f, 0.50f, 0.58f, 1.0f}, Color{0.60f, 0.66f, 0.74f, 1.0f}, Color{0.34f, 0.40f, 0.48f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}};

        // the track of a slider, per state, which is the line the thumb rides (§6.1)
        Color slider_track[4] = {Color{0.24f, 0.27f, 0.32f, 1.0f}, Color{0.28f, 0.32f, 0.38f, 1.0f}, Color{0.20f, 0.23f, 0.28f, 1.0f}, Color{0.40f, 0.42f, 0.46f, 1.0f}};

        // the thumb of a slider, per state, which is what the user grabs (§6.1)
        Color slider_thumb[4] = {Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, Color{0.78f, 0.82f, 0.88f, 1.0f}, Color{0.66f, 0.68f, 0.72f, 1.0f}};

        // the text colour of a plain label, per state, which a label has no press of its own (§6.1)
        Color label_foreground[4] = {Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{0.96f, 0.97f, 0.99f, 1.0f}, Color{0.92f, 0.94f, 0.96f, 1.0f}, Color{0.58f, 0.60f, 0.64f, 1.0f}};

        // the groove a scrollbar thumb rides in, per state, which is what shows the travel (§6.1)
        Color scrollbar_track[4] = {Color{0.16f, 0.18f, 0.22f, 0.0f}, Color{0.18f, 0.20f, 0.24f, 0.0f}, Color{0.16f, 0.18f, 0.22f, 0.0f}, Color{0.24f, 0.26f, 0.30f, 0.0f}};

        // the thumb of a scrollbar, per state, which is what the user drags (§6.1)
        Color scrollbar_thumb[4] = {Color{0.42f, 0.46f, 0.52f, 1.0f}, Color{0.58f, 0.62f, 0.68f, 1.0f}, Color{0.72f, 0.76f, 0.82f, 1.0f}, Color{0.28f, 0.30f, 0.34f, 1.0f}};
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

        // replace the track of the normal state of a slider (§6.1)
        void set_slider_track(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_slider_track(ControlState state, Color value);

        // replace the thumb of the normal state of a slider (§6.1)
        void set_slider_thumb(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_slider_thumb(ControlState state, Color value);

        // replace the text colour of the normal state of a label (§6.1)
        void set_label_foreground(Color value);

        // replace one state of the entry, which is what a derived palette writes (§6.1)
        void set_label_foreground(ControlState state, Color value);

        // replace the smallest push control extent, which is a sizing decision rather than a colour (§8.2)
        void set_button_minimum(Size value);

        // replace the corner radius a plate is drawn with (§6.1)
        void set_corner_radius(float value);

        // hand out the surface colour, which a frame background resolves through (§6.1)
        [[nodiscard]] Color surface() const;

        // replace the surface colour, which is what a derived palette sets (§6.1)
        void set_surface(Color value);

        // replace the extent a check box measures to, which the control reads instead of a literal (§8.2)
        void set_checkbox_box(Size value);

        // replace the width a check box outline strokes at (§6.1)
        void set_checkbox_outline(float value);

        // replace the extent a slider measures to (§8.2)
        void set_slider_extent(Size value);

        // replace the thickness a slider track is drawn at (§6.1)
        void set_slider_track_thickness(float value);

        // replace the extent of a slider thumb, which is what the user grabs (§6.1)
        void set_slider_thumb(float value);

        // replace the amount one key press moves a slider value (§10.1)
        void set_slider_step(float value);

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

    private:
        // the colours, which the overrides above write (§6.1)
        Palette m_palette;

        // the sizing, which the overrides above write (§8.2)
        ControlMetrics m_metrics;

        // the surface colour, which a frame background resolves through (§6.1)
        Color m_surface{0.07f, 0.08f, 0.10f, 1.0f};
    };

    // the index a state maps to, because the palette stores one colour per state in declaration order (§6.1)
    [[nodiscard]] constexpr std::size_t state_index(ControlState state) { return static_cast<std::size_t>(state); }
} // namespace albor
