#include "albor/application/tooltip_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "albor/core/constraints.h"
#include "albor/text/text_engine.h"
#include "albor/ui/tooltip_view.h"
#include "albor/ui/ui_node.h"

namespace albor
{
    TooltipController::TooltipController(UiNode& root, TextEngine& text, TooltipStyle style) :
        m_root(root), m_text(text), m_style(style) {}

    TooltipController::~TooltipController() {
        // the overlay belongs to the root, so it is removed while the tree is still alive (§8.3)
        hide();
    }

    void TooltipController::set_target(UiNode* hovered) {
        // the nearest tooltipped node is what the pointer presents, which lets a container cover its children (§8.3)
        UiNode* target = nullptr;
        // the walk starts at the innermost node, because a leaf with its own tooltip wins over its parent (§8.3)
        for (UiNode* node = hovered; node != nullptr; node = node->parent()) {
            // the first node that carries content is the target, because nothing above it can be nearer (§8.3)
            if (node->has_tooltip()) {
                target = node;
                break;
            }
        }
        // a target a dismissal suppressed stays quiet until the pointer names another node (§8.3)
        if (target != nullptr && target == m_suppressed)
            return;
        // a different target clears the suppression, because the pointer moved on (§8.3)
        m_suppressed = nullptr;
        // an unchanged target keeps the timer it already runs, because a move inside it changes nothing (§8.3)
        if (target == m_target)
            return;
        // a target that is gone leaves the warm grace to expire, which is what the pointer left behind (§8.3)
        if (target == nullptr) {
            // a shown bubble leaves at once, because its target is no longer under the pointer (§8.3)
            if (m_phase == TooltipPhase::Visible) {
                hide();
                m_phase = TooltipPhase::Warmup;
                m_remaining = WARM_GRACE;
            }
            // an armed bubble stops arming, because nothing would ever show it (§8.3)
            else if (m_phase == TooltipPhase::Arming) {
                m_phase = TooltipPhase::Hidden;
                m_remaining = 0.0f;
            }
            // the target is cleared in either case, so no timer names a node the pointer left (§8.3)
            m_target = nullptr;
            return;
        }
        // a bubble already shown hides first, because the new target supplies its own text (§8.3)
        const bool warm = m_phase == TooltipPhase::Visible || m_phase == TooltipPhase::Warmup || (m_phase == TooltipPhase::Arming && m_remaining <= WARM_ARM_DELAY);
        // the old bubble leaves before the new one arms, so two targets never share one overlay (§8.3)
        hide();
        // a warm controller opens the next target quickly, which is the adjacent control rule (§8.3)
        arm(target, warm ? WARM_ARM_DELAY : ARM_DELAY);
    }

    void TooltipController::pointer_left() {
        // the pointer names nothing now, which hides the bubble and keeps the grace (§8.3)
        set_target(nullptr);
    }

    void TooltipController::dismiss() {
        // the target that was showing or arming is suppressed, so a click does not re-arm it (§8.3)
        if (m_target != nullptr)
            m_suppressed = m_target;
        // the bubble leaves at once, because the gesture asked for it (§8.3)
        hide();
        // every timer stops, because a dismissal is not a grace (§8.3)
        m_phase = TooltipPhase::Hidden;
        m_target = nullptr;
        m_remaining = 0.0f;
    }

    void TooltipController::dismiss_transient() {
        // the bubble leaves at once, because the window is no longer in front (§8.3)
        hide();
        // every timer stops, because a blurred window presents nothing (§8.3)
        m_phase = TooltipPhase::Hidden;
        m_target = nullptr;
        m_remaining = 0.0f;
        // the target is not suppressed, because the pointer never moved and a returning window may show it again (§8.3)
        m_suppressed = nullptr;
    }

