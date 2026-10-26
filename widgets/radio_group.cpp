#include "widgets/radio_group.h"

#include <algorithm>
#include <utility>

#include "widgets/radio_button.h"

namespace albor
{
    RadioGroup::RadioGroup(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_token(std::make_shared<CallbackToken>(CallbackToken{this})) {}

    RadioGroup& RadioGroup::add_child(std::unique_ptr<UiNode> child) {
        // the base adopts the child, which is where ownership moves (§8.1)
        Widget::add_child(std::move(child));
        // the new choice is compared with the selected value, so a value written before it is marked now (§8)
        apply_selection();
        // the group is handed back, because the base add_child returns the parent (§8.1)
        return *this;
    }

    void RadioGroup::sync_buttons() const {
        // a count match is only a fast path, because a same-count replacement or reorder changes the identity (§8.1)
        const std::size_t children = child_count();
        bool current = children == m_synced_children;
        if (current) {
            // the cached buttons are compared against the direct children, which catches every structural edit (§8.1)
            std::size_t cached = 0;
            for (std::size_t index = 0; index < children; index++) {
                // a child that is not a radio button is skipped, because only choices join the group (§8)
                auto* button = dynamic_cast<RadioButton*>(child_at(index));
                if (button == nullptr)
                    continue;
                // a pointer that differs means the list is stale, even when the count is unchanged (§8.1)
                if (cached >= m_buttons.size() || m_buttons[cached] != button) {
                    current = false;
                    break;
                }
                cached++;
            }
            // a list that ends short is stale too, because a choice was removed without changing the count (§8.1)
            if (current && cached != m_buttons.size())
                current = false;
        }
        // a current list is left alone, which keeps the steady frame allocation free (§8.1)
        if (current)
            return;
        // the list is rebuilt only when the pointer sequence changed, because a group holds a handful of choices (§8.1)
        m_buttons.clear();
        m_buttons.reserve(children);
        for (std::size_t index = 0; index < children; index++) {
            // the choice type decides, because a group only drives the controls it understands (§8)
            auto* button = dynamic_cast<RadioButton*>(child_at(index));
            if (button == nullptr)
                continue;
            // the button joins the layout order, which is the order the author wrote (§8.1)
            m_buttons.push_back(button);
            // a press selects the choice it belongs to, which is how the group owns the selection (§8.3)
            button->set_on_select([token = std::weak_ptr<CallbackToken>(m_token), button] {
                // the token expires with the group, so a detached choice cannot call into freed state (§8.3, AD-09)
                if (const std::shared_ptr<CallbackToken> state = token.lock(); state != nullptr && state->group != nullptr)
                    state->group->select_button(button);
            });
        }
        // the count is recorded after the walk, so the next frame can take the fast path (§8.1)
        m_synced_children = children;
    }

    void RadioGroup::apply_selection() const {
        // the list is current before the choices are marked (§8.1)
        sync_buttons();
        // only the first choice with the selected value is checked, because a value is one choice (§8)
        bool chosen = false;
        for (RadioButton* button : m_buttons) {
            // a matching value becomes the one checked choice, and a later duplicate stays unchecked (§8)
            const bool selected = !chosen && button->value() == m_selected;
            // the state is written on every choice, so a replaced button cannot keep a stale mark (§8)
            button->set_checked(selected);
            // the first match consumes the selection, which is what makes the group single-choice (§8)
            chosen = chosen || selected;
        }
    }

    void RadioGroup::measure(const SizeConstraints& constraints, Size& out_size) {
        // the choices are current before they are measured, because a choice added this frame has no size (§8.1)
        sync_buttons();
        // every choice measures its own row, which is what makes a label part of the target (§8.2)
        Size total{};
        for (std::size_t index = 0; index < m_buttons.size(); index++) {
            // each choice measures against the offered ceiling, which is the container contract (§8.2)
            Size child{};
            m_buttons[index]->measure(constraints, child);
            // a row grows along the width, while a column grows along the height (§8.2)
            if (m_vertical) {
                // a column is as wide as its widest choice and as tall as every choice stacked (§8.2)
                total.width = std::max(total.width, child.width);
                total.height += child.height;
            }
            else {
                // a row is as wide as every choice in a line and as tall as its tallest choice (§8.2)
                total.width += child.width;
                total.height = std::max(total.height, child.height);
            }
            // the gap sits between choices, so it is not added after the last one (§8.2)
            if (index + 1 < m_buttons.size()) {
                // the gap follows the main axis, which is what separates two choices (§8.2)
                total.width += m_vertical ? 0.0f : spacing();
                total.height += m_vertical ? spacing() : 0.0f;
            }
        }
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void RadioGroup::arrange(const Rect& bounds) {
        // the group takes the rectangle its parent assigned, which the choices are placed inside (§8.2)
        m_bounds = bounds;
        // the choices and their selected state are current before they are placed (§8.1, §8)
        sync_buttons();
        apply_selection();
        // the natural sizes are measured first, because a group that does not fit must divide its room (§8.2)
        std::vector<Size> sizes(m_buttons.size());
        for (std::size_t index = 0; index < m_buttons.size(); index++) {
            // each choice measures against the group, because a label wraps inside the room the group owns (§8.2)
            m_buttons[index]->measure(SizeConstraints::at_most(Size{.width = bounds.width, .height = bounds.height}), sizes[index]);
            // a choice never claims more than the group, because the parent decided the extent (§8.2)
            sizes[index].width = std::min(sizes[index].width, bounds.width);
            sizes[index].height = std::min(sizes[index].height, bounds.height);
        }
        // the gap total is removed before the room is divided, because the gaps sit between choices (§8.2)
        const float gap_total = m_buttons.empty() ? 0.0f : spacing() * static_cast<float>(m_buttons.size() - 1);
        // a horizontal row that does not fit divides its width equally, so no choice vanishes or falls outside (§8.2)
        if (!m_vertical) {
            float natural = gap_total;
            for (const Size& size : sizes)
                natural += size.width;
            if (natural > bounds.width) {
                const float each = m_buttons.empty() ? 0.0f : std::max(0.0f, (bounds.width - gap_total) / static_cast<float>(m_buttons.size()));
                for (Size& size : sizes)
                    size.width = each;
            }
        }
        // a vertical column that does not fit divides its height equally, so every choice keeps a row (§8.2)
        else {
            float natural = gap_total;
            for (const Size& size : sizes)
                natural += size.height;
            if (natural > bounds.height) {
                const float each = m_buttons.empty() ? 0.0f : std::max(0.0f, (bounds.height - gap_total) / static_cast<float>(m_buttons.size()));
                for (Size& size : sizes)
                    size.height = each;
            }
        }
        // the main axis advance is the width of a row or the height of a column (§8.2)
        float x = bounds.x;
        float y = bounds.y;
        for (std::size_t index = 0; index < m_buttons.size(); index++) {
            // the choice takes its divided extent, which keeps the label and circle inside the group (§8.2)
            m_buttons[index]->arrange(Rect::from_origin_size(Point{.x = x, .y = y}, sizes[index]));
            // the next choice starts after the gap, which is what separates the group (§8.2)
            x += m_vertical ? 0.0f : sizes[index].width + spacing();
            y += m_vertical ? sizes[index].height + spacing() : 0.0f;
        }
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    SemanticNode RadioGroup::semantic_node() const {
        // the choices are current before the group actions range over them (§8.1)
        sync_buttons();
        // a selected value written before the children is applied here, so the first published tree is accurate (§8)
        apply_selection();
        // the role is a radio group, because the group owns one choice among its children (§10.1)
        SemanticNode node{SemanticRole::RadioGroup};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the identity is the accessible name a group speaks (§10.1)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a group is not checkable, because the choice state belongs to its radio buttons (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the actions move the single selection, which is what automation can drive on a group (§10.1)
        (void)node.add_action("select_next", [control = mutable_owner(this)] { control->move_selection(1); });
        (void)node.add_action("select_previous", [control = mutable_owner(this)] { control->move_selection(-1); });
        return node;
    }

    std::size_t RadioGroup::semantic_child_count() const {
        // the choices are current before the count is reported (§8.1)
        sync_buttons();
        // only the radio buttons are semantic children, because content that is not a choice is ignored (§8)
        return m_buttons.size();
    }

    std::unique_ptr<SemanticProvider> RadioGroup::semantic_child(std::size_t index) const {
        // the choices are current before an index is resolved against them (§8.1)
        sync_buttons();
        // a selected value written before the children is applied before the choice is published (§8)
        apply_selection();
        // an index past the end names no choice, which the lazy walk expects (§10.1)
        if (index >= m_buttons.size())
            return nullptr;
        // the choice is watched rather than copied, so the ui tree stays the only owner (§10.1, AD-09)
        return std::make_unique<SemanticProviderView>(m_buttons[index]);
    }

    bool RadioGroup::accepts_focus() const {
        // the group is a container, so the radio buttons take the caret rather than the group (§8.3)
        return false;
    }

    void RadioGroup::set_selected(int value) {
        // the choices are current before the selected value is applied (§8.1)
        sync_buttons();
        // the value is read before the write, because the signal reports an actual selection move (§8.3)
        const int previous = m_selected;
        // the value is stored first, so the redraw walks a group that already carries it (§8)
        m_selected = value;
        // the choice with the value becomes the one checked choice (§8)
        apply_selection();
        // an unchanged value reports nothing, because the group looks the same (§8.3)
        if (previous == value)
            return;
        // the checked choice changed, so the frame needs a paint (§6.1)
        invalidate();
        // the installed work is told, which is what the selection signal reports (§8.3)
        if (m_on_selection_changed)
            m_on_selection_changed(value);
    }

    int RadioGroup::selected() const {
        // the choices are current before the selected value is reported (§8.1)
        sync_buttons();
        // the selected value marks its choice even before a layout pass, which a read is allowed to do (§8)
        apply_selection();
        // the value is what the group stores, which is the value an author binds (§8)
        return m_selected;
    }

    void RadioGroup::set_vertical(bool vertical) {
        // an unchanged orientation redraws nothing, because the choices sit in the same order (§6.1)
        if (m_vertical == vertical)
            return;
        // the orientation is stored first, so the invalidation walks a group that already carries it (§8)
        m_vertical = vertical;
        // the choices moved, so the layout passes run (§8.2)
        invalidate_layout();
    }

    bool RadioGroup::vertical() const {
        // the orientation is what the layout reads (§8.2)
        return m_vertical;
    }

    void RadioGroup::set_spacing(float spacing) {
        // a negative gap would overlap choices, so the value is clamped at zero (§8.2)
        const float clamped = std::max(0.0f, spacing);
        // the written gap is compared against the gap in force, which may be the theme default (§8.2)
        const bool changed = this->spacing() != clamped;
        // the gap is stored and marked written, because the next layout reads it (§8.2)
        m_spacing = clamped;
        m_spacing_explicit = true;
        // an unchanged gap redraws nothing, because the choices sit in the same place (§8.2)
        if (!changed)
            return;
        // the choices moved, so the layout passes run (§8.2)
        invalidate_layout();
    }

    float RadioGroup::spacing() const {
        // an unwritten gap follows the installed theme, because the preset owns the compact directions (§8.4)
        if (!m_spacing_explicit && theme() != nullptr)
            return theme()->metrics().radio_group_spacing;
        // the stored gap is what the layout reads when the author wrote one or no theme is installed (§8.2)
        return m_spacing;
    }

    void RadioGroup::set_on_selection_changed(std::function<void(int)> handler) {
        // the work is stored, which the next actual selection move runs (§8.3)
        m_on_selection_changed = std::move(handler);
    }

    void RadioGroup::select_button(RadioButton* button) {
        // a choice that is not in the group cannot select it, which a detached button is (§8.3)
        if (button == nullptr || button->parent() != this)
            return;
        // the choices are current before the pressed one is looked up, because a rebuild may have replaced it (§8.1)
        sync_buttons();
        // a button the current choice list does not hold cannot select the group (§8.3)
        if (std::find(m_buttons.begin(), m_buttons.end(), button) == m_buttons.end())
            return;
        // the value is the identity the group owns, which a press selects (§8)
        set_selected(button->value());
    }

    void RadioGroup::move_selection(int direction) {
        // the choices are current before a move is resolved against them (§8.1)
        sync_buttons();
        // an empty group has nothing to move, which a semantic action then ignores (§8)
        if (m_buttons.empty())
            return;
        // the current position is found by value, because the author-facing selection is a value (§8)
        std::size_t position = 0;
        bool found = false;
        for (std::size_t index = 0; index < m_buttons.size(); index++) {
            // the selected value names the position a move starts from (§8)
            if (m_buttons[index]->value() == m_selected) {
                position = index;
                found = true;
                break;
            }
        }
        // a choice the user cannot press is skipped, because an arrow follows the same rules as a press (§8.3)
        const auto enabled = [this](std::size_t index) { return !m_buttons[index]->disabled(); };
        // the candidate is the next enabled choice in the direction, or nothing when the end is reached (§8)
        std::size_t candidate = 0;
        bool has_candidate = false;
        if (found) {
            // a selected position moves one place, and a disabled place is passed over (§8.3)
            if (direction > 0) {
                for (std::size_t index = position + 1; index < m_buttons.size(); index++) {
                    if (!enabled(index))
                        continue;
                    candidate = index;
                    has_candidate = true;
                    break;
                }
            }
            else {
                for (std::size_t index = position; index > 0; index--) {
                    if (!enabled(index - 1))
                        continue;
                    candidate = index - 1;
                    has_candidate = true;
                    break;
                }
            }
        }
        else {
            // a group with no selected choice starts at the first or last enabled choice (§8.3)
            if (direction > 0) {
                for (std::size_t index = 0; index < m_buttons.size(); index++) {
                    if (!enabled(index))
                        continue;
                    candidate = index;
                    has_candidate = true;
                    break;
                }
            }
            else {
                for (std::size_t index = m_buttons.size(); index > 0; index--) {
                    if (!enabled(index - 1))
                        continue;
                    candidate = index - 1;
                    has_candidate = true;
                    break;
                }
            }
        }
        // an end with no enabled choice changes nothing, because a group has no wrap-around (§8)
        if (!has_candidate)
            return;
        // the choice at the candidate becomes the selected value (§8)
        set_selected(m_buttons[candidate]->value());
    }
} // namespace albor
