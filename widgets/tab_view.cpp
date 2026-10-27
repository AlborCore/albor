#include "widgets/tab_view.h"

#include <algorithm>
#include <utility>

#include "widgets/tab_page.h"
#include "widgets/tab_title.h"

namespace albor
{
    TabView::TabView(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void TabView::sync_tabs() const {
        // a count match is only a fast path, because a same-count replacement or reorder changes the identity (§8.1)
        const std::size_t children = child_count();
        bool current = children == m_synced_children;
        if (current) {
            // the cached pages are compared against the direct children, which catches every structural edit (§8.1)
            std::size_t cached = 0;
            for (std::size_t index = 0; index < children; index++) {
                // a child that is not a page is skipped, because only pages join the strip (§8)
                auto* tab = dynamic_cast<TabPage*>(child_at(index));
                if (tab == nullptr)
                    continue;
                // a pointer that differs means the list is stale, even when the count is unchanged (§8.1)
                if (cached >= m_tabs.size() || m_tabs[cached] != tab) {
                    current = false;
                    break;
                }
                cached++;
            }
            // a list that ends short is stale too, because a page was removed without changing the count (§8.1)
            if (current && cached != m_tabs.size())
                current = false;
        }
        // a current list is left alone, which keeps the steady frame allocation free (§8.1)
        if (current)
            return;
        // the selected page is remembered by identity, because a structural edit may move its index (§8)
        const int previous_index = m_tabs.empty() ? -1 : effective_selected();
        TabPage* previous = previous_index >= 0 ? m_tabs[static_cast<std::size_t>(previous_index)] : nullptr;
        // the requested index is kept for the case where no page held the selection yet (§8)
        const int requested = m_selected;
        // the list is rebuilt only when the pointer sequence changed, because a tab view holds a handful of pages (§8.1)
        m_tabs.clear();
        m_tabs.reserve(children);
        // every direct child that is a page joins the strip, which ignores content that is not a page (§8.1)
        for (std::size_t index = 0; index < children; index++) {
            // the page type decides, because a tab view only drives the pages it understands (§8.1)
            auto* tab = dynamic_cast<TabPage*>(child_at(index));
            if (tab == nullptr)
                continue;
            // the page is kept in strip order, which is the order the children were added in (§8.1)
            m_tabs.push_back(tab);
            // a press or an accessible activate selects the page it belongs to (§8.3, §10.1)
            tab->set_on_activate([control = mutable_owner(this), tab] { control->select_tab(tab); });
        }
        // the count is recorded after the walk, so the next frame can take the fast path (§8.1)
        m_synced_children = children;
        // one width per tab is reserved, and the next measure fills it (§8.2)
        m_tab_widths.assign(m_tabs.size(), 0.0f);
        // the widths are stale until the next measure, which keeps a paint after a structural edit correct (§8.2)
        m_widths_dirty = true;
        // the selected page is found in the rebuilt order, which keeps the shown page stable (§8)
        int next = requested;
        if (!m_tabs.empty()) {
            // a page that survived the edit keeps the selection at its new index (§8)
            if (previous != nullptr) {
                const auto found = std::find(m_tabs.begin(), m_tabs.end(), previous);
                next = found != m_tabs.end() ? static_cast<int>(found - m_tabs.begin()) : std::clamp(requested, 0, static_cast<int>(m_tabs.size()) - 1);
            }
            else {
                // a view that had no page clamps the requested index to the pages now present (§8)
                next = std::clamp(requested, 0, static_cast<int>(m_tabs.size()) - 1);
            }
        }
        // a structure that moved or removed the shown page reports the new index once (§8.3)
        if (previous_index >= 0 && next != previous_index)
            m_structure_selection_changed = true;
        // an initial request that had to clamp is reported too, because the model still holds the old index (§8.3)
        if (previous_index < 0 && !m_tabs.empty() && next != requested)
            m_structure_selection_changed = true;
        // the effective index is stored, so a later add does not jump back to a stale request (§8)
        m_selected = next;
    }

    void TabView::compute_header_widths(float font_size) const {
        // one width per tab, in the strip order the pages were found in (§8.2)
        m_tab_widths.resize(m_tabs.size());
        // the inset and the minimum are the two decisions that make a short label grabbable (§8.2)
        const float inset = tab_padding();
        const float minimum = tab_minimum_width();
        for (std::size_t index = 0; index < m_tabs.size(); index++) {
            // the page measures its own run, because the label belongs to the page it names (§9.1)
            m_tabs[index]->measure_header(font_size);
            // a visible title carries its own preferred width, while a plain page measures the label run (§8.2, §9.1)
            const bool titled = m_tabs[index]->title() != nullptr && m_tabs[index]->title()->visible();
            const float content_width = titled ? m_tabs[index]->title_size().width : m_tabs[index]->label_layout().size.width;
            // the width is the content plus both insets, and never below the themed minimum (§8.2)
            m_tab_widths[index] = std::max(minimum, content_width + inset * 2.0f);
        }
        // the widths are current now, which keeps a paint from measuring them again (§8.2)
        m_widths_dirty = false;
    }

    Rect TabView::title_bounds(const Rect& header, const Size& title_size) const {
        // the title takes the header inset on both sides, which is where a label sits (§8.2)
        const float inner_width = std::max(0.0f, header.width - tab_padding() * 2.0f);
        // a title taller than the header is clipped, which keeps the strip one thickness (§8.2)
        const float title_height = std::min(title_size.height, header.height);
        // the title is centred in the header, which is where a tab title reads (§8.2)
        return Rect::from_origin_size(Point{.x = header.x + tab_padding(), .y = header.y + (header.height - title_height) * 0.5f}, Size{.width = inner_width, .height = title_height});
    }

    void TabView::raise_structure_selection() {
        // a view without a pending structural move reports nothing (§8.3)
        if (!m_structure_selection_changed)
            return;
        // the move is consumed once, so an arrange and a setter cannot report the same change twice (§8.3)
        m_structure_selection_changed = false;
        // a view with no pages has no index to report, so the move is settled without a signal (§8)
        if (!m_tabs.empty() && m_on_selection_changed)
            m_on_selection_changed(effective_selected());
    }

    void TabView::measure(const SizeConstraints& constraints, Size& out_size) {
        // the strip is rebuilt before it is measured, because a page added this frame has no width yet (§8.1)
        sync_tabs();
        // every label is measured once per layout pass, which is what settles the strip (§9.1)
        compute_header_widths(label_font_size());
        // the cross axis is one tab thick, which both orientations take from the theme (§8.2)
        const float thickness = strip_thickness();
        // the strip extent depends on the orientation: a top band or a left column (§8.2)
        float strip_width = 0.0f;
        float strip_height = 0.0f;
        if (m_vertical) {
            // a vertical strip is as wide as its widest tab and as tall as every tab stacked (§8.2)
            for (const float width : m_tab_widths)
                strip_width = std::max(strip_width, width);
            strip_height = m_tabs.empty() ? 0.0f : thickness * static_cast<float>(m_tabs.size());
        }
        else {
            // a horizontal strip is as wide as every tab in a row and one tab tall (§8.2)
            for (const float width : m_tab_widths)
                strip_width += width;
            strip_height = m_tabs.empty() ? 0.0f : thickness;
        }
        // the page sees the space that remains beside the strip, because the strip is outside its content (§8.2)
        SizeConstraints page_constraints = constraints;
        if (m_vertical) {
            // a left strip takes its width from the page, which is the horizontal axis (§8.2)
            page_constraints.minimum.width = std::max(0.0f, constraints.minimum.width - strip_width);
            page_constraints.preferred.width = std::max(0.0f, constraints.preferred.width - strip_width);
            page_constraints.maximum.width = std::max(0.0f, constraints.maximum.width - strip_width);
        }
        else {
            // a top strip takes its height from the page, which is the vertical axis (§8.2)
            page_constraints.minimum.height = std::max(0.0f, constraints.minimum.height - strip_height);
            page_constraints.preferred.height = std::max(0.0f, constraints.preferred.height - strip_height);
            page_constraints.maximum.height = std::max(0.0f, constraints.maximum.height - strip_height);
        }
        // only the selected page contributes to the extent, because it is the only one the view shows (§8.2)
        Size page_size{};
        if (TabPage* page = selected_tab())
            page->measure(page_constraints, page_size);
        // a vertical strip adds its width, while a horizontal one adds its height (§8.2)
        const Size content = m_vertical ? Size{.width = strip_width + page_size.width, .height = std::max(strip_height, page_size.height)} : Size{.width = std::max(strip_width, page_size.width), .height = strip_height + page_size.height};
        // the offered constraints are final, because a parent has the last word (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void TabView::arrange(const Rect& bounds) {
        // the view takes the rectangle its parent assigned, which the strip and the pages are placed inside (§8.2)
        m_bounds = bounds;
        // the page list and the label widths are current before any page is placed (§8.1, §8.2)
        sync_tabs();
        compute_header_widths(label_font_size());
        // the selected page is the only visible one, which is what the selection means (§8)
        apply_selection();
        // the cross axis is one tab thick, which both orientations take from the theme (§8.2)
        const float thickness = strip_thickness();
        // the strip width is the widest tab on a left strip, and the whole bounds on a top strip (§8.2)
        float strip_width = 0.0f;
        if (m_vertical) {
            for (const float width : m_tab_widths)
                strip_width = std::max(strip_width, width);
            // a left strip never grows past the view, which keeps its content inside the clip (§8.2)
            strip_width = std::min(strip_width, bounds.width);
        }
        // the hidden extent is clipped to the bounds, because a strip never draws outside its view (§8.2)
        const float strip_height = m_vertical ? std::min(thickness * static_cast<float>(m_tabs.size()), bounds.height) : std::min(thickness, bounds.height);
        // the page sits beside a left strip or under a top strip, which is the only difference (§8.2)
        const Rect content = m_vertical ? Rect::from_origin_size(Point{.x = bounds.x + strip_width, .y = bounds.y}, Size{.width = std::max(0.0f, bounds.width - strip_width), .height = bounds.height}) : Rect::from_origin_size(Point{.x = bounds.x, .y = bounds.y + strip_height}, Size{.width = bounds.width, .height = std::max(0.0f, bounds.height - strip_height)});
        // the tabs are laid left to right or top to bottom, in the order their pages were adopted (§8.2)
        float x = bounds.x;
        float y = bounds.y;
        for (std::size_t index = 0; index < m_tabs.size(); index++) {
            // a vertical header is one strip width and row tall; a horizontal one is label width and strip height (§8.2)
            const Rect header = m_vertical ? Rect::from_origin_size(Point{.x = bounds.x, .y = y}, Size{.width = strip_width, .height = thickness}) : Rect::from_origin_size(Point{.x = x, .y = bounds.y}, Size{.width = m_tab_widths[index], .height = strip_height});
            // the header is the strip slice this tab owns, which a press aims at (§8.3)
            m_tabs[index]->set_header_bounds(header);
            // a visible title is placed with the header, which keeps layout out of the paint pass (§8.2)
            if (TabTitle* title = m_tabs[index]->title(); title != nullptr && title->visible())
                title->arrange(title_bounds(header, m_tabs[index]->title_size()));
            // the page body sits beside or under the strip, whether or not this page is the shown one (§8.2)
            m_tabs[index]->arrange(content);
            // the next tab starts where this one ends, which is what makes the strip contiguous (§8.2)
            x += m_vertical ? 0.0f : m_tab_widths[index];
            y += m_vertical ? thickness : 0.0f;
        }
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
        // a structure that moved the shown page reports its new index once, after the view is consistent (§8.3)
        raise_structure_selection();
    }

    void TabView::paint_content(RenderContext& context) {
        // a view with no area draws nothing, because the strip has no place to land (§6.2)
        if (bounds().empty())
            return;
        // the strip is current before it is drawn, because a page may have been added this frame (§8.1)
        sync_tabs();
        // a view with no pages has no strip to draw (§8)
        if (m_tabs.empty())
            return;
        // a layout that did not run still has widths, which the strip needs to place its labels (§8.2)
        if (m_widths_dirty || m_tab_widths.size() != m_tabs.size())
            compute_header_widths(label_font_size());
        // the cross axis is one tab thick, which both orientations take from the theme (§8.2)
        const float thickness = strip_thickness();
        // a left strip is as wide as its widest tab, while a top strip spans the view (§8.2)
        float strip_width = 0.0f;
        if (m_vertical) {
            for (const float width : m_tab_widths)
                strip_width = std::max(strip_width, width);
            // a left strip never grows past the view, which keeps its content inside the clip (§8.2)
            strip_width = std::min(strip_width, bounds().width);
        }
        // the strip is clipped to the bounds, because a long strip never draws outside its view (§8.2)
        const Rect strip_rect = m_vertical ? Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y}, Size{.width = strip_width, .height = std::min(thickness * static_cast<float>(m_tabs.size()), bounds().height)}) : Rect::from_origin_size(Point{.x = bounds().x, .y = bounds().y}, Size{.width = bounds().width, .height = std::min(thickness, bounds().height)});
        // a strip with no area has nothing to draw (§6.2)
        if (strip_rect.empty())
            return;
        // the selected index is resolved once, because every tab compares against it (§8)
        const int selected = effective_selected();
        // the strip clips its labels, because a long row of tabs may exceed the view (§8.2)
        context.save();
        context.clip_rect(strip_rect);
        for (std::size_t index = 0; index < m_tabs.size(); index++) {
            // an unplaced header has nothing to draw, which a view that was never arranged leaves (§8.2)
            const Rect& header = m_tabs[index]->header_bounds();
            if (header.empty())
                continue;
            // the selected tab carries an indicator, which marks the page the view shows (§6.1)
            if (static_cast<int>(index) == selected) {
                // the indicator sits on the bottom of a top strip or the left edge of a left strip (§6.1)
                const float indicator = indicator_height();
                const Rect marker = m_vertical ? Rect::from_origin_size(Point{.x = header.x, .y = header.y + tab_padding()}, Size{.width = indicator, .height = std::max(0.0f, header.height - tab_padding() * 2.0f)}) : Rect::from_origin_size(Point{.x = header.x + tab_padding(), .y = header.y + header.height - indicator}, Size{.width = std::max(0.0f, header.width - tab_padding() * 2.0f), .height = indicator});
                context.draw_rect(marker, {.fill = tab_indicator_color()});
            }
            // a visible title draws its own content, which is what makes an icon or a composed run possible (§8)
            if (TabTitle* title = m_tabs[index]->title(); title != nullptr && title->visible()) {
                // the title was placed by the arrange pass, so the paint only records it (§8.2)
                title->render(context);
                continue;
            }
            // a page with no label draws no run, because there is no name to show (§9.1)
            const TextLayout& layout = m_tabs[index]->label_layout();
            if (layout.empty())
                continue;
            // the label is centred vertically in the header, which is where a tab title reads (§9.1)
            const Point origin{.x = header.x + tab_padding(), .y = header.y + (header.height - layout.size.height) * 0.5f};
            context.draw_text(layout, origin, {.fill = tab_label_color(static_cast<int>(index) == selected)});
        }
        // the clip comes off, so a sibling draws at the state the view found (§6.2)
        context.restore();
    }

    void TabView::dispatch_event(Event& event) {
        // a hidden view takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the strip is current before a press is mapped through it (§8.1)
        sync_tabs();
        // a view with no pages has nothing to select, which a press then ignores (§8.3)
        if (m_tabs.empty())
            return;
        // a press on a header selects the page it names, which is the one pointer action of the strip (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // a press outside the view belongs to nothing it holds, even when an overflow header sits there (§8.3)
            if (!hit_test(event.position))
                return;
            for (std::size_t index = 0; index < m_tabs.size(); index++) {
                // the header is the target, because the page body belongs to the page (§8.3)
                const Rect& header = m_tabs[index]->header_bounds();
                // a point outside this header belongs to another tab or to the page (§8.3)
                if (event.position.x < header.x || event.position.x >= header.x + header.width || event.position.y < header.y || event.position.y >= header.y + header.height)
                    continue;
                // the press is claimed, so no ancestor acts on the same click (§8.3)
                event.handled = true;
                // the named page becomes the shown one (§8)
                set_selected(static_cast<int>(index));
                return;
            }
            return;
        }
        // a modified key belongs to the application, so the view only answers an unmodified key down (§8.3)
        if (event.kind != EventKind::KeyDown || !event.modifiers.none())
            return;
        // the arrows move one tab, which is the selection the strip owns (§8.3)
        switch (event.key) {
        case Key::ArrowLeft:
        case Key::ArrowUp:
            // the event is claimed, so no ancestor moves with the same key (§8.3)
            event.handled = true;
            // the previous page is selected, wrapping is not done because a strip has two ends (§8)
            set_selected(effective_selected() - 1);
            return;
        case Key::ArrowRight:
        case Key::ArrowDown:
            // the event is claimed, so no ancestor moves with the same key (§8.3)
            event.handled = true;
            // the next page is selected, stopping at the last one (§8)
            set_selected(effective_selected() + 1);
            return;
        case Key::Home:
            // the event is claimed, so no ancestor moves with the same key (§8.3)
            event.handled = true;
            // home names the first page, which is the start of the strip (§8.3)
            set_selected(0);
            return;
        case Key::End:
            // the event is claimed, so no ancestor moves with the same key (§8.3)
            event.handled = true;
            // end names the last page, which is the other end of the strip (§8.3)
            set_selected(static_cast<int>(m_tabs.size()) - 1);
            return;
        default:
            return;
        }
    }

    SemanticNode TabView::semantic_node() const {
        // the strip is current before the node is announced, because the actions range over the pages (§8.1)
        sync_tabs();
        // the role is a tab group, because the view owns one selection over its pages (§10.1)
        SemanticNode node{SemanticRole::TabView};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // the focus is the view's own, because the strip is what takes keys (§8.3)
        node.set_focused(focused());
        // a tab group is not checkable, because it carries a selection rather than a toggle (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the actions move the selection, which is what automation can drive on a read only strip (§10.1)
        (void)node.add_action("select_next", [control = mutable_owner(this)] { control->set_selected(control->selected() + 1); });
        (void)node.add_action("select_previous", [control = mutable_owner(this)] { control->set_selected(control->selected() - 1); });
        (void)node.add_action("select_first", [control = mutable_owner(this)] { control->set_selected(0); });
        (void)node.add_action("select_last", [control = mutable_owner(this)] { control->set_selected(static_cast<int>(control->tab_count()) - 1); });
        return node;
    }

    void TabView::set_focused(bool focused) {
        // the base stores the state and asks for the focus ring, which is the view's own visual (§8.3)
        Widget::set_focused(focused);
        // the selected tab item repeats the focus, because a reader needs to know which strip item holds it (§10.1)
        apply_selection();
    }

    std::size_t TabView::semantic_child_count() const {
        // the page list is current before the count is reported (§8.1)
        sync_tabs();
        // only the pages are semantic children, because content that is not a page is ignored by the strip (§8)
        return m_tabs.size();
    }

    std::unique_ptr<SemanticProvider> TabView::semantic_child(std::size_t index) const {
        // the page list is current before an index is resolved against it (§8.1)
        sync_tabs();
        // an index past the end names no page, which the lazy walk expects (§10.1)
        if (index >= m_tabs.size())
            return nullptr;
        // the page is watched rather than copied, so the ui tree stays the only owner (§10.1, AD-09)
        return std::make_unique<SemanticProviderView>(m_tabs[index]);
    }

    void TabView::set_selected(int index) {
        // the page list is current before an index is resolved against it (§8.1)
        sync_tabs();
        // a structural move is reported before the new request, so the model hears every actual change (§8.3)
        raise_structure_selection();
        // a view with no pages stores the request, because the author may write the selection first (§8)
        if (m_tabs.empty()) {
            m_selected = std::max(0, index);
            return;
        }
        // the request is clamped to the pages the view holds, because an index past the end names nothing (§8)
        const int next = std::clamp(index, 0, static_cast<int>(m_tabs.size()) - 1);
        // the effective selection is read before the write, because the signal reports an actual move (§8.3)
        const int previous = effective_selected();
        // the index is stored first, so the redraw walks a view that already carries it (§8)
        m_selected = next;
        // the shown page follows the selection, which is what a tab view means (§8)
        apply_selection();
        // a selection that did not move redraws nothing, because the strip looks the same (§6.1)
        if (next == previous)
            return;
        // the page and the strip changed, so the frame needs both layout and paint (§6.1)
        invalidate_layout();
        invalidate();
        // the installed work is told, which is what the selection signal reports (§8.3)
        if (m_on_selection_changed)
            m_on_selection_changed(next);
    }

    int TabView::selected() const {
        // the list is current before the selected index is reported (§8.1)
        sync_tabs();
        // the effective index is what the view shows, which is clamped to the pages it holds (§8)
        return effective_selected();
    }

    void TabView::set_vertical(bool vertical) {
        // an unchanged orientation redraws nothing, because the strip looks the same (§6.1)
        if (m_vertical == vertical)
            return;
        // the orientation is stored first, so the invalidation walks a view that already carries it (§8)
        m_vertical = vertical;
        // the strip moved, so the page and the headers need a new layout and a new frame (§6.1)
        invalidate_layout();
        invalidate();
    }

    bool TabView::vertical() const {
        // the orientation is what the layout and the paint both read (§8)
        return m_vertical;
    }

    std::size_t TabView::tab_count() const {
        // the list is current before its size is reported (§8.1)
        sync_tabs();
        // the count is the number of pages the strip draws (§8)
        return m_tabs.size();
    }

    TabPage* TabView::tab_at(std::size_t index) const {
        // the list is current before an index is resolved against it (§8.1)
        sync_tabs();
        // an index past the end names no page, which a caller treats as absent (§8)
        return index < m_tabs.size() ? m_tabs[index] : nullptr;
    }

    void TabView::set_on_selection_changed(std::function<void(int)> handler) {
        // the work is stored, which the next actual selection move runs (§8.3)
        m_on_selection_changed = std::move(handler);
    }

    void TabView::apply_selection() {
        // the page list is current before the shown page is chosen (§8.1)
        sync_tabs();
        // the selected index is resolved once, because every page compares against it (§8)
        const int selected = effective_selected();
        for (std::size_t index = 0; index < m_tabs.size(); index++) {
            // the page carries the selected state, which its semantic node announces (§10.1)
            m_tabs[index]->set_selected(static_cast<int>(index) == selected);
            // only the shown page is visible, which hides its paint and its pointers (§6.1, §8.3)
            m_tabs[index]->set_visible(static_cast<int>(index) == selected);
            // the shown page repeats the focus of the view, because the strip holds it (§8.3)
            m_tabs[index]->set_tab_focused(focused() && static_cast<int>(index) == selected);
        }
    }

    void TabView::select_tab(TabPage* tab) {
        // the page list is current before a pointer is matched against it (§8.1)
        sync_tabs();
        for (std::size_t index = 0; index < m_tabs.size(); index++) {
            // the pointer identity is the page the press named (§8.3)
            if (m_tabs[index] != tab)
                continue;
            // the page is selected by its strip position, which is what the view stores (§8)
            set_selected(static_cast<int>(index));
            return;
        }
    }

    TabPage* TabView::selected_tab() const {
        // a view with no pages shows none, which the measure pass reads as no content (§8.2)
        if (m_tabs.empty())
            return nullptr;
        // the effective index names the page the view shows (§8)
        return m_tabs[static_cast<std::size_t>(effective_selected())];
    }

    int TabView::effective_selected() const {
        // a view with no pages has no selected index, which zero stands in for (§8)
        if (m_tabs.empty())
            return 0;
        // the request is clamped to the pages the view holds, because an index past the end names nothing (§8)
        return std::clamp(m_selected, 0, static_cast<int>(m_tabs.size()) - 1);
    }

    Color TabView::tab_label_color(bool selected) const {
        // the state selects the palette entry, which a hover or a focus moves (§6.1)
        const std::size_t index = static_cast<std::size_t>(state());
        // an unthemed view keeps the two colours the widget always drew with (§6.1)
        if (theme() == nullptr)
            return selected ? Color{0.20f, 0.55f, 0.90f, 1.0f} : Color{0.68f, 0.71f, 0.76f, 1.0f};
        // the palette answers, because the theme owns the strip colours (§6.1)
        return selected ? theme()->palette().tab_label_selected[index] : theme()->palette().tab_label[index];
    }

    Color TabView::tab_indicator_color() const {
        // the state selects the palette entry, which a hover or a focus moves (§6.1)
        const std::size_t index = static_cast<std::size_t>(state());
        // an unthemed view keeps the accent the widget always drew with (§6.1)
        if (theme() == nullptr)
            return Color{0.20f, 0.55f, 0.90f, 1.0f};
        // the palette answers, because the theme owns the indicator colour (§6.1)
        return theme()->palette().tab_indicator[index];
    }

    float TabView::strip_thickness() const {
        // the installed metrics answer, because a theme sizes the strip (§8.2)
        return theme() != nullptr ? theme()->metrics().tab_strip_height : default_metrics().tab_strip_height;
    }

    float TabView::tab_padding() const {
        // the installed metrics answer, because a theme insets the label (§8.2)
        return theme() != nullptr ? theme()->metrics().tab_padding_horizontal : default_metrics().tab_padding_horizontal;
    }

    float TabView::tab_minimum_width() const {
        // the installed metrics answer, because a theme gives a short tab its width (§8.2)
        return theme() != nullptr ? theme()->metrics().tab_minimum_width : default_metrics().tab_minimum_width;
    }

    float TabView::indicator_height() const {
        // the installed metrics answer, because a theme owns the marker thickness (§6.1)
        return theme() != nullptr ? theme()->metrics().tab_indicator_height : default_metrics().tab_indicator_height;
    }

    float TabView::label_font_size() const {
        // the hierarchy answers, because a tab label is set one step below body text (§8.4, §9.1)
        return theme() != nullptr ? theme()->typography().subheadline : 13.0f;
    }
} // namespace albor
