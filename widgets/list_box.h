#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/geometry.h"
#include "model/item_key.h"
#include "model/selection_model.h"
#include "widgets/scroll_view.h"
#include "widgets/widget.h"

namespace albor
{
    // the list control that owns one selection and navigates it by keyboard over an internal scroll view (§8, §18.3)
    class ListBox final : public Widget
    {
    public:
        // build a list over the engine that settles its properties, composing an empty scroll view (§8, §8.2)
        explicit ListBox(BindingEngine* engine, std::string explicit_id = {});

        // override: measure the internal viewport, because the list never grows with its rows (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the internal viewport, because the rows live below the scroll view (§8.2)
        void arrange(const Rect& bounds) override;

        // override: claim the navigation and activation keys, moving the selection before the view sees them (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce a list, whose item children carry the selected and focused states (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: draw the selection highlight under the rows, which is what shows a selection (§6.1, §18.3)
        void paint_content(RenderContext& context) override;

        // override: expose one child per materialized row, which the list overlays its states onto (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: build the row at a materialized position, carrying its selected, focused and activate (§10.1)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // hand out the internal scroll view, which the caller adopts the row content into (§8.2)
        [[nodiscard]] ScrollView& scroll_view();

        // install the row count, which bounds navigation and keys the selection (§8.2, §18.1)
        void set_item_count(std::size_t count);

        // install the plain key reader, which maps an index to the identity a row is keyed by (§18.1, §13.3)
        void set_key_reader(model::ItemKey (*reader)(void* context, std::size_t index), void* context);

        // select the row at an index, which replaces any previous selection (§18.3)
        void select(std::size_t index);

        // select the row at an index as the generated code writes it, which is the property setter of `selected` (§11.3)
        void set_selected_index(std::size_t index);

        // select the row carrying an identity, which finds it by scanning the item range (§18.1, AD-11)
        void select(const model::ItemKey& key);

        // drop the selection, leaving no row selected (§18.3)
        void clear_selection();

        // report whether any row is selected, which the semantic tests read (§18.3)
        [[nodiscard]] bool has_selection() const;

        // hand out the selected index, which is the model sentinel while nothing is selected (§18.3)
        [[nodiscard]] std::size_t selected_index() const;

        // hand out the selected identity, which is the empty key while nothing is selected (AD-11)
        [[nodiscard]] const model::ItemKey& selected_key() const;

        // hand out the internally focused row, which is kept apart from the selection (D4, §8.3)
        [[nodiscard]] std::size_t focused_index() const;

        // move the internal focus onto a row, which the selection does not have to follow (D4, §8.3)
        void set_focused_index(std::size_t index);

        // delegate the declared viewport extent to the internal view, which is what it draws (§8.2)
        void set_viewport_extent(Size size);

        // delegate the row height to the internal view, which is what turns an index into a position (§8.2, AD-13)
        void set_row_height(float pixels);

        // hand out the row height the internal view was given, which a page move divides by (§8.2)
        [[nodiscard]] float row_height() const;

        // delegate the scrollbar policy to the internal view, which is a per instance decision (§8)
        void set_scrollbar_visibility(ScrollbarVisibility visibility);

        // delegate the overscan to the internal view, which prefetches a band of rows (§8.2, AD-13)
        void set_overscan(float pixels);

        // install the work told about every change of the selection, which is what the signal reports (§18.3)
        void set_on_selection_changed(std::function<void()> handler);

        // install the work told when a row is activated, which carries the identity text the row is keyed by (§18.1)
        void set_on_activate(std::function<void(const std::string&)> handler);

        // set whether the list refuses input, which the internal view follows (§8.3)
        void set_disabled(bool disabled);

        // override: report whether the list refuses input, which navigation and activation both read (§8.3)
        [[nodiscard]] bool disabled() const override;

    private:
        // move the focused row by a signed delta and carry the selection with it, which the arrows and pages share (§8.3)
        void move_selection(int delta);

        // move onto an absolute row and carry the selection with it, which home and end share (§8.3)
        void jump_to(std::size_t index);

        // scroll the internal view so the given row intersects the viewport (§8.2)
        void reveal(std::size_t index);

        // activate the focused row, which selects it and raises the activation signal (§18.1)
        void activate();

        // hand out how many rows one viewport covers, which a page move steps by (§8.3)
        [[nodiscard]] std::size_t page_row_count() const;

        // build the identity of the row at an index, which is the empty key past the end (§18.1)
        [[nodiscard]] model::ItemKey key_at(std::size_t index) const;

        // hand out the item a point lands on, which a press maps through the viewport geometry (§8.2, §8.3)
        [[nodiscard]] std::optional<std::size_t> item_at(Point point) const;

        // the owned child scroll view, which is the viewport the list drives (§8.2)
        ScrollView* m_scroll = nullptr;

        // the single selection this list owns, which the model reports changes through (§18.3)
        model::SelectionModel m_selection;

        // the internally focused row, which is stored apart from the selection (D4, §8.3)
        std::size_t m_focused = 0;

        // the rows the list hosts, which bounds navigation (§8.2)
        std::size_t m_item_count = 0;

        // the row asked for before the list held any, because a declared selection precedes its rows (§18.3)
        std::size_t m_pending_selection = 0;

        // whether a row was asked for before the list held any, which set_item_count then settles (§18.3)
        bool m_has_pending_selection = false;

        // the plain reader that names the identity of a row, which the list cannot capture itself (§13.3)
        model::ItemKey (*m_key_reader)(void* context, std::size_t index) = nullptr;

        // the context the plain reader reads, which it cannot capture on its own (§13.3)
        void* m_key_context = nullptr;

        // the work told about every change of the selection, which is what the signal reports (§18.3)
        std::function<void()> m_on_selection_changed;

        // the work told when a row is activated, which carries the identity text (§18.1)
        std::function<void(const std::string&)> m_on_activate;

        // whether the list refuses input, which navigation and activation both read (§8.3)
        bool m_disabled = false;
    };
} // namespace albor
