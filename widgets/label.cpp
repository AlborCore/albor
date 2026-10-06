#include "widgets/label.h"

#include <utility>

namespace albor
{
    Label::Label(BindingEngine* engine, TextEngine* text_engine, std::string text, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_text(std::move(text)) {
        // a label with no engine cannot measure, so it stays empty rather than inventing an extent (§9.1)
        if (m_text_engine == nullptr)
            return;
        // the run is measured once up front, so a label is sized before the first layout pass reaches it (§9.1)
        m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size);
    }

    void Label::measure(const SizeConstraints& constraints, Size& out_size) {
        // a label with no engine keeps whatever it measured, because it cannot measure at all (§9.1)
        if (m_text_engine != nullptr) {
            // the offered width bounds the run, because a label wraps inside the space its parent gives it (§8.2, §9.1)
            const float offered_width = constraints.maximum.width > 0.0f ? constraints.maximum.width : 0.0f;
            // the run is measured at the current font size, because the metrics follow the size (§9.1)
            m_layout = m_text_engine->measure(m_text, offered_width, m_font_size);
        }
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(m_layout.size);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Label::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the preferred size follows the arranged one, because the parent has now decided (§8.2)
        m_preferred_size = m_measured_size;
    }

    void Label::render(RenderContext& context) {
        // an empty run carries no glyphs, so recording it would draw nothing (§6.2)
        if (m_layout.empty())
            return;
        // bounds
        const Rect& b = bounds();
        // the run is recorded at the baseline, which sits one ascent below the top of the assigned bounds (§6.2, §9.1)
        context.draw_text(m_layout, Point{.x = b.x, .y = b.y + m_layout.ascent}, {.fill = m_foreground});
    }

    SemanticNode Label::semantic_node() const {
        // the role is a label, because the run is static descriptive text (§10.1)
        SemanticNode node{SemanticRole::Label};
        // the accessible name is the run itself, because that is what a screen reader must speak (§10.1)
        node.set_name(m_text);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a hidden label announces nothing, which is what the visible state means to assistive technology (§10.1)
        node.mutable_states().disabled = !visible();
        return node;
    }

    void Label::set_text(std::string text) {
        // an unchanged run redraws nothing, because the label looks the same (§9.1)
        if (m_text == text)
            return;
        // the text is stored first, so the invalidation walks a label that already carries the new run (§6.1)
        m_text = std::move(text);
        // a label with no engine has nothing to remeasure, so its run stays as it was (§9.1)
        if (m_text_engine != nullptr)
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size);
        // a new run changes the measured extent, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    const std::string& Label::text() const {
        // the drawn text is what the semantic node announces as the name (§10.1)
        return m_text;
    }

    void Label::set_foreground(Color color) {
        // an unchanged colour redraws nothing, because the label looks the same (§6.1)
        if (m_foreground == color)
            return;
        // the colour is stored first, so the invalidation walks a label that already paints the new colour (§6.1)
        m_foreground = color;
        // a colour change is visual only, so the layout passes are left alone (§6.1)
        invalidate();
    }

    Color Label::foreground() const {
        // the fill colour is what the render pass records (§6.1)
        return m_foreground;
    }

    void Label::set_font_size(float font_size) {
        // an unchanged size remeasures nothing, because the metrics did not change (§9.1)
        if (m_font_size == font_size)
            return;
        // the size is stored first, so the invalidation walks a label that already knows its metrics (§9.1)
        m_font_size = font_size;
        // a label with no engine has nothing to remeasure, so its run stays as it was (§9.1)
        if (m_text_engine != nullptr)
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size);
        // a font size change resizes the label, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    float Label::font_size() const {
        // the size is what the run was last measured at (§9.1)
        return m_font_size;
    }

    const TextLayout& Label::layout() const {
        // the run is what the render pass draws and what the measure pass produced (§9.1)
        return m_layout;
    }
} // namespace albor
