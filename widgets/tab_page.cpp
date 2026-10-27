#include "widgets/tab_page.h"

#include <algorithm>
#include <utility>

#include "widgets/tab_title.h"

namespace albor
{
    TabPage::TabPage(BindingEngine* engine, TextEngine* text_engine, std::string label, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_label(std::move(label)) {}

    void TabPage::measure(const SizeConstraints& constraints, Size& out_size) {
        // the title is found before the page content is measured, because the strip owns it (§8)
        sync_title();
        // the page is as large as the largest content child, because the title is not page content (§8.2)
        Size total{};
        // the offered ceiling travels down, because a parent knows the space the page has to fit in (§8.2)
        const SizeConstraints child_constraints = SizeConstraints::at_most(constraints.maximum);
        for (std::size_t index = 0; index < child_count(); index++) {
            // the title is placed by the strip, so it contributes no page extent (§8)
            UiNode* child = child_at(index);
            if (child == nullptr || child == m_title)
                continue;
            // each content child measures against the page ceiling, which is the container contract (§8.2)
            Size child_size{};
            child->measure(child_constraints, child_size);
            // the page is at least as large as its largest content child (§8.2)
            total.width = std::max(total.width, child_size.width);
            total.height = std::max(total.height, child_size.height);
        }
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void TabPage::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, which the page content is placed inside (§8.2)
        m_bounds = bounds;
        // the title is found before the content is placed, because the strip places it (§8)
        sync_title();
        for (std::size_t index = 0; index < child_count(); index++) {
            // the title is placed by the strip, so the page leaves its bounds alone (§8)
            UiNode* child = child_at(index);
            if (child == nullptr || child == m_title)
                continue;
            // the content child takes the whole page, which is the container contract (§8.2)
            child->arrange(bounds);
        }
        // the arranged extent is the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    void TabPage::paint_children(RenderContext& context) {
        // the title is found before the content is drawn, because the strip draws it (§8)
        sync_title();
        for (std::size_t index = 0; index < child_count(); index++) {
            // the title is drawn by the strip, so the page skips it (§8)
            UiNode* child = child_at(index);
            if (child == nullptr || child == m_title || !child->visible())
                continue;
            // the content child draws over the page, which is the composite contract (§6.1)
            child->render(context);
        }
    }

    bool TabPage::accepts_focus() const {
        // the page is a container, so the caret belongs to its content rather than to the page (§8.3)
        return false;
    }

    SemanticNode TabPage::semantic_node() const {
        // the role is a tab item, because this is one page the user chooses from the strip (§10.1)
        SemanticNode node{SemanticRole::TabItem};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the label is what the strip draws and what assistive technology speaks (§10.1)
        node.set_name(m_label);
        // the header bounds are where the user aims, which the page body is not (§10.1)
        node.set_bounds(m_header_bounds.empty() ? bounds() : m_header_bounds);
        // the selected tab is the page the view is showing, which is the state a reader needs (§10.1)
        node.set_selected(m_selected);
        // the focus belongs to the tab view, which the selected item repeats for its reader (§8.3)
        node.set_focused(m_tab_focused);
        // a tab item is not checkable, because it is a single choice rather than a toggle (§10.1)
        node.set_check(CheckState::NotCheckable);
        // a press selects the page, which is the one action a strip item carries (§8.3, §10.1)
        if (m_on_activate)
            (void)node.add_action("activate", [control = mutable_owner(this)] { control->activate(); });
        return node;
    }

    std::size_t TabPage::semantic_child_count() const {
        // a page the view is not showing exposes no content, because a reader cannot see it either (§10.1)
        if (!m_selected)
            return 0;
        // the title is decorative, so it contributes no semantic child to the page (§10.1)
        sync_title();
        std::size_t count = 0;
        for (std::size_t index = 0; index < child_count(); index++) {
            // the title is part of the strip, not of the page content the reader walks (§10.1)
            if (child_at(index) != nullptr && child_at(index) != m_title)
                count++;
        }
        return count;
    }

    std::unique_ptr<SemanticProvider> TabPage::semantic_child(std::size_t index) const {
        // a hidden page exposes no child, which keeps an unselected page out of the reader's walk (§10.1)
        if (!m_selected)
            return nullptr;
        // the title is decorative, so it is skipped when an index is mapped to a content child (§10.1)
        sync_title();
        std::size_t position = 0;
        for (std::size_t child_index = 0; child_index < child_count(); child_index++) {
            // the title is part of the strip, not of the page content the reader walks (§10.1)
            if (child_at(child_index) == nullptr || child_at(child_index) == m_title)
                continue;
            // the requested position names this content child, which the base walker wraps (§10.1)
            if (position == index)
                return Widget::semantic_child(child_index);
            // the next content child is one position further on (§10.1)
            position++;
        }
        // an index past the content children names nothing, which the lazy walk expects (§10.1)
        return nullptr;
    }

    void TabPage::set_label(std::string label) {
        // an unchanged label redraws nothing, because the strip looks the same (§6.1)
        if (m_label == label)
            return;
        // the label is stored first, so the invalidation walks a page that already carries it (§8)
        m_label = std::move(label);
        // the old layout belongs to the old run, so a new measurement replaces it (§9.1)
        m_label_layout = TextLayout{};
        // a wider or narrower label changes the strip, so the layout passes run (§8.2)
        invalidate_layout();
        // the strip draws the new label, so the frame is redrawn too (§6.1)
        invalidate();
    }

    const std::string& TabPage::label() const {
        // the label is what the strip draws and the semantics speak (§10.1)
        return m_label;
    }

    void TabPage::measure_header(float font_size) {
        // the title is found before the header is measured, because the strip draws it instead of the label (§8)
        sync_title();
        // a visible title measures as arbitrary content rather than one run (§8.2)
        if (m_title != nullptr && m_title->visible()) {
            // a new measurement replaces the previous extent, which the strip reads (§8.2)
            m_title_size = Size{};
            // the title measures freely, because the strip gives it the width the label needs (§8.2)
            m_title->measure(SizeConstraints::unconstrained(), m_title_size);
            // the label is not drawn beside a title, so its run is cleared (§9.1)
            m_label_layout = TextLayout{};
            return;
        }
        // a hidden title leaves the label as the header, because the author hid the richer content (§8)
        m_title_size = Size{};
        // a page with no engine cannot measure, so its header stays empty and the strip uses its minimum (§9.1)
        if (m_text_engine == nullptr || m_label.empty()) {
            m_label_layout = TextLayout{};
            return;
        }
        // the label measures as one unbroken run, because a tab header never wraps (§9.1)
        m_label_layout = m_text_engine->measure(m_label, TextEngine::UNBOUNDED_WIDTH, font_size);
    }

    const TextLayout& TabPage::label_layout() const {
        // the strip draws the run the last measurement produced (§9.1)
        return m_label_layout;
    }

    TabTitle* TabPage::title() const {
        // the title is found on demand, because the page is a plain container (§8)
        sync_title();
        return m_title;
    }

    const Size& TabPage::title_size() const {
        // the size is what the last header measurement produced, which the strip reads (§8.2)
        return m_title_size;
    }

    void TabPage::set_header_bounds(const Rect& bounds) {
        // the header is placed by the view, so an unchanged rectangle is left alone (§8.2)
        if (m_header_bounds == bounds)
            return;
        // the header is stored, because the semantic node announces where the user aims (§10.1)
        m_header_bounds = bounds;
    }

    const Rect& TabPage::header_bounds() const {
        // the header is where the strip drew this page's label (§10.1)
        return m_header_bounds;
    }

    void TabPage::set_selected(bool selected) {
        // an unchanged selection redraws nothing, because the page and its strip look the same (§6.1)
        if (m_selected == selected)
            return;
        // the state is stored first, so the invalidation walks a page that already carries it (§8)
        m_selected = selected;
        // a selected page exposes its content, so the frame is redrawn (§6.1)
        invalidate();
    }

    bool TabPage::selected() const {
        // the view and the semantic node both read the selection (§8, §10.1)
        return m_selected;
    }

    void TabPage::set_tab_focused(bool focused) {
        // an unchanged focus changes nothing, because the semantic state already agrees (§8.3)
        if (m_tab_focused == focused)
            return;
        // the state is stored, because the selected tab repeats the focus of its view (§8.3)
        m_tab_focused = focused;
    }

    void TabPage::set_on_activate(std::function<void()> handler) {
        // the view installs the selection work once, which a press then runs (§8.3)
        m_on_activate = std::move(handler);
    }

    void TabPage::activate() {
        // a page with no installed work cannot be selected, which a detached page is (§8.3)
        if (m_on_activate)
            m_on_activate();
    }

    void TabPage::sync_title() const {
        // the direct children are scanned, because a page holds a handful and a stale pointer is never safe (§8.1)
        TabTitle* found = nullptr;
        for (std::size_t index = 0; index < child_count(); index++) {
            // the type decides, because the title is the one child the strip draws (§8)
            if (auto* title = dynamic_cast<TabTitle*>(child_at(index))) {
                found = title;
                break;
            }
        }
        // a changed title resets the old measurement, which a removed or replaced title must not leave behind (§8.2)
        if (found != m_title) {
            m_title = found;
            m_title_size = Size{};
        }
    }
} // namespace albor
