#include "widgets/list_box.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include "semantics/semantic_provider.h"

namespace albor
{
    namespace
    {
        // the provider that announces one list row, decorating the row's own provider with the list states (§10.1)
        class ListItemProvider final : public SemanticProvider
        {
        public:
            // build a row provider over the node it announces and the row provider it forwards its children to (§10.1)
            ListItemProvider(SemanticNode node, const SemanticProvider* row) :
                m_node(std::move(node)), m_row(row) {}

            // override: hand out the decorated row node, which an adapter announces (§10.1)
            [[nodiscard]] SemanticNode semantic_node() const override { return m_node; }

            // override: forward the child count of the row, which a list does not add to (§10.1)
            [[nodiscard]] std::size_t semantic_child_count() const override { return m_row == nullptr ? 0 : m_row->semantic_child_count(); }

            // override: forward the child of the row, which a list does not add to (§10.1)
            [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override { return m_row == nullptr ? nullptr : m_row->semantic_child(index); }

        private:
            // the decorated node the row announces, which carries the list item role and the list states (§10.1)
            SemanticNode m_node;

            // the row provider whose children are forwarded, which the ui tree owns (§10.1, AD-09)
            const SemanticProvider* m_row;
        };
    } // namespace

    ListBox::ListBox(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {
        // the internal scroll view is built first, because the list drives it from its own layout (§8.2)
        auto scroll = std::make_unique<ScrollView>(engine);
        // the raw pointer is kept before the child is adopted, because the tree owns it from then on (§8.1)
        m_scroll = scroll.get();
        // the viewport is the only child, so adopting it is what gives the list its rows (§8.1, §8.2)
        add_child(std::move(scroll));
        // the list owns the navigation keys, so the hosted viewport leaves them to it (§8.3)
        m_scroll->set_key_navigation(false);
        // the selection reports through one place, which is what repaints and signals every change (§18.3)
        m_selection.set_on_change([this] {
            // the frame is redrawn, because the selected row carries the selected state (§10.1)
            invalidate();
            // the installed work is told, which is what the selection signal reports (§18.3)
            if (m_on_selection_changed)
                m_on_selection_changed();
        });
    }

    void ListBox::measure(const SizeConstraints& constraints, Size& out_size) {
        // the internal viewport measures itself, because the list takes the size it is given (§8.2)
        m_scroll->measure(constraints, out_size);
        // the measured size is that viewport, because a list never grows with its rows (§8.2)
        m_measured_size = out_size;
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
    }

    void ListBox::arrange(const Rect& bounds) {
        // the list fills the space it was given, because it never grows with its rows (§8.2)
        m_bounds = bounds;
        // the viewport takes the whole of it, because the scroll view is the only child (§8.2)
        m_scroll->arrange(bounds);
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    void ListBox::dispatch_event(Event& event) {
        // a hidden list takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // a press on a row selects it, which is what pointing at a row means (§8.3, §18.3)
        if (event.kind == EventKind::PointerDown) {
            // a disabled list refuses the press, because it refuses every input (§8.3)
            if (disabled())
                return;
            // the point maps through the viewport geometry to the row it lands on (§8.2)
            const std::optional<std::size_t> index = item_at(event.position);
            // a press past the rows names nothing to select, so the list leaves it to a parent (§8.3)
            if (!index)
                return;
            // the event is claimed, so the pointer path grants the list the caret (§8.3)
            event.handled = true;
            // the row under the pointer takes the focus and the selection, which is what a click means (§18.3)
            jump_to(*index);
            return;
        }
        // a modified key belongs to the application, so the list only answers an unmodified key down (§8.3)
        if (event.kind != EventKind::KeyDown || !event.modifiers.none())
            return;
        // a disabled list refuses every key, which is what the disabled state means (§8.3)
        if (disabled())
            return;
        // the claimed keys are the ones that move the focus or activate a row, and each names its move (§8.3)
        switch (event.key) {
        case Key::ArrowDown:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focus steps one row forward, which is what the down arrow means (§8.3)
            move_selection(1);
            return;
        case Key::ArrowUp:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focus steps one row back, which is what the up arrow means (§8.3)
            move_selection(-1);
            return;
        case Key::PageDown:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focus steps a whole viewport forward, which is what paging means (§8.3)
            move_selection(static_cast<int>(page_row_count()));
            return;
        case Key::PageUp:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focus steps a whole viewport back, which is what paging means (§8.3)
            move_selection(-static_cast<int>(page_row_count()));
            return;
        case Key::Home:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focus lands on the first row, which is what home means (§8.3)
            jump_to(0);
            return;
        case Key::End:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focus lands on the last row, which is what end means (§8.3)
            jump_to(m_item_count == 0 ? 0 : m_item_count - 1);
            return;
        case Key::Return:
        case Key::Space:
            // the event is claimed, so no ancestor acts on the same key (§8.3)
            event.handled = true;
            // the focused row is activated, which is what return and space both mean (§18.1)
            activate();
            return;
        default:
            // every other key belongs to a parent, so the list does not claim it (§8.3)
            return;
        }
    }

    SemanticNode ListBox::semantic_node() const {
        // the role is a list, because the rows below it share one selection (§10.1)
        SemanticNode node{SemanticRole::List};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the name is the author identity, because a list carries no text of its own (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(m_bounds);
        // a disabled list reports the disabled state, which is what refuses navigation (§10.1)
        node.set_disabled(disabled());
        // the list is not checked, because the selected state belongs to its rows and not to it (§10.1)
        node.set_check(CheckState::NotCheckable);
        // the navigation actions mirror the keys, because automation drives what the keyboard drives (§10.1, §10.3)
        (void)node.add_action("select_next", [list = mutable_owner(this)] { list->move_selection(1); });
        // the matching action steps the selection back, which completes the pair a client expects (§10.1)
        (void)node.add_action("select_previous", [list = mutable_owner(this)] { list->move_selection(-1); });
        // the first row is a jump rather than a step, which is what home means as an action (§10.1, §8.3)
        (void)node.add_action("select_first", [list = mutable_owner(this)] { list->jump_to(0); });
        // the last row follows the item count, which the list knows and a client does not (§10.1)
        (void)node.add_action("select_last", [list = mutable_owner(this)] {
            // a list with no row has no last row, so the action does nothing (§10.1)
            if (list->m_item_count > 0)
                list->jump_to(list->m_item_count - 1);
        });
        // the focused row is activated, which is the action a screen reader invokes to open a row (§10.1, §18.1)
        (void)node.add_action("activate", [list = mutable_owner(this)] { list->activate(); });
        return node;
    }

    std::size_t ListBox::semantic_child_count() const {
        // the materialized rows are the children, because only the built ones exist to announce (§10.1, AD-13)
        return m_scroll->materialized_row_count();
    }

    std::unique_ptr<SemanticProvider> ListBox::semantic_child(std::size_t index) const {
        // a position past the built rows names no child, which is what the lazy walk expects (§10.1, AD-09)
        UiNode* row = m_scroll->row_node(index);
        if (row == nullptr)
            return nullptr;
        // the item a row shows is offset by the first built one, because the window can start mid list (§8.2, AD-13)
        const std::size_t item = m_scroll->first_row_index() + index;
        // the row provider is wrapped rather than copied, so the ui tree stays the only owner (§10.1, AD-09)
        const auto* row_provider = dynamic_cast<const SemanticProvider*>(row);
        // the base node comes from the row itself, so its identity survives the decoration (§10.1)
        SemanticNode node = row_provider != nullptr ? row_provider->semantic_node() : SemanticNode{SemanticRole::ListItem};
        // the role is overwritten to a list item, because the row is what the list announces (§10.1)
        node.set_role(SemanticRole::ListItem);
        // the selected state follows the model's index, which is what a screen reader announces (§10.1)
        node.set_selected(!m_selection.empty() && m_selection.selected_index() == item);
        // the focused state follows the internal focus, which is kept apart from the selection (D4, §10.1)
        node.set_focused(item == m_focused);
        // the activate action selects and raises the row, which is the same path the return key takes (§10.1, §18.1)
        (void)node.add_action("activate", [list = mutable_owner(this), item] {
            // the row is selected first, so an activation moves the selection onto it (§18.3)
            list->jump_to(item);
            // the row is then raised, which is what the curated activate signal reports (§18.1)
            list->activate();
        });
        return std::make_unique<ListItemProvider>(std::move(node), row_provider);
    }

    ScrollView& ListBox::scroll_view() { return *m_scroll; }

    void ListBox::paint_content(RenderContext& context) {
        // without a theme there is no palette to draw a selection from, so nothing is drawn (§6.1)
        const Theme* installed = theme();
        if (installed == nullptr)
            return;
        // a row height turns the selected index into a place, and without one nothing can be placed (§8.2)
        const float height = m_scroll->row_height();
        if (height <= 0.0f)
            return;
        // the viewport is what the rows are drawn inside, so the band is clipped to it (§6.2)
        const Rect viewport = m_scroll->bounds();
        if (viewport.empty())
            return;
        // a list with no selection draws no band, because there is no row to mark (§18.3)
        if (m_selection.empty())
            return;
        context.save();
        context.clip_rect(viewport);
        // the selected row sits at its index below the viewport top, moved by the scroll offset (§8.2)
        const float top = viewport.y - m_scroll->offset() + static_cast<float>(m_selection.selected_index()) * height;
        // the highlight is the theme's selection token, which is what a selected row is read through (§6.1)
        const Color fill = installed->palette().text_field_selection[state_index(ControlState::Normal)];
        // the band is rounded like the native selection, which reads as a highlight rather than a bar (§6.1)
        context.draw_rounded_rect(Rect::from_origin_size(Point{.x = viewport.x, .y = top}, Size{.width = viewport.width, .height = height}), installed->metrics().corner_radius, Paint{.fill = fill});
        context.restore();
    }

    void ListBox::set_item_count(std::size_t count) {
        // the count is stored first, so the clamp below reads the new range (§8.2)
        m_item_count = count;
        // the focus is clamped into the new range, because the old row may no longer exist (§8.3)
        if (m_focused >= m_item_count)
            m_focused = m_item_count == 0 ? 0 : m_item_count - 1;
        // a selection past the end is dropped, because the row it named is gone (§18.3)
        if (!m_selection.empty() && m_selection.selected_index() >= m_item_count)
            m_selection.clear();
        // a selection asked for before the rows existed settles now, which is what a declared `selected` needs (§18.3)
        if (m_item_count > 0 && m_has_pending_selection) {
            // the asked row is applied through the ordinary path, which clamps and reveals it (§18.3)
            const std::size_t index = m_pending_selection;
            m_has_pending_selection = false;
            select(index);
        }
        // the announced rows changed, so the frame is redrawn (§10.1)
        invalidate();
    }

    void ListBox::set_key_reader(model::ItemKey (*reader)(void* context, std::size_t index), void* context) {
        // the plain reader is stored, because a list cannot capture the row identity on its own (§13.3)
        m_key_reader = reader;
        // the context travels with the reader, because a plain function pointer reads no member (§13.3)
        m_key_context = context;
    }

    void ListBox::select(std::size_t index) {
        // a list with no row has nothing to select, so the call is dropped (§18.3)
        if (m_item_count == 0)
            return;
        // the index is clamped into the range, because a caller may name a stale position (§18.3)
        const std::size_t clamped = std::min(index, m_item_count - 1);
        // the focus moves first, so the announced row carries the focused state the selection lands on (D4)
        m_focused = clamped;
        // the viewport follows the selected row, because a selection made from code should be visible (§8.2)
        reveal(clamped);
        // the selection is written through the model, which is where a change is reported once (§18.3)
        m_selection.select(key_at(clamped), clamped);
        // the selected and focused rows changed, so the frame is redrawn (§10.1)
        invalidate();
    }

    void ListBox::set_selected_index(std::size_t index) {
        // a list that holds no row yet remembers the ask, because a declared selection precedes its rows (§18.3)
        if (m_item_count == 0) {
            // the asked row is stored, which set_item_count applies once the rows exist (§18.3)
            m_pending_selection = index;
            m_has_pending_selection = true;
            return;
        }
        // the generated property shares the imperative path, so both write one selection (§11.3, §18.3)
        select(index);
    }

    void ListBox::select(const model::ItemKey& key) {
        // a list without a reader cannot map an identity back to a row, so the call is dropped (§18.1)
        if (m_key_reader == nullptr)
            return;
        // the item range is scanned, because a plain reader is the only mapping from identity to index (§18.1)
        for (std::size_t index = 0; index < m_item_count; ++index) {
            // the row whose identity matches is the one to select, and the scan stops there (§18.1)
            if (key_at(index) == key) {
                select(index);
                return;
            }
        }
    }

    void ListBox::clear_selection() {
        // the model owns the state, so clearing goes through it, which is where the change is reported (§18.3)
        m_selection.clear();
    }

    bool ListBox::has_selection() const { return !m_selection.empty(); }

    std::size_t ListBox::selected_index() const { return m_selection.selected_index(); }

    const model::ItemKey& ListBox::selected_key() const { return m_selection.selected_key(); }

    std::size_t ListBox::focused_index() const { return m_focused; }

    void ListBox::set_focused_index(std::size_t index) {
        // a list with no row has no row to focus, so the call is dropped (§8.3)
        if (m_item_count == 0)
            return;
        // the index is clamped into the range, because a caller may name a stale position (§8.3)
        m_focused = std::min(index, m_item_count - 1);
        // the focused row carries the focused state, so the frame is redrawn (§10.1)
        invalidate();
    }

    void ListBox::set_viewport_extent(Size size) {
        // the extent is the internal view's decision, so it travels straight through (§8.2)
        m_scroll->set_viewport_extent(size);
    }

    void ListBox::set_row_height(float pixels) {
        // the row height is the internal view's decision, so it travels straight through (§8.2, AD-13)
        m_scroll->set_row_height(pixels);
    }

    float ListBox::row_height() const {
        // the internal view owns the row height, because it is the thing that builds the rows (§8.2, AD-13)
        return m_scroll->row_height();
    }

    void ListBox::set_scrollbar_visibility(ScrollbarVisibility visibility) {
        // the bar policy is the internal view's decision, so it travels straight through (§8)
        m_scroll->set_scrollbar_visibility(visibility);
    }

    void ListBox::set_overscan(float pixels) {
        // the overscan is the internal view's decision, so it travels straight through (§8.2, AD-13)
        m_scroll->set_overscan(pixels);
    }

    void ListBox::set_on_selection_changed(std::function<void()> handler) {
        // the work is swapped in place, because a later change must reach the new work (§18.3)
        m_on_selection_changed = std::move(handler);
    }

    void ListBox::set_on_activate(std::function<void(const std::string&)> handler) {
        // the work is swapped in place, because a later activation must reach the new work (§18.1)
        m_on_activate = std::move(handler);
    }

    void ListBox::set_disabled(bool disabled) {
        // an unchanged state changes nothing, because the list answers the same keys (§8.3)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so the dispatch and the internal view read the same value (§8.3)
        m_disabled = disabled;
        // the internal view follows, because it refuses the wheel and the pointer the same way (§8.3)
        m_scroll->set_disabled(disabled);
        // a disabled list stops answering keys, so the frame changes (§6.1)
        invalidate();
    }

    bool ListBox::disabled() const {
        // the stored flag is what the dispatch refuses on, which is the list's own state (§8.3)
        return m_disabled;
    }

    void ListBox::move_selection(int delta) {
        // a list with no row has nothing to move onto, so the move is dropped (§8.3)
        if (m_item_count == 0)
            return;
        // the arithmetic runs in a signed width, so a delta past either end cannot wrap (§8.3)
        const long current = static_cast<long>(m_focused);
        // the bound is signed too, because the comparison below is against a negative floor (§8.3)
        const long count = static_cast<long>(m_item_count);
        // the moved position is the current one stepped by the signed delta (§8.3)
        const long moved = current + delta;
        // the destination is clamped into the range, which is what stops a page move overshooting (§8.3)
        const long clamped = moved < 0 ? 0 : (moved >= count ? count - 1 : moved);
        // the clamped destination is what the focus and the selection both land on (§8.3)
        jump_to(static_cast<std::size_t>(clamped));
    }

    void ListBox::jump_to(std::size_t index) {
        // a list with no row has nothing to land on, so the jump is dropped (§8.3)
        if (m_item_count == 0)
            return;
        // the destination is clamped into the range, because home and page moves can overshoot (§8.3)
        const std::size_t clamped = std::min(index, m_item_count - 1);
        // the focus moves first, because the row it names is what the selection follows (D4, §8.3)
        m_focused = clamped;
        // the viewport follows the focused row, so a keyboard move scrolls it into view (§8.2)
        reveal(clamped);
        // the selection follows the focus on a keyboard move, which is what a single selection list does (D4, §18.3)
        m_selection.select(key_at(clamped), clamped);
        // the focused row carries the focused state, so the frame is redrawn (§10.1)
        invalidate();
    }

    void ListBox::reveal(std::size_t index) {
        // the row height is what places a row, and without one no row has a position (§8.2, AD-13)
        const float height = m_scroll->row_height();
        if (height <= 0.0f)
            return;
        // the top of the row is the index times the height, which is the placement the view builds with (§8.2)
        const float top = static_cast<float>(index) * height;
        // the bottom of the row is one height below its top, because a row is one row tall (§8.2)
        const float bottom = top + height;
        // the viewport the list drives is the whole of its bounds, because the scroll view is the only child (§8.2)
        const float view_top = m_scroll->offset();
        // the viewport bottom follows from the top, because the offset is the scroll position (§8.2)
        const float view_bottom = view_top + m_scroll->bounds().height;
        // a row above the viewport scrolls it up so the row's top becomes the new viewport top (§8.2)
        if (top < view_top)
            m_scroll->scroll_to(top);
        // a row below the viewport scrolls it down so the row's bottom meets the viewport bottom (§8.2)
        else if (bottom > view_bottom)
            m_scroll->scroll_to(bottom - m_scroll->bounds().height);
    }

    void ListBox::activate() {
        // a list with no row has nothing to activate, so nothing is raised (§18.1)
        if (m_item_count == 0)
            return;
        // the focused row is the one a plain activation means, because the keyboard moved onto it (§18.1)
        const std::size_t index = std::min(m_focused, m_item_count - 1);
        // the activation selects the row, so a return press both moves and chooses (§18.3)
        m_selection.select(key_at(index), index);
        // the row is revealed, because activation may follow a jump that has not scrolled yet (§8.2)
        reveal(index);
        // the installed work is told with the row identity text, which is what an activation carries (§18.1)
        if (m_on_activate)
            m_on_activate(key_at(index).text());
    }

    std::size_t ListBox::page_row_count() const {
        // the row height is what a page counts in, and without one a page is a single row (§8.2, AD-13)
        const float height = m_scroll->row_height();
        if (height <= 0.0f)
            return 1;
        // the rows a viewport covers is the viewport height divided by the row height (§8.2)
        const float rows = m_scroll->bounds().height / height;
        // a page moves at least one row, so a viewport shorter than a row still steps (§8.3)
        return rows < 1.0f ? 1 : static_cast<std::size_t>(rows);
    }

    model::ItemKey ListBox::key_at(std::size_t index) const {
        // a row without a reader has no identity, which is the empty key the model reports (§18.1, AD-11)
        if (m_key_reader == nullptr)
            return model::ItemKey{};
        // the reader is plain, so the context it was bound with travels back to it (§13.3)
        return m_key_reader(m_key_context, index);
    }

    std::optional<std::size_t> ListBox::item_at(Point point) const {
        // a row height turns an index into a position, and without one no row has a position (§8.2, AD-13)
        const float height = m_scroll->row_height();
        if (height <= 0.0f)
            return std::nullopt;
        // the viewport is where the rows are drawn, so a point outside it lands on no row (§8.3)
        const Rect viewport = m_scroll->bounds();
        if (viewport.empty())
            return std::nullopt;
        // a point outside the viewport is past the drawn rows, so it names nothing the list can select (§8.3)
        if (point.x < viewport.x || point.x >= viewport.x + viewport.width)
            return std::nullopt;
        if (point.y < viewport.y || point.y >= viewport.y + viewport.height)
            return std::nullopt;
        // the content sits at the offset, so the local position adds the scroll back to the point (§8.2)
        const float local = point.y - viewport.y + m_scroll->offset();
        // the row is the local position divided by the row height, floored so a point inside a row lands on it (§8.2)
        const long row = static_cast<long>(std::floor(local / height));
        // a position above the first row names nothing, which only a negative offset could produce (§8.2)
        if (row < 0)
            return std::nullopt;
        // the item index offsets the row by the first built one, because the window can start mid list (§8.2, AD-13)
        const std::size_t index = m_scroll->first_row_index() + static_cast<std::size_t>(row);
        // a row past the item count is empty space below the last row, which names nothing to select (§8.3)
        if (index >= m_item_count)
            return std::nullopt;
        return index;
    }
} // namespace albor
