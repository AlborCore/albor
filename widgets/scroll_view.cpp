#include "widgets/scroll_view.h"

#include <algorithm>
#include <limits>

#include "commands/command.h"

namespace albor
{
    // the extent one wheel notch moves the content when no theme is installed (§8.3)
    constexpr float SCROLL_LINE_EXTENT = 40.0f;

    // the width a scrollbar is drawn when no theme is installed (§6.1)
    constexpr float SCROLLBAR_EXTENT = 10.0f;

    // the smallest thumb a scrollbar draws when no theme is installed (§6.1)
    constexpr float SCROLLBAR_THUMB_MINIMUM = 24.0f;

    ScrollView::ScrollView(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)),
        // the offset registers with the engine, which scopes its identifier (§7.2)
        m_offset(0.0f) {
        // the property needs an identifier before a binding can key on it (§7.2)
        register_property(m_offset);
    }

    void ScrollView::measure(const SizeConstraints& constraints, Size& out_size) {
        // the viewport is what the author declared, or the whole space the parent offers (§8.2)
        const bool declared = m_viewport_extent.width > 0.0f || m_viewport_extent.height > 0.0f;
        // an undeclared viewport fills the offer, because a viewport that shrank to its content could not scroll (§8.2)
        const Size viewport = constraints.constrain(declared ? m_viewport_extent : constraints.maximum);
        // the measured size is the viewport, because a scroll container never grows with the content behind it (§8.2)
        m_measured_size = viewport;
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        // the result is what the parent places, which is the viewport rather than the content (§8.2)
        out_size = m_measured_size;
        // the content is measured with no height ceiling, because a tall content scrolls (§8.2)
        const SizeConstraints content_constraints = SizeConstraints::at_most(Size{.width = viewport.width, .height = std::numeric_limits<float>::infinity()});
        // the row height reaches the content first, because the extent below is arithmetic over it (§8.2, AD-13)
        apply_row_extent();
        // the reported extent wins, because only it knows the rows it did not build (§8.2)
        Size measured_extent{};
        for (const auto& child : children()) {
            // each child measures against that ceiling, so the content keeps its natural height (§8.2)
            Size child_size{};
            child->measure(content_constraints, child_size);
            // the content the scroll view scrolls is one container, so the last measured extent is the one (§8.2)
            measured_extent = child_size;
        }
        // the scrollable contract reports the extent of the whole content rather than the built part (§8.2, AD-13)
        const ScrollableContent* scrollable = dynamic_cast<const ScrollableContent*>(child_at(0));
        // the reported extent is authoritative, because a virtualizing content builds only the visible rows (§8.2)
        m_content_extent = scrollable != nullptr ? scrollable->content_extent() : measured_extent;
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
        // the assigned bounds replace the previous ones, while the measured size stands as the measure settled it (§8.2)
        m_bounds = bounds;
        // the window reaches the content before it is placed, so its rows exist in time (§8.2, AD-13)
        push_viewport();
        // the viewport is the assigned bounds, because the bar overlays rather than insetting (§8.2)
        const Rect viewport = viewport_rect();
        // the content sits at the offset, which is the whole of what scrolling a viewport does (§8.2)
        const Rect content = Rect::from_origin_size(Point{.x = viewport.x, .y = viewport.y - offset()}, m_content_extent);
        // the single content child is placed at the offset, which is what moves the content (§8.2)
        for (const auto& child : children())
            arrange_if_changed(*child, content);
    }

    void ScrollView::paint_children(RenderContext& context) {
        // a view with no area clips nothing, because there is no viewport to clip to (§6.2)
        if (m_bounds.empty())
            return;
        // the clip is pushed so the content can be scissored to the viewport (§6.2)
        context.save();
        // the viewport is the clip, which is what keeps scrolled content out of the surrounding surface (§6.2)
        context.clip_rect(viewport_rect());
        // the content records inside the clip, in insertion order (§6.1)
        render_children(context);
        // the clip is popped, so the bar below is not scissored to it (§6.2)
        context.restore();
        // the bar draws over the content, because it reports a position the content itself cannot show (§6.1)
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
            // the event is claimed, so no ancestor acts on the same wheel (§8.3)
            event.handled = true;
            // the delta is converted here, because this container owns the policy (§8.3)
            scroll_by(event.scroll * wheel_scale(event));
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
            return;
        }
        // a press on the thumb starts a drag, which is what grabbing the bar means (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // a disabled view does not claim a press, because it refuses input (§8.3)
            if (disabled())
                return;
            // a press outside the viewport is not this view's, because only the bar is draggable (§8.3)
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
        // the event is claimed, so no ancestor reacts to the same key (§8.3)
        event.handled = true;
        // a line key moves one step and a page key moves a viewport, which is the difference a key name carries (§8.3)
        const float line = theme() != nullptr ? theme()->metrics().scroll_line_extent : SCROLL_LINE_EXTENT;
        if (event.key == Key::ArrowDown)
            scroll_by(line);
        // the other line key moves the same distance the other way (§8.3)
        else if (event.key == Key::ArrowUp)
            scroll_by(-line);
        // a page key moves a whole viewport, which is what paging means (§8.3)
        else if (event.key == Key::PageDown)
            scroll_by(viewport_rect().height);
        // the other page key moves a whole viewport the other way (§8.3)
        else if (event.key == Key::PageUp)
            scroll_by(-viewport_rect().height);
        // the home key goes to the leading edge, which is the only place the content starts (§8.3)
        else if (event.key == Key::Home)
            scroll_to(0.0f);
        // the end key goes to the trailing edge, which is the furthest the content can be scrolled (§8.3)
        else
            scroll_to(maximum_offset());
        return;
    }

    bool ScrollView::hit_test(Point logical_point) const {
        // a hidden view is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled view is not a hit, because it refuses input (§8.3)
        if (disabled())
            return false;
        // the press must land inside the viewport, because content scrolled out of it is not there to press (§8.3)
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
        // the bar appeared or disappeared, which changes the frame (§6.1)
        invalidate();
    }

    ScrollbarVisibility ScrollView::scrollbar_visibility() const { return m_visibility; }

    bool ScrollView::scrollbar_visible() const {
        // an always on bar is drawn even when the content fits, because that is what the policy asked (§8)
        if (m_visibility == ScrollbarVisibility::AlwaysOn)
            return true;
        // an always off bar is never drawn, while the content still scrolls (§8)
        if (m_visibility == ScrollbarVisibility::AlwaysOff)
            return false;
        // the as needed policy draws the bar only while the content reaches past the viewport (§8)
        return scrollable();
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
        // the offset is written as a user scroll writes it, so it detaches a binding (§7.3, AD-24)
        scroll_to(offset);
    }

    float ScrollView::offset() const {
        // the presented offset is clamped, so a binding that outgrew its content cannot push the viewport away (§8.2)
        return std::clamp(m_offset.get(), 0.0f, maximum_offset());
    }

    bool ScrollView::user_scrolled() const { return m_offset.overridden(); }

    float ScrollView::maximum_offset() const {
        // content that fits has nothing to scroll, which is the zero range (§8.2)
        return std::max(0.0f, m_content_extent.height - viewport_rect().height);
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

    bool ScrollView::scrollable() const { return m_content_extent.height > viewport_rect().height + 0.0f; }

    void ScrollView::scroll_by(float amount) { scroll_to(offset() + amount); }

    void ScrollView::scroll_to(float offset) {
        // the destination is clamped into the range, which is what stops a drag overshooting the content (§8.2)
        const float clamped = std::clamp(offset, 0.0f, maximum_offset());
        // an unchanged destination moves nothing, so no write and no report (§8.2)
        if (m_offset.get() == clamped)
            return;
        // a press on the bar is a user scroll, which takes precedence over a bound offset (§7.3, AD-24)
        m_offset.set(clamped);
        // the offset moved the content, so only the placement runs (§8.2)
        invalidate_arrange();
        // a handler that is installed is told, which is what the signal reports (§8.3)
        if (m_on_scroll)
            m_on_scroll(clamped);
    }

    void ScrollView::set_on_scroll(std::function<void(float)> handler) { m_on_scroll = std::move(handler); }

    bool ScrollView::watches() const { return static_cast<bool>(m_on_scroll); }

    float ScrollView::wheel_scale(const Event& event) const {
        // the policy is the themed distance a notch moves the content, which is what this view owns (§8.3)
        const float line = theme() != nullptr ? theme()->metrics().scroll_line_extent : SCROLL_LINE_EXTENT;
        // a trackpad already counts points, so the delta is the distance; a notched wheel counts notches (§8.3)
        if (event.precision == ScrollPrecision::Point)
            return 1.0f;
        // the notched wheel moves a whole line, which is the policy the theme states (§8.3)
        return line;
    }

    Rect ScrollView::viewport_rect() const { return m_bounds; }

    void ScrollView::paint_scrollbar(RenderContext& context) const {
        // the policy decides whether a bar is drawn at all, which is a per instance decision (§8)
        if (!scrollbar_visible())
            return;
        // the metrics come from the theme when one is installed (§6.1)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the width is what the theme says, or the value the bar always drew with (§6.1)
        const float extent = theme() != nullptr ? metrics.scrollbar_extent : SCROLLBAR_EXTENT;
        // the bar sits against the trailing edge of the viewport, which is where a vertical bar belongs (§6.1)
        const Rect track = Rect::from_origin_size(Point{.x = m_bounds.x + m_bounds.width - extent, .y = m_bounds.y}, Size{.width = extent, .height = m_bounds.height});
        // the track colour resolves by state, so a hover and a press differ without a second literal (§6.1)
        context.draw_rect(track, {.fill = resolve(track_colors(), Color{0.0f, 0.0f, 0.0f, 0.0f})});
        // a viewport with no travel has no reachable position, so the thumb is left off (§6.2)
        if (!scrollable())
            return;
        // the thumb colour resolves by state, so the same resolver the other controls use reads it (§6.1)
        context.draw_rounded_rect(thumb_rect(), extent * 0.5f, {.fill = resolve(thumb_colors(), Color{0.42f, 0.46f, 0.52f, 1.0f})});
    }

    Rect ScrollView::thumb_rect() const {
        // the metrics come from the theme when one is installed (§6.1)
        const ControlMetrics& metrics = theme() != nullptr ? theme()->metrics() : default_metrics();
        // the width is what the theme says, or the value the bar always drew with (§6.1)
        const float extent = theme() != nullptr ? metrics.scrollbar_extent : SCROLLBAR_EXTENT;
        // the smallest thumb is what the theme says, or the value the bar always drew with (§6.1)
        const float floor = theme() != nullptr ? metrics.scrollbar_thumb_minimum : SCROLLBAR_THUMB_MINIMUM;
        // the thumb takes the share of the track the viewport takes of the content, which is what the ratio means (§8.2)
        const float travel = m_bounds.height;
        // the content is never shorter than the viewport here, because a bar is only drawn when it overflows (§8.2)
        const float share = std::clamp(travel / std::max(1.0f, m_content_extent.height), 0.0f, 1.0f);
        // the thumb never shrinks below the grabbable floor, which is what keeps a long content reachable (§8.2)
        const float height = std::min(travel, std::max(floor, travel * share));
        // the thumb sits where the offset puts it, which is the whole of what a bar reports (§8.2)
        const float maximum = maximum_offset();
        // the fraction is zero while there is nothing to scroll, so the thumb stays at the leading edge (§8.2)
        const float fraction = maximum > 0.0f ? std::clamp(offset() / maximum, 0.0f, 1.0f) : 0.0f;
        return Rect::from_origin_size(Point{.x = m_bounds.x + m_bounds.width - extent, .y = m_bounds.y + (travel - height) * fraction}, Size{.width = extent, .height = height});
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
        // the window carries the viewport, the content extent, the offset and the overscan (§8.2)
        const ScrollViewport window{.viewport = viewport_rect().empty() ? Size{} : Size{.width = m_bounds.width, .height = m_bounds.height}, .content = m_content_extent, .offset = offset(), .overscan = m_overscan};
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