    void TooltipController::advance(float delta_seconds) {
        // a target that left the tree or went hidden is not presented, because its text is not on screen (§8.3)
        if (m_target != nullptr && !m_target->visible_in_tree()) {
            // the bubble and the timers go together, because neither can name a hidden target (§8.3)
            hide();
            m_phase = TooltipPhase::Hidden;
            m_target = nullptr;
            m_remaining = 0.0f;
            return;
        }
        // a hidden controller runs no timer, which is what makes an idle window cheap (§8.3)
        if (m_phase == TooltipPhase::Hidden)
            return;
        // the phase counts down, because a frame is the only clock a tooltip needs (§8.3)
        m_remaining -= delta_seconds;
        // an armed target opens once its delay elapses, which is the half second or the warm tick (§8.3)
        if (m_phase == TooltipPhase::Arming && m_remaining <= 0.0f) {
            // a frame before the first arrange has no view to clamp against, so the timer waits for one (§8.2, §8.3)
            if (m_root.bounds().empty()) {
                m_remaining = WARM_ARM_DELAY;
                return;
            }
            // the bubble either presents or the target had nothing left to show (§8.3)
            if (present()) {
                m_phase = TooltipPhase::Visible;
                m_remaining = VISIBLE_TIMEOUT;
            }
            else {
                m_phase = TooltipPhase::Hidden;
                m_target = nullptr;
                m_remaining = 0.0f;
            }
            return;
        }
        // a visible bubble follows its target and rebuilds when the text changed beneath it (§8.3)
        if (m_phase == TooltipPhase::Visible) {
            // a text driven target rebuilds, while a rich builder owns its own content (§8.3)
            if (m_target != nullptr && !m_target->has_tooltip_builder() && m_presented_text != m_target->tooltip()) {
                // the new text arms with the warm delay, because the pointer never left the control (§8.3)
                hide();
                arm(m_target, WARM_ARM_DELAY);
                return;
            }
            // an arrange may have moved the target, so the bubble is placed again (§8.2, §8.3)
            replace_presentation();
            // the auto dismissal hides the bubble but keeps the warmth for the next target (§8.3)
            if (m_remaining <= 0.0f) {
                hide();
                m_phase = TooltipPhase::Warmup;
                m_target = nullptr;
                m_remaining = WARM_GRACE;
            }
            return;
        }
        // the grace only lasts as long as its timer, after which the next target is cold again (§8.3)
        if (m_phase == TooltipPhase::Warmup && m_remaining <= 0.0f) {
            m_phase = TooltipPhase::Hidden;
            m_remaining = 0.0f;
        }
    }

    void TooltipController::forget(UiNode* leaving) {
        // the presented overlay is removed by hide, so the notification it raises is not a target change (§8.3)
        if (leaving == nullptr || leaving == m_presented)
            return;
        // a timer that names the leaving subtree would read a destroyed node, so it is cleared (§8.3)
        if (m_target != nullptr && descends_from(m_target, leaving)) {
            // the bubble leaves first, because it presents the target that is going away (§8.3)
            hide();
            m_phase = TooltipPhase::Hidden;
            m_target = nullptr;
            m_remaining = 0.0f;
        }
        // a suppression that names the leaving subtree would too, so it is cleared as well (§8.3)
        if (m_suppressed != nullptr && descends_from(m_suppressed, leaving))
            m_suppressed = nullptr;
    }

    void TooltipController::set_style(TooltipStyle style) {
        // an unchanged style paints the same bubble, so no frame is requested (§8.4)
        if (m_style == style)
            return;
        // the style is stored first, so the next presentation reads the value it was given (§8.4)
        m_style = style;
        // a shown bubble repaints through the new style, because the theme may arrive after it opened (§8.4)
        if (auto* view = dynamic_cast<TooltipView*>(m_presented); view != nullptr)
            view->set_style(m_style);
    }

    TooltipPhase TooltipController::phase() const {
        // the phase decides what the window draws and what a test asserts (§8.3)
        return m_phase;
    }

    const UiNode* TooltipController::target() const {
        // the target is the node the timers name, which is null while hidden (§8.3)
        return m_target;
    }

    UiNode* TooltipController::presentation() const {
        // the presented overlay is the bubble the root paints, which is null while hidden (§8.3)
        return m_presented;
    }

    bool TooltipController::present() {
        // a target without content has nothing to present, so the timer settles as hidden (§8.3)
        if (m_target == nullptr || !m_target->has_tooltip())
            return false;
        // the view the bubble floats over is the root's arranged rect, which the frame pipeline settles (§8.2)
        const Rect viewport = m_root.bounds();
        // a tree that has not been arranged has no edges to clamp against, which the caller retries (§8.2)
        if (viewport.empty())
            return false;
        // the text wraps inside the widest content box the plate can hold, which keeps the tip inside the window (§8.3)
        const float content_width = std::max(1.0f, plate_width() - m_style.padding_horizontal * 2.0f);
        // a rich builder supplies its own subtree, which is measured like any node (§8.3)
        std::unique_ptr<UiNode> content = m_target->build_tooltip();
        // a builder that returned nothing leaves no content, unless the text still supplies a bubble (§8.3)
        if (!content && m_target->tooltip().empty())
            return false;
        // a text target falls back to the bubble, which the controller measures with the text engine (§8.3, §9.1)
        if (!content) {
            // the run wraps at the content width the plate leaves, which is the whole of the wrapping decision (§9.1)
            TextLayout layout = m_text.measure(m_target->tooltip(), content_width, m_style.font_size);
            // the bubble presents the run the engine shaped, so it needs no engine of its own (§8.3)
            content = std::make_unique<TooltipView>(m_target->tooltip(), std::move(layout), m_style);
        }
        // the content is measured against the plate it must fit, which is what places it (§8.2)
        Size measured{};
        content->measure(SizeConstraints::at_most(Size{.width = plate_width(), .height = viewport.height}), measured);
        // the rect flips and clamps against the target, which keeps the bubble readable at every edge (§8.3)
        const Rect rect = place_tooltip(m_target->bounds(), measured, viewport, m_style.gap, m_style.margin);
        // the content takes the rect, which is where the root paints it above every child (§8.2, §8.3)
        content->arrange(rect);
        // the presented overlay is kept raw, because the root owns it once it is adopted (§8.1)
        m_presented = content.get();
        // the text is kept, because a later change rebuilds the bubble (§8.3)
        m_presented_text = m_target->tooltip();
        // the root adopts the overlay last, so a failure leaves nothing half added (§8.3)
        m_root.add_overlay(std::move(content));
        return true;
    }

