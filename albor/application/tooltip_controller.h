#pragma once

#include <string>

#include "albor/core/geometry.h"
#include "albor/ui/tooltip.h"

namespace albor
{
    class TextEngine;
    class UiNode;

    // the phase a hover tooltip is in, which the window advances on every frame (§8.3)
    enum class TooltipPhase
    {
        // no target and no warmth, so nothing is armed
        Hidden,

        // a target is hovered and the opening delay is running
        Arming,

        // the bubble is presented and the auto dismissal is running
        Visible,

        // the bubble is hidden and an adjacent target still opens quickly
        Warmup
    };

    // the window level tooltip lifecycle, which arms on hover and presents a bubble over the root (§8.3)
    class TooltipController
    {
    public:
        // the delay a cold target waits, which is the conventional half second (§8.3)
        static constexpr float ARM_DELAY = 0.5f;

        // the delay an adjacent target waits once a tooltip was shown (§8.3)
        static constexpr float WARM_ARM_DELAY = 0.05f;

        // how long a shown tooltip stays before it dismisses itself (§8.3)
        static constexpr float VISIBLE_TIMEOUT = 10.0f;

        // how long the warm state survives after the pointer leaves the target (§8.3)
        static constexpr float WARM_GRACE = 0.25f;

        // build a controller over the root it presents on and the engine it measures with (§8.3)
        TooltipController(UiNode& root, TextEngine& text, TooltipStyle style = {});

        // the controller owns the overlay it added, so it removes it before the tree unwinds (§8.3)
        ~TooltipController();

        TooltipController(const TooltipController&) = delete;
        TooltipController& operator=(const TooltipController&) = delete;
        TooltipController(TooltipController&&) = delete;
        TooltipController& operator=(TooltipController&&) = delete;

        // point the controller at the innermost hovered node, which resolves the nearest tooltip target (§8.3)
        void set_target(UiNode* hovered);

        // end the pointer's stay, which hides the bubble and keeps the warm state for the grace (§8.3)
        void pointer_left();

        // dismiss the bubble at once, which a press, a key or a wheel asks for (§8.3)
        void dismiss();

        // dismiss the bubble without suppressing the target, which a blur asks for because the pointer did not move (§8.3)
        void dismiss_transient();

        // advance the timers by one frame, which presents or dismisses the bubble it is due (§8.3)
        void advance(float delta_seconds);

        // forget a subtree that is leaving, so no timer holds a node about to die (§8.3)
        void forget(UiNode* leaving);

        // replace the appearance a later bubble paints through (§8.4)
        void set_style(TooltipStyle style);

        // hand out the phase the controller is in, which a test asserts on (§8.3)
        [[nodiscard]] TooltipPhase phase() const;

        // hand out the node the controller armed on, which is null while it is hidden (§8.3)
        [[nodiscard]] const UiNode* target() const;

        // hand out the overlay the controller presented, which is null while it is hidden (§8.3)
        [[nodiscard]] UiNode* presentation() const;

    private:
        // show the bubble for the armed target, reporting whether the overlay was added (§8.3)
        bool present();

        // hand out the widest plate the view can hold, which the style ceiling and the viewport inset bound (§8.2)
        [[nodiscard]] float plate_width() const;

        // remove the overlay the controller presented, which leaves the tree as the author built it (§8.3)
        void hide();

        // place the presented bubble and the armed target beside each other again (§8.3)
        void replace_presentation();

        // arm the given target with the delay the current warmth gives it (§8.3)
        void arm(UiNode* target, float delay);

        // report whether a node descends from the one that is leaving, which clears the timers (§8.3)
        [[nodiscard]] static bool descends_from(const UiNode* node, const UiNode* ancestor);

        // the root the bubble is presented over, which the stage owns (§8.1)
        UiNode& m_root;

        // the engine the tooltip text is measured with, which the application owns (§9.1)
        TextEngine& m_text;

        // the appearance a bubble paints through (§8.4)
        TooltipStyle m_style;

        // the phase the controller is in (§8.3)
        TooltipPhase m_phase = TooltipPhase::Hidden;

        // the node the controller armed on, which is null while hidden (§8.3)
        UiNode* m_target = nullptr;

        // the target a dismissal suppressed, which stays dismissed until the pointer moves (§8.3)
        UiNode* m_suppressed = nullptr;

        // the overlay the controller presented, which the root owns while it is attached (§8.3)
        UiNode* m_presented = nullptr;

        // the text the presented bubble carries, which a changed target text rebuilds (§8.3)
        std::string m_presented_text;

        // the seconds left before the current phase changes (§8.3)
        float m_remaining = 0.0f;
    };
} // namespace albor
