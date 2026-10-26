#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace albor
{
    // one page of a tab view, which the view owns as a child (§8)
    class TabPage;

    // the container that shows one page at a time and owns the single selection of its tabs (§8)
    class TabView final : public Widget
    {
    public:
        // build an empty tab view over the engine that settles its properties (§8)
        explicit TabView(BindingEngine* engine, std::string explicit_id = {});

        // override: measure the strip and the selected page, because only one page contributes to the extent (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the strip and every page under it, showing only the selected page (§8.2)
        void arrange(const Rect& bounds) override;

        // override: draw the strip and the labels, which is what the tabs look like (§6.1)
        void paint_content(RenderContext& context) override;

        // override: claim a press on a tab and the keys that move the selection (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the tab group and the actions that move its selection (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: expose one child per tab page, which is the group's semantic shape (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: build the page at a position, which is what the reader walks (§10.1)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // override: keep the selected tab item's focused state in step with the view (§8.3, §10.1)
        void set_focused(bool focused) override;

        // replace the selected page, which is the one the view shows (§8)
        void set_selected(int index);

        // hand out the selected page, which the view shows (§8)
        [[nodiscard]] int selected() const;

        // set whether the strip runs down the left side, which is what a vertical tab control is (§8)
        void set_vertical(bool vertical);

        // report whether the strip runs down the left side (§8)
        [[nodiscard]] bool vertical() const;

        // hand out how many tab pages the view holds, which the strip draws (§8)
        [[nodiscard]] std::size_t tab_count() const;

        // hand out one tab page, or null past the end (§8)
        [[nodiscard]] TabPage* tab_at(std::size_t index) const;

        // install the work told about every change of the selection, which is what the signal reports (§8.3)
        void set_on_selection_changed(std::function<void(int)> handler);

    private:
        // rebuild the tab pointer list when the child count changes, which keeps the strip current (§8.1)
        void sync_tabs() const;

        // show the selected page and hide the others, which is what the selection means (§8)
        void apply_selection();

        // select the page a press named, which is the only input a strip item carries (§8.3)
        void select_tab(TabPage* tab);

        // measure every label and settle the width one tab takes, which the strip reads (§8.2, §9.1)
        void compute_header_widths(float font_size) const;

        // raise the pending structural selection change, which the model follows once (§8.3)
        void raise_structure_selection();

        // hand out the rectangle a title takes inside its header (§8.2)
        [[nodiscard]] Rect title_bounds(const Rect& header, const Size& title_size) const;

        // hand out the selected page, which the measure pass and the paint pass share (§8.2)
        [[nodiscard]] TabPage* selected_tab() const;

        // hand out the selected index, clamped to the pages the view currently holds (§8)
        [[nodiscard]] int effective_selected() const;

        // hand out the label colour of a tab, which the selected page carries the accent for (§6.1)
        [[nodiscard]] Color tab_label_color(bool selected) const;

        // hand out the colour of the indicator under the selected tab (§6.1)
        [[nodiscard]] Color tab_indicator_color() const;

        // hand out the strip thickness and the tab insets, which the theme owns when one is installed (§8.2)
        [[nodiscard]] float strip_thickness() const;

        // hand out the inset on either side of a tab label (§8.2)
        [[nodiscard]] float tab_padding() const;

        // hand out the smallest width one tab takes (§8.2)
        [[nodiscard]] float tab_minimum_width() const;

        // hand out the thickness of the selected tab indicator (§6.1)
        [[nodiscard]] float indicator_height() const;

        // hand out the size a tab label is measured at (§9.1)
        [[nodiscard]] float label_font_size() const;

        // the tab pages the view found among its children, which the strip draws in order (§8.1)
        mutable std::vector<TabPage*> m_tabs;

        // the width one tab takes, which the label and the theme inset settle (§8.2)
        mutable std::vector<float> m_tab_widths;

        // the child count the tab list was built from, which keeps the rebuild off the steady frame (§8.1)
        mutable std::size_t m_synced_children = static_cast<std::size_t>(-1);

        // whether the widths need measuring again, which a structural change sets (§8.2)
        mutable bool m_widths_dirty = true;

        // the page the view shows, which a press or a binding replaces (§8)
        mutable int m_selected = 0;

        // whether a structural change moved the selection, which the next arrange reports (§8.3)
        mutable bool m_structure_selection_changed = false;

        // whether the strip runs down the left side, which is what a vertical tab control is (§8)
        bool m_vertical = false;

        // the work told about every change of the selection, which is what the signal reports (§8.3)
        std::function<void(int)> m_on_selection_changed;
    };
} // namespace albor
