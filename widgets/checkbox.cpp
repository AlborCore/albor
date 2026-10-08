#include <algorithm>

#include "widgets/checkbox.h"

namespace albor
{
    // the square a checkbox measures to, because the control has no intrinsic text (§8.2)
    constexpr float BOX_SIZE = 16.0f;

    // the width of the box outline, which is what makes an unchecked box visible (§6.1)
    constexpr float OUTLINE_WIDTH = 2.0f;

    CheckBox::CheckBox(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void CheckBox::measure(const SizeConstraints& constraints, Size& out_size) {
        // a checkbox is a fixed square, because its extent comes from the control metric rather than from content (§8.2)
        Size content{.width = BOX_SIZE, .height = BOX_SIZE};
        // the children are measured too, because a widget is a component and they are part of its content (§8.1, §8.2)
        Size children{};
        UiNode::measure(constraints, children);
        // a widget is at least as large as its own content and as its children (§8.2)
        content.width = std::max(content.width, children.width);
        content.height = std::max(content.height, children.height);
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void CheckBox::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void CheckBox::paint_content(RenderContext& context) {
        // a box with no area draws nothing, because there is no outline to stroke (§6.2)
        if (bounds().empty())
            return;
        // a checked box carries a fill, which is the only way the state reads without a glyph (§6.1)
        if (m_checked) {
            // the fill sits under the outline, so the border stays visible on top (§6.1)
            context.draw_rect(bounds(), {.fill = m_check_fill});
        }
        // the outline is stroked on top, because a control must read as a control (§6.1)
        context.draw_rect(bounds(), {.stroke = m_outline, .stroke_width = OUTLINE_WIDTH});
    }

    bool CheckBox::hit_test(Point logical_point) const {
        // a hidden box is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled box is not a hit, because it refuses input (§8.3)
        if (m_disabled)
            return false;
        // the press must land inside the arranged bounds, with the far edge excluded so neighbours stay separate (§8.3)
        const bool inside = logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height;
        // a press outside the box falls through to whatever is behind it (§8.3)
        return inside && !bounds().empty();
    }

    void CheckBox::dispatch_event(Event& event) {
        // a hidden box takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // a key that toggles the box is a press, which the focused box receives (§8.3)
        if (event.kind == EventKind::KeyDown && event.key == Key::Space) {
            // a disabled box does not claim the key, because it refuses input (§8.3)
            if (m_disabled)
                return;
            // a modified key is an application shortcut, so a plain toggle ignores it (§8.3)
            if (!event.modifiers.none())
                return;
            // the event is claimed, so no ancestor reacts to the same key (§8.3)
            event.handled = true;
            // the state flips before the handler runs, so the callback sees the new state (§8.3)
            m_checked = !m_checked;
            // a toggled box changes its fill, so the frame must be redrawn (§6.1)
            invalidate();
            // a box with no handler still toggles, because the state is the control (§8.3)
            if (!m_on_toggle)
                return;
            // the handler receives the new state, which is what a toggle reports (§8.3)
            m_on_toggle(m_checked);
            return;
        }
        // a press is the only other event that toggles a box (§8.3)
        if (event.kind != EventKind::PointerDown) {
            // any other event belongs to a parent, so the box does not claim it (§8.3)
            return;
        }
        // a disabled box does not claim a press, because it refuses input (§8.3)
        if (m_disabled)
            return;
        // a press outside the box belongs to whatever is behind it (§8.3)
        if (!hit_test(event.position))
            return;
        // the event is claimed, so no ancestor reacts to the same press (§8.3)
        event.handled = true;
        // the state flips before the handler runs, so the callback sees the state the box moved to (§8.3)
        m_checked = !m_checked;
        // a toggled box changes its fill, so the frame must be redrawn (§6.1)
        invalidate();
        // a box with no handler still toggles, because the state is the control (§8.3)
        if (!m_on_toggle)
            return;
        // the handler receives the new state, which is what a toggle reports (§8.3)
        m_on_toggle(m_checked);
    }

    SemanticNode CheckBox::semantic_node() const {
        // the role is a checkbox, because a press flips a boolean (§10.1)
        SemanticNode node{SemanticRole::Checkbox};
        // the accessible name is the identity the author assigned, because an unlabelled box has no text (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a disabled box reports the disabled state, which is what refuses activation (§10.1)
        node.set_disabled(m_disabled);
        // the check state mirrors the stored boolean, which is what a screen reader announces (§10.1)
        node.set_check(m_checked ? CheckState::Checked : CheckState::Unchecked);
        // the toggle action runs the same path a press does, and its identifier is discarded because a widget names actions by label (§10.1) the action body carries no disabled guard, because SemanticNode::invoke refuses every action while the node it belongs to is disabled, which is where that rule belongs (§10.1)
        (void)node.add_action("toggle", [control = mutable_owner(this)] {
            // the state flips before the handler runs, so the callback sees the state the box moved to (§8.3)
            control->m_checked = !control->m_checked;
            // a toggled box changes its fill, so the frame must be redrawn (§6.1)
            control->invalidate();
            // a box with no handler still toggles, because the state is the control (§8.3)
            if (!control->m_on_toggle)
                return;
            // the handler receives the new state, which is what a toggle reports (§8.3)
            control->m_on_toggle(control->m_checked);
        });
        return node;
    }

    void CheckBox::set_checked(bool checked) {
        // an unchanged state redraws nothing, because the box looks the same (§6.1)
        if (m_checked == checked)
            return;
        // the state is stored first, so the invalidation walks a box that already knows it is checked (§8.3)
        m_checked = checked;
        // a checked box changes its fill, so the frame must be redrawn (§6.1)
        invalidate();
    }

    bool CheckBox::checked() const {
        // the state is what the semantic check state and the render pass both read (§10.1)
        return m_checked;
    }

    void CheckBox::set_on_toggle(std::function<void(bool)> handler) {
        // the handler is swapped in place, because a press after this point must run the new work (§8.3)
        m_on_toggle = std::move(handler);
    }

    void CheckBox::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the box looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the invalidation walks a box that already refuses input (§8.3)
        m_disabled = disabled;
        // a disabled box stops accepting presses, so the frame changes (§6.1)
        invalidate();
    }

    bool CheckBox::disabled() const {
        // the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void CheckBox::set_outline(Color outline) {
        // an unchanged colour redraws nothing, because the box looks the same (§6.1)
        if (m_outline == outline)
            return;
        // the colour is stored first, so the invalidation walks a box that already strokes the new colour (§6.1)
        m_outline = outline;
        // an outline change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color CheckBox::outline() const {
        // the outline is what the render pass strokes (§6.1)
        return m_outline;
    }

    void CheckBox::set_check_fill(Color check_fill) {
        // an unchanged colour redraws nothing, because the box looks the same (§6.1)
        if (m_check_fill == check_fill)
            return;
        // the colour is stored first, so the invalidation walks a box that already fills the new colour (§6.1)
        m_check_fill = check_fill;
        // a fill change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }
} // namespace albor