    void TooltipController::hide() {
        // a controller with no presented bubble has nothing to remove (§8.3)
        if (m_presented == nullptr)
            return;
        // the pointer is cleared first, because the removal notifies the tree before it moves the overlay (§8.3)
        UiNode* const presented = m_presented;
        m_presented = nullptr;
        // the presented text goes with the bubble, because the next presentation supplies its own (§8.3)
        m_presented_text.clear();
        // the root hands the overlay back, which the statement releases (§8.3)
        (void)m_root.remove_overlay(presented);
    }

    void TooltipController::replace_presentation() {
        // a hidden controller or a target that is gone has nothing to place (§8.3)
        if (m_presented == nullptr || m_target == nullptr)
            return;
        // the view may have no arranged rect after a resize, which the next frame supplies (§8.2)
        const Rect viewport = m_root.bounds();
        // a tree that lost its arrangement has no edges to clamp against, so the bubble waits (§8.2)
        if (viewport.empty())
            return;
        // the default bubble recomputes its own rect from the run it already carries (§8.3)
        if (auto* view = dynamic_cast<TooltipView*>(m_presented); view != nullptr) {
            view->place(m_target->bounds(), viewport);
            return;
        }
        // a rich bubble is measured again, because its content may depend on the space it was given (§8.2)
        const float max_width = plate_width();
        // the measure settles the extent the placement needs, which the root does not do for an overlay (§8.2)
        Size measured{};
        m_presented->measure(SizeConstraints::at_most(Size{.width = max_width, .height = viewport.height}), measured);
        // the rect flips and clamps exactly as the first presentation did (§8.3)
        const Rect rect = place_tooltip(m_target->bounds(), measured, viewport, m_style.gap, m_style.margin);
        // an unchanged rect needs no arrange, because the bubble already sits there (§8.2)
        if (rect == m_presented->bounds())
            return;
        // the old visual area includes the cast, which paints outside the plate (§6.1)
        const Rect previous_visual = m_presented->visual_bounds();
        // the rich subtree takes the rect its own measure answered (§8.2)
        m_presented->arrange(rect);
        // the move damages both visual extents, because the old shadow fringe is still on the surface (§6.1)
        m_presented->invalidate(previous_visual.unioned(m_presented->visual_bounds()));
    }

    float TooltipController::plate_width() const {
        // the viewport inset keeps the bubble off the edge, which the style's margin names (§8.3)
        const float available = m_root.bounds().width - m_style.margin * 2.0f;
        // the plate is bounded by the style ceiling and the space the view leaves (§8.2)
        return std::max(1.0f, std::min(m_style.max_width, available));
    }

    void TooltipController::arm(UiNode* target, float delay) {
        // the target and its delay replace the timer, because only one bubble is ever armed (§8.3)
        m_target = target;
        // the phase carries the delay, which the next frame counts down (§8.3)
        m_phase = TooltipPhase::Arming;
        // the delay is the warm tick or the half second the caller chose (§8.3)
        m_remaining = delay;
    }

    bool TooltipController::descends_from(const UiNode* node, const UiNode* ancestor) {
        // the walk starts at the node, because a target that is the leaving node itself is inside it (§8.3)
        for (const UiNode* current = node; current != nullptr; current = current->parent()) {
            // the identity decides, because a pointer into the subtree names the same object (§8.3)
            if (current == ancestor)
                return true;
        }
        // a node outside the leaving subtree keeps its timer, because nothing it names is going away (§8.3)
        return false;
    }
} // namespace albor
