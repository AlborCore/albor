#include "albor/widgets/scroll_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "albor/commands/command.h"
#include "albor/core/self_scrolling_content.h"
#include "albor/widgets/scroll_thumb.h"
#include "albor/widgets/scroll_wheel.h"

namespace albor
{
    ScrollView::ScrollView(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)),
        // the offset registers with the engine, which scopes its identifier (§7.2)
        m_offset(0.0f) {
        // the property needs an identifier before a binding can key on it (§7.2)
        register_property(m_offset);
    }

    void ScrollView::measure(const SizeConstraints& constraints, Size& out_size) {
        // an unbounded parent has no finite offer, so its preferred extent is the finite fallback (§8.2)
        const float offered_width = std::isfinite(constraints.maximum.width) ? constraints.maximum.width : (std::isfinite(constraints.preferred.width) ? constraints.preferred.width : 0.0f);
        // the height follows the same fallback, because an undeclared axis must stay finite (§8.2)
        const float offered_height = std::isfinite(constraints.maximum.height) ? constraints.maximum.height : (std::isfinite(constraints.preferred.height) ? constraints.preferred.height : 0.0f);
        // each axis is resolved on its own, because an author may pin one axis and let the other follow the window (§8.2)
        const float width = std::isfinite(m_viewport_extent.width) && m_viewport_extent.width > 0.0f ? m_viewport_extent.width : offered_width;
        // an undeclared height keeps the offered ceiling, so a fixed-width view grows with its parent (§8.2)
        const float height = std::isfinite(m_viewport_extent.height) && m_viewport_extent.height > 0.0f ? m_viewport_extent.height : offered_height;
        // the resolved pair is clamped into the offer, because the parent's decision is final (§8.2)
        const Size viewport = constraints.constrain(Size{.width = width, .height = height});
        // the measured size is the viewport, because a scroll container never grows with the content behind it (§8.2)
        m_measured_size = viewport;
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        // the result is what the parent places, which is the viewport rather than the content (§8.2)
        out_size = m_measured_size;
        // the row height reaches the content first, because the extent below is arithmetic over it (§8.2, AD-13)
        apply_row_extent();
        // an always on bar takes its gutter before the content is measured, because the policy owns the decision (§8)
        const bool forced = m_visibility == ScrollbarVisibility::AlwaysOn;
        // an overlay bar takes no gutter, so the content keeps the whole viewport at every policy (§8.2)
        const bool overlay = bar_overlay();
        // the reserved width is what the content keeps, which is what keeps it out of the gutter (§8.2)
        const float content_width = std::max(0.0f, viewport.width - bar_extent());
        // the content extent is measured against the width the policy leaves it, which an overlay leaves whole (§8.2)
        Size measured_extent = measure_content(forced && !overlay ? content_width : viewport.width, viewport.height);
        // the as needed policy draws a bar while content overflows a bar wide enough to draw (§8)
        const bool needed = forced || (m_visibility == ScrollbarVisibility::AsNeeded && bar_extent() > 0.0f && measured_extent.height > viewport.height);
        // a needed gutter narrows the second measure, because wrapping and height follow the visible width (§8.2)
        if (needed && !forced && !overlay)
            measured_extent = measure_content(content_width, viewport.height);
        // the decision is stored, because the clip and the pushed window both read the same one (§8.2)
        m_bar_needed = needed;
        // the scrollable contract reports the extent of the whole content rather than the built part (§8.2, AD-13)
        m_content_extent = measured_extent;
        // a content that changed hands the offset back to its binding, which is what a pinned log view needs (§7.3, AD-24)
        if (m_content_extent != m_settled_content_extent) {
            // the settled extent is remembered, so the next content change is recognised (§7.3)
            m_settled_content_extent = m_content_extent;
            // a user scroll detached the binding, and new content is what asks the binding to speak again (§7.3, AD-24)
            if (m_offset.overridden())
                m_offset.rebind();
        }
    }

    void ScrollView::arrange(const Rect& bounds) {
        // a declared axis keeps its extent even when the parent stretches the assigned bounds (§8.2)
        const float width = m_viewport_extent.width > 0.0f ? std::min(m_viewport_extent.width, bounds.width) : bounds.width;
        // an undeclared height takes the assigned bounds, which is how a fixed-width view follows its parent (§8.2)
        const float height = m_viewport_extent.height > 0.0f ? std::min(m_viewport_extent.height, bounds.height) : bounds.height;
        // the resolved extent is what the content viewport, the bar and the scroll range derive from (§8.2)
        m_bounds = Rect::from_origin_size(Point{.x = bounds.x, .y = bounds.y}, Size{.width = width, .height = height});
        // the window reaches the content before it is placed, so its rows exist in time (§8.2, AD-13)
        push_viewport();
        // the content viewport is the arranged extent less whatever gutter the policy reserves (§8.2)
        const Rect viewport = content_viewport();
        // a vertical scroll crosses its content horizontally, so the alignment rule is what places it (§8.2)
        for (const auto& child : children()) {
            // an unset alignment resolves to stretch, which is what fills the viewport rather than the widest row (§8.2)
            const Align horizontal = resolve_align(child->horizontal_align(), Align::Stretch);
            // a stretch takes the whole content viewport, while an authored alignment keeps its measured width (§8.2)
            const float width = horizontal == Align::Stretch ? viewport.width : std::min(m_content_extent.width, viewport.width);
            // the leading edge is the default position, which is where start and stretch both put the content (§8.2)
            float x = viewport.x;
            // a centred content splits the leftover width across both edges (§8.2)
            if (horizontal == Align::Center)
                x += (viewport.width - width) * 0.5f;
            // an end aligned content meets the trailing edge, which is the leftover it did not take (§8.2)
            else if (horizontal == Align::End)
                x += viewport.width - width;
            // the content sits at the offset and the resolved position, which is what moves it (§8.2)
            const Rect content = Rect::from_origin_size(Point{.x = x, .y = viewport.y - offset()}, Size{.width = width, .height = m_content_extent.height});
            // the content child is placed at the offset, which is what the viewport scrolls (§8.2)
            arrange_if_changed(*child, content);
        }
        // the bar follows the content, so its strip is damaged by the same arrange (§6.1)
        if (!m_bounds.empty() && scrollbar_visible())
            note_damage(scrollbar_track_rect());
    }

    Rect ScrollView::visual_bounds() const {
        // a hidden view paints nothing, so it contributes no damage (§6.1)
        if (!visible())
            return Rect{};
        // the view paints its plate, border and cast, while the clipped content is not part of it (§6.1, §6.2)
        Rect visual = own_bounds();
        // the focus ring and its halo paint outside the plate, so they join the damage the view reports (§6.1)
        return visual.unioned(ring_bounds());
    }

    Rect ScrollView::clip_child_damage(const Rect& damage) const {
        // the content is painted inside the content viewport, so a scrolled away extent damages nothing (§6.1, §6.2)
        return damage.intersect(content_viewport());
    }

    void ScrollView::paint_children(RenderContext& context) {
        // a view with no area clips nothing, because there is no viewport to clip to (§6.2)
        if (m_bounds.empty())
            return;
        // the clip is pushed so the content can be scissored to the content viewport (§6.2)
        context.save();
        // the content viewport is the clip, which is what keeps scrolled content inside the surface (§6.2)
        context.clip_rect(content_viewport());
        // the content records inside the clip, in insertion order (§6.1)
        render_children(context);
        // the clip is popped, so the bar below is not scissored to it (§6.2)
        context.restore();
        // the bar draws after the content, in its gutter or over it, because an overlay is not clipped by it (§6.1)
        paint_scrollbar(context);
    }

    void ScrollView::dispatch_event(Event& event) {
        // a hidden view takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run first, so the bar knows the state it paints (§8.3)
        handle_hover_transitions(event);
        // a wheel turns the content, which is what the viewport exists for (§8.3)
        if (event.kind == EventKind::Wheel) {
            // a disabled view does not claim the wheel, because it refuses input (§8.3)
            if (disabled())
                return;
            // a positive wheel turns toward the start, so it subtracts from the offset (§8.3)
            const float line = theme() != nullptr ? theme()->metrics().scroll_line_extent : SCROLL_LINE_EXTENT;
            const bool moved = scroll_by(-event.scroll * wheel_scale(event, line));
            // a notch that found an edge stays unclaimed, so an enclosing viewport takes it (§8.3)
            event.handled = moved;
            return;
        }
        // a move that continues a thumb drag steers the offset, which is what dragging the bar means (§8.3)
        if (event.kind == EventKind::PointerMove && m_dragging_thumb) {
            // the event is claimed, so no ancestor reacts to the same move (§8.3)
            event.handled = true;
            // the travel the pointer covered becomes the offset, because the bar maps one onto the other (§8.3)
            drag_to(event.position);
            return;
        }
        // a release ends a thumb drag, which leaves the offset where the drag put it (§8.3)
        if (event.kind == EventKind::PointerUp && m_dragging_thumb) {
            // the event is claimed, so no ancestor reacts to the same release (§8.3)
            event.handled = true;
            // the drag ends with the release, so the next press starts a fresh one (§8.3)
            m_dragging_thumb = false;
            // the thumb returns to its resting width, so the wide strip it drew has to be repainted (§6.1)
            invalidate();
            return;
        }
        // a press on the thumb starts a drag, which is what grabbing the bar means (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // only the primary button scrolls the view, because the others are their own gesture (§8.3)
            if (event.button != PointerButton::Left)
                return;
            // a disabled view does not claim a press, because it refuses input (§8.3)
            if (disabled())
                return;
            // a press outside the bounds is not this view's, because only the bar is draggable (§8.3)
            if (!hit_test(event.position) || !scrollbar_visible())
                return;
            // the thumb is the only draggable part, so a press must land on it rather than merely beside it (§8.3)
            const Rect thumb = thumb_rect();
            // both axes are checked, because the thumb is a strip and a row beside it must not start a drag (§8.3)
            const bool on_thumb = event.position.x >= thumb.x && event.position.x < thumb.x + thumb.width && event.position.y >= thumb.y && event.position.y < thumb.y + thumb.height;
            if (!on_thumb)
                return;
            // the event is claimed, so no ancestor reacts to the same press (§8.3)
            event.handled = true;
            // the drag begins, and it measures from where the thumb and the pointer stand (§8.3)
            m_dragging_thumb = true;
            // the pointer position is the origin the drag measures against (§8.3)
            m_drag_origin = event.position;
            // the offset is the other half of that measurement, so the thumb does not jump (§8.3)
            m_drag_origin_offset = offset();
            // the thumb widens while it is dragged, so the strip it now draws has to be repainted (§6.1)
            invalidate();
            return;
        }
        // a key reaches the viewport unmodified, because a modified key is the application's (§8.3)
        if (event.kind != EventKind::KeyDown || !event.modifiers.none())
            return;
        // a host that owns navigation removes the paging keys, which is what a list hosted view does (§8.3)
        if (!m_key_navigation)
            return;
        // a disabled view does not claim a key, because it refuses input (§8.3)
        if (disabled())
            return;
        // the paging keys are the ones a viewport answers, and each names where the offset lands (§8.3)
        if (event.key != Key::ArrowDown && event.key != Key::ArrowUp && event.key != Key::PageDown && event.key != Key::PageUp && event.key != Key::Home && event.key != Key::End)
            return;
        // a line key moves one step and a page key moves a viewport, which is the difference a key name carries (§8.3)
        const float line = theme() != nullptr ? theme()->metrics().scroll_line_extent : SCROLL_LINE_EXTENT;
        // the move is claimed only where the offset landed, so an enclosing viewport takes a key at the edge (§8.3)
        bool moved = false;
        if (event.key == Key::ArrowDown)
            moved = scroll_by(line);
        // the other line key moves the same distance the other way (§8.3)
        else if (event.key == Key::ArrowUp)
            moved = scroll_by(-line);
        // a page key moves a whole viewport, which is what paging means (§8.3)
        else if (event.key == Key::PageDown)
            moved = scroll_by(content_viewport().height);
        // the other page key moves a whole viewport the other way (§8.3)
        else if (event.key == Key::PageUp)
            moved = scroll_by(-content_viewport().height);
        // the home key goes to the leading edge, which is the only place the content starts (§8.3)
        else if (event.key == Key::Home)
            moved = scroll_to(0.0f);
        // the end key goes to the trailing edge, which is the furthest the content can be scrolled (§8.3)
        else
            moved = scroll_to(maximum_offset());
        // the event is claimed when the viewport moved, which is the same rule the wheel follows (§8.3)
        event.handled = moved;
        return;
    }

    bool ScrollView::hit_test(Point logical_point) const {
        // a hidden view is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled view is not a hit, because it refuses input (§8.3)
        if (disabled())
            return false;
        // the press must land inside the bounds, because content scrolled out of the content viewport is not there (§8.3)
        const bool inside = logical_point.x >= m_bounds.x && logical_point.x < m_bounds.x + m_bounds.width && logical_point.y >= m_bounds.y && logical_point.y < m_bounds.y + m_bounds.height;
        return inside && !m_bounds.empty();
    }

    SemanticNode ScrollView::semantic_node() const {
        // the role is a viewport, because the content behind it is larger than what it shows (§10.1)
        SemanticNode node{SemanticRole::ScrollView};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the name is the author identity, because a viewport carries no text of its own (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(m_bounds);
        // a disabled viewport reports the disabled state, which is what refuses scrolling (§10.1)
        node.set_disabled(disabled());
        // a viewport is not checkable, so the check state stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the actions move the content, because assistive technology drives what the wheel drives (§10.1)
        (void)node.add_action("scroll_down", [control = mutable_owner(this)] {
            // the action moves down by a line, which is what the wheel notch a screen reader sends means (§10.1)
            const float line = control->theme() != nullptr ? control->theme()->metrics().scroll_line_extent : SCROLL_LINE_EXTENT;
            control->scroll_by(line);
        });
        // the matching action moves up, which completes the pair assistive technology expects (§10.1)
        (void)node.add_action("scroll_up", [control = mutable_owner(this)] {
            // the action moves up by a line, which is the other direction the same wheel notch takes (§10.1)
            const float line = control->theme() != nullptr ? control->theme()->metrics().scroll_line_extent : SCROLL_LINE_EXTENT;
            control->scroll_by(-line);
        });
        // no absolute jump is offered, because a semantic action carries no target to jump to (§10.1)
        return node;
    }

    void ScrollView::set_viewport_extent(Size size) {
        // an unchanged extent relayouts nothing, because the viewport did not change (§8.2)
        if (m_viewport_extent == size)
            return;
        // the extent is stored first, so the invalidation walks a viewport that already knows it (§8.2)
        m_viewport_extent = size;
        // the viewport moved, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    Size ScrollView::viewport_extent() const { return m_viewport_extent; }

    void ScrollView::set_row_height(float pixels) {
        // an unchanged height lays the same rows out, so nothing rebuilds (§8.2, AD-13)
        if (m_row_height == pixels)
            return;
        // the height is stored first, so a rebuild walks a viewport that already knows it (§8.2, AD-13)
        m_row_height = pixels;
        // the content extent is the count of rows times the height, so both passes run again (§8.2, AD-13)
        invalidate_layout();
    }

    float ScrollView::row_height() const { return m_row_height; }

    void ScrollView::set_scrollbar_visibility(ScrollbarVisibility visibility) {
        // an unchanged policy redraws nothing, because the bar looks the same (§8)
        if (m_visibility == visibility)
            return;
        // the policy is stored first, so the invalidation walks a view that already knows it (§8)
        m_visibility = visibility;
        // the bar appearing or disappearing changes the content area, so both layout passes run (§8.2)
        invalidate_layout();
    }

    ScrollbarVisibility ScrollView::scrollbar_visibility() const { return m_visibility; }

    bool ScrollView::scrollbar_visible() const {
        // an always on bar is drawn even when the content fits, because that is what the policy asked (§8)
        if (m_visibility == ScrollbarVisibility::AlwaysOn)
            return true;
        // an always off bar is never drawn, while the content still scrolls (§8)
        if (m_visibility == ScrollbarVisibility::AlwaysOff)
            return false;
        // the as needed policy draws the bar the measure pass found content for (§8)
        return m_bar_needed;
    }

    void ScrollView::set_overscan(float pixels) {
        // an unchanged overscan changes no window, so the content is not rebuilt (§8.2)
        if (m_overscan == pixels)
            return;
        // the overscan is stored first, so the window the content is told about already carries it (§8.2)
        m_overscan = pixels;
        // the window the content builds against moved, so its rows are reconciled again (§8.2, AD-13)
        invalidate_layout();
    }

    float ScrollView::overscan() const { return m_overscan; }

    void ScrollView::bind_offset(Property<float>::StaticEvaluator evaluator, void* context, std::span<const PropertyId> dependencies) {
        // the binding writes the bound channel, which a user scroll then outranks until the next value (§7.3, AD-24)
        m_offset.bind_static(evaluator, context, dependencies);
        // a binding moves the offset, so the placement follows it (§8.2)
        invalidate_arrange();
    }

    void ScrollView::set_offset(float offset) {
        // a write that changes the presented offset detaches a binding, while a no-op keeps it (§7.3, AD-24)
        scroll_to(offset);
    }

    float ScrollView::offset() const {
        // the presented offset is clamped, so a binding that outgrew its content cannot push the viewport away (§8.2)
        return std::clamp(m_offset.get(), 0.0f, maximum_offset());
    }

    bool ScrollView::user_scrolled() const { return m_offset.overridden(); }

    float ScrollView::maximum_offset() const {
        // content that fits has nothing to scroll, which is the zero range (§8.2)
        return std::max(0.0f, m_content_extent.height - content_viewport().height);
    }

    void ScrollView::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the viewport looks the same (§8.3)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so unbinding a command later restores what the form declared (§18.7)
        m_disabled = disabled;
        // a bound command owns the state, so the imperative write is recorded and not applied (§18.7)
        if (command() != nullptr)
            return;
        // a disabled viewport stops answering the wheel and the keys, so the frame changes (§8.3)
        invalidate();
    }

    bool ScrollView::disabled() const {
        // a bound command is the single answer, which is what three consumers reading one object means (§18.7)
        if (command() != nullptr)
            return !command()->state().enabled;
        // without a command the state is what the dispatch and the hit test both refuse on (§8.3)
        return m_disabled;
    }

    void ScrollView::set_key_navigation(bool enabled) {
        // the flag is stored, because it is read once per key rather than cached anywhere (§8.3)
        m_key_navigation = enabled;
    }

    bool ScrollView::key_navigation() const {
        // the flag reports whether the paging keys belong to this viewport or to its host (§8.3)
        return m_key_navigation;
    }

    Size ScrollView::content_extent() const { return m_content_extent; }

    std::size_t ScrollView::materialized_row_count() const {
        // content that does not build itself has no rows, which is the plain container path (§8.2, AD-13)
        ScrollableContent* scrollable = scrollable_content();
        // the content owns the count, because it is the only thing that knows which rows exist (§8.2, AD-13)
        return scrollable == nullptr ? 0 : scrollable->materialized_row_count();
    }

    std::size_t ScrollView::first_row_index() const {
        // content that does not build itself starts at no item, which is the plain container path (§8.2, AD-13)
        ScrollableContent* scrollable = scrollable_content();
        // the content owns the first index, because it is the only thing that knows which item leads (§8.2, AD-13)
        return scrollable == nullptr ? 0 : scrollable->first_row_index();
    }

    UiNode* ScrollView::row_node(std::size_t position) const {
        // content that does not build itself has no rows, which the plain container path reports (§8.2, AD-13)
        ScrollableContent* scrollable = scrollable_content();
        // the content owns the rows, because it is the only thing that materialized them (§8.2, AD-13)
        return scrollable == nullptr ? nullptr : scrollable->row_node(position);
    }

    bool ScrollView::scrollable() const { return m_content_extent.height > content_viewport().height + 0.0f; }

    bool ScrollView::scroll_by(float amount) { return scroll_to(offset() + amount); }

    bool ScrollView::scroll_to(float destination) {
        // the destination is clamped into the range, which is what stops a drag overshooting the content (§8.2)
        const float clamped = std::clamp(destination, 0.0f, maximum_offset());
        // the presented offset decides the no-op; the stored channel keeps its value for a range that grows (§7.3)
        if (offset() == clamped)
            return false;
        // a press on the bar is a user scroll, which takes precedence over a bound offset (§7.3, AD-24)
        m_offset.set(clamped);
        // the offset moved the content, so only the placement runs (§8.2)
        invalidate_arrange();
        // a handler that is installed is told, which is what the signal reports (§8.3)
        if (m_on_scroll)
            m_on_scroll(clamped);
        // the offset moved, which is what the wheel claim reads (§8.3)
        return true;
    }

    void ScrollView::set_on_scroll(std::function<void(float)> handler) { m_on_scroll = std::move(handler); }

    bool ScrollView::watches() const { return static_cast<bool>(m_on_scroll); }

    Rect ScrollView::content_viewport() const {
        // a bar that reserves takes its gutter from the content, which keeps the content out of it (§8.2)
        const float width = std::max(0.0f, m_bounds.width - (bar_reserved() ? bar_extent() : 0.0f));
        // the content starts where the viewport starts, because only the trailing strip is reserved (§8.2)
        return Rect::from_origin_size(Point{.x = m_bounds.x, .y = m_bounds.y}, Size{.width = width, .height = m_bounds.height});
    }

    bool ScrollView::bar_reserved() const {
        // an overlay bar paints over the content, so it never takes a gutter from it (§8.2)
        if (bar_overlay())
            return false;
        // the policy is the one statement of the matrix: off takes none, on always, as needed from the measure (§8)
        return m_visibility == ScrollbarVisibility::AlwaysOn || (m_visibility == ScrollbarVisibility::AsNeeded && m_bar_needed);
    }

    bool ScrollView::bar_overlay() const {
        // an unthemed view reserves the gutter it has always reserved, because no preset decided otherwise (§6.1)
        return theme() != nullptr && theme()->metrics().scrollbar_overlay;
    }

    float ScrollView::bar_inset() const {
        // an unthemed view keeps the bar flush with the edge, because no preset decided otherwise (§6.1)
        if (theme() == nullptr)
            return 0.0f;
        // a negative inset would push the bar outside the bounds, so the standoff never inverts (§8.2)
        return std::max(0.0f, theme()->metrics().scrollbar_overlay_inset);
    }

    float ScrollView::bar_extent() const {
        // the width is what the theme says, or the value the bar always drew with (§6.1)
        const float extent = theme() != nullptr ? theme()->metrics().scrollbar_extent : SCROLLBAR_EXTENT;
        // a negative metric would widen the content past the viewport, so the gutter never inverts (§8.2)
        return std::max(0.0f, extent);
    }

    float ScrollView::drawn_bar_extent() const {
        // the resting width is what the theme declares, or the value the bar always drew with (§6.1)
        float extent = bar_extent();
        // an overlay bar widens while the pointer is on it or the thumb is dragged, which keeps it grabbable (§6.1)
        if (bar_overlay() && theme()->metrics().scrollbar_hover_extent > 0.0f && (hovered() || m_dragging_thumb))
            extent = theme()->metrics().scrollbar_hover_extent;
        // a bar wider than the view is clipped to it, so the strip never paints outside the bounds (§8.2)
        return std::min(extent, m_bounds.width);
    }

    Size ScrollView::measure_content(float width, float height) {
        // the reported extent starts empty, because a content child is what names it (§8.2)
        Size measured_extent{};
        // each child measures against the ceiling its own kind implies, so a tall content keeps its natural height (§8.2)
        for (const auto& child : children()) {
            // a child that scrolls itself keeps the viewport height, because a taller one collapses its range (§8.2)
            const auto* self_scrolling = dynamic_cast<const SelfScrollingContent*>(child.get());
            // the ceiling is the viewport for a self scrolling child, and none for the content a viewport scrolls (§8.2)
            const float ceiling = self_scrolling != nullptr && self_scrolling->owns_vertical_scroll() ? height : std::numeric_limits<float>::infinity();
            // the child measures against the width the gutter leaves it and the ceiling its kind implies (§8.2)
            const SizeConstraints content_constraints = SizeConstraints::at_most(Size{.width = width, .height = ceiling});
            Size child_size{};
            // the measure settles the size the child reports, which is what the extent below reads (§8.2)
            child->measure(content_constraints, child_size);
            // the content the scroll view scrolls is one container, so the last measured extent is the one (§8.2)
            measured_extent = child_size;
        }
        // the scrollable contract reports the extent of the whole content rather than the built part (§8.2, AD-13)
        const ScrollableContent* scrollable = dynamic_cast<const ScrollableContent*>(child_at(0));
        // the reported extent is authoritative, because a virtualizing content builds only the visible rows (§8.2)
        return scrollable != nullptr ? scrollable->content_extent() : measured_extent;
    }

    Rect ScrollView::scrollbar_track_rect() const {
        // an unplaced view has no gutter to paint, so there is no strip either (§6.2)
        if (m_bounds.empty())
            return Rect{};
        // the strip is the width the current state draws at, which a hover widens (§6.1)
        const float extent = drawn_bar_extent();
        // an overlay bar stands off the trailing edge, while a gutter bar sits against it (§8.2)
        const float trailing = bar_overlay() ? bar_inset() : 0.0f;
        // the strip starts where the extent and the standoff leave it inside the bounds (§6.2)
        const float x = m_bounds.x + std::max(0.0f, m_bounds.width - trailing - extent);
        return Rect::from_origin_size(Point{.x = x, .y = m_bounds.y}, Size{.width = extent, .height = m_bounds.height});
    }

    void ScrollView::paint_scrollbar(RenderContext& context) const {
        // the policy decides whether a bar is drawn at all, which is a per instance decision (§8)
        if (!scrollbar_visible())
            return;
        // the strip is the width the bar paints at, which the content viewport excludes unless the bar overlays (§8.2)
        const Rect track = scrollbar_track_rect();
        // the track colour resolves by state, so a hover and a press differ without a second literal (§6.1)
        context.draw_rect(track, {.fill = resolve(track_colors(), Color{0.0f, 0.0f, 0.0f, 0.0f})});
        // a viewport with no travel has no reachable position, so the thumb is left off (§6.2)
        if (!scrollable())
            return;
        // the thumb takes the width of the strip, so its end caps sit flush with the bar column (§6.1)
        const float extent = drawn_bar_extent();
        // the thumb colour resolves by state, so the same resolver the other controls use reads it (§6.1)
        context.draw_rounded_rect(thumb_rect(), extent * 0.5f, {.fill = resolve(thumb_colors(), Color{0.42f, 0.46f, 0.52f, 1.0f})});
    }

    Rect ScrollView::thumb_rect() const {
        // the smallest thumb is what the theme says, or the value the bar always drew with (§6.1)
        const float floor = theme() != nullptr ? theme()->metrics().scrollbar_thumb_minimum : SCROLLBAR_THUMB_MINIMUM;
        // the thumb takes the strip the bar paints at, at the travel the offset reports (§8.2)
        return scroll_thumb_rect(offset(), maximum_offset(), scrollbar_track_rect(), floor);
    }

    void ScrollView::drag_to(Point logical_position) {
        // a viewport with no travel has nowhere for the thumb to go (§8.2)
        if (!scrollable())
            return;
        // the metrics come from the theme when one is installed (§6.1)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the smallest thumb is what the theme says, or the value the bar always drew with (§6.1)
        const float floor = theme() != nullptr ? metrics.scrollbar_thumb_minimum : SCROLLBAR_THUMB_MINIMUM;
        // the thumb is as tall as the policy allows, which is what a drag measures its travel against (§8.2)
        const Rect thumb = thumb_rect();
        // the distance the thumb can still travel is the track less the thumb, which is what maps onto the offset (§8.2)
        const float travel = std::max(0.0f, m_bounds.height - thumb.height);
        // a thumb that fills the track has no travel, so the drag moves nothing (§8.2)
        if (travel <= 0.0f || m_bounds.height <= floor)
            return;
        // the fraction the pointer covered is the fraction of the content the offset takes (§8.3)
        const float fraction = std::clamp((logical_position.y - m_drag_origin.y) / travel, -1.0f, 1.0f);
        // the offset follows that fraction, measured from where the drag began (§8.3)
        scroll_to(m_drag_origin_offset + fraction * maximum_offset());
    }

    const Color* ScrollView::track_colors() const {
        // without a theme the table is unused, because the resolver returns the bar's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().scrollbar_track;
    }

    const Color* ScrollView::thumb_colors() const {
        // without a theme the table is unused, because the resolver returns the bar's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().scrollbar_thumb;
    }

    void ScrollView::push_viewport() const {
        // content that does not build itself has no window to take, so nothing is pushed (§8.2)
        ScrollableContent* scrollable = scrollable_content();
        // a plain container is measured as a whole, which is the path a non virtualizing scroll view takes (§8.2)
        if (scrollable == nullptr)
            return;
        // the row height travels first, because the window it produces is arithmetic over that height (§8.2, AD-13)
        apply_row_extent();
        // the window carries the content viewport, the content extent, the offset and the overscan (§8.2)
        const Rect viewport = content_viewport();
        // the window names the area the content actually occupies, because a reserved gutter is not content (§8.2)
        const ScrollViewport window{.viewport = viewport.empty() ? Size{} : Size{.width = viewport.width, .height = viewport.height}, .content = m_content_extent, .offset = offset(), .overscan = m_overscan};
        // the content builds against the window, which is what keeps only the visible rows materialized (§8.2, AD-13)
        scrollable->set_scroll_viewport(window);
    }

    ScrollableContent* ScrollView::scrollable_content() const {
        // a scroll view scrolls one content container, which is the first child it adopted (§8.2)
        const UiNode* child = child_at(0);
        // content that is not scrollable has no window, which is what a plain column is (§8.2)
        return child == nullptr ? nullptr : dynamic_cast<ScrollableContent*>(const_cast<UiNode*>(child));
    }

    void ScrollView::apply_row_extent() const {
        // content that does not build itself divides by no row height, so nothing is told (§8.2)
        ScrollableContent* scrollable = scrollable_content();
        // a plain container is measured as a whole, which is the path a non virtualizing scroll view takes (§8.2)
        if (scrollable == nullptr)
            return;
        // the height settles the content extent, so it has to arrive before the measure that reads it (§8.2, AD-13)
        scrollable->set_row_extent(m_row_height);
    }
} // namespace albor
