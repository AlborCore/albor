#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // the content one tab draws in the strip, which is measured apart from the page body (§8)
    class TabTitle;

    // one page of a tab view, which carries the label its strip draws and the content it shows when selected (§8)
    class TabPage final : public Widget
    {
    public:
        // build a page over the engines it reads, with an empty label and no selected state (§8)
        TabPage(BindingEngine* engine, TextEngine* text_engine, std::string label = {}, std::string explicit_id = {});

        // override: measure the page content without the title, because the strip owns the title (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: arrange the page content without the title, because the strip places the title (§8.2)
        void arrange(const Rect& bounds) override;

        // override: draw the page content without the title, because the strip draws the title (§8.2)
        void paint_children(RenderContext& context) override;

        // override: announce the page as a tab item with its label and selected state (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: a page node is not a focus stop, because its content takes the caret (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: expose the page content only while the page is selected (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: expose the page child only while the page is selected (§10.1)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // replace the label the strip draws, which is also the accessible name (§8, §10.1)
        void set_label(std::string label);

        // hand out the label the strip draws (§10.1)
        [[nodiscard]] const std::string& label() const;

        // measure the label at the size the tab view draws it, which the strip reads (§8.2, §9.1)
        void measure_header(float font_size);

        // hand out the measured label, which the strip draws and sizes itself by (§9.1)
        [[nodiscard]] const TextLayout& label_layout() const;

        // hand out the title the page carries, or null when the strip draws the label (§8)
        [[nodiscard]] TabTitle* title() const;

        // hand out the size the title measured to, which the strip sizes itself by (§8.2)
        [[nodiscard]] const Size& title_size() const;

        // install the header bounds the tab view assigned, which the semantic node announces (§10.1)
        void set_header_bounds(const Rect& bounds);

        // hand out the header bounds the tab view assigned (§10.1)
        [[nodiscard]] const Rect& header_bounds() const;

        // set whether this page is the selected one, which the semantics and the tab view read (§8, §10.1)
        void set_selected(bool selected);

        // report whether this page is the selected one (§8)
        [[nodiscard]] bool selected() const;

        // set whether the tab view holds the focus, which the selected tab item announces (§8.3, §10.1)
        void set_tab_focused(bool focused);

        // install the work a press performs, which the tab view uses to select the page (§8.3, §10.1)
        void set_on_activate(std::function<void()> handler);

    private:
        // activate the page, which the tab view turns into a selection (§8.3, §10.1)
        void activate();

        // find the title among the direct children, which the strip places apart from the page (§8)
        void sync_title() const;

        // the text engine that measures the label, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the label measures to, because the strip is as wide as the label it carries (§9.1)
        TextLayout m_label_layout;

        // the label the strip draws, which is also the accessible name (§10.1)
        std::string m_label;

        // the bounds of the strip header, which the semantic node announces instead of the page body (§10.1)
        Rect m_header_bounds;

        // the title the page carries, which the strip draws instead of the label (§8)
        mutable TabTitle* m_title = nullptr;

        // the size the title measured to, which the strip reads (§8.2)
        mutable Size m_title_size;

        // whether this page is the selected one (§8)
        bool m_selected = false;

        // whether the tab view holds the focus (§8.3)
        bool m_tab_focused = false;

        // the work a press performs, which the tab view installs (§8.3, §10.1)
        std::function<void()> m_on_activate;
    };
} // namespace albor
