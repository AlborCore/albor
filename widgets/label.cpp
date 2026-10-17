#include "widgets/label.h"

#include <algorithm>
#include <utility>

namespace albor
{
    Label::Label(BindingEngine* engine, TextEngine* text_engine, std::string text, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_text(std::move(text)) {
        // a label with no engine cannot measure, so it stays empty rather than inventing an extent (§9.1)
        if (m_text_engine == nullptr)
            return;
        // the run is measured once up front, so a label is sized before the first layout pass reaches it (§9.1)
        m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size, m_font_spec);
    }

    void Label::measure(const SizeConstraints& constraints, Size& out_size) {
        // a label with no engine keeps whatever it measured, because it cannot measure at all (§9.1)
        if (m_text_engine != nullptr) {
            // the offered width bounds the run, because a label wraps inside the space its parent gives it (§8.2, §9.1)
            const float offered_width = constraints.maximum.width > 0.0f ? constraints.maximum.width : 0.0f;
            // the run is measured at the hierarchy size the role resolved, because the metrics follow it (§9.1, §8.4)
            m_layout = m_text_engine->measure(m_text, offered_width, font_size_used(), m_font_spec);
        }
        // the content is at least the run the label carries, and at least its children (§8.2)
        Size content = m_layout.size;
        // the children are measured too, because a widget is a component and they are part of its content (§8.1, §8.2)
        Size children{};
        UiNode::measure(constraints, children);
        // a widget is at least as large as its own content and as its children (§8.2)
        content.width = std::max(content.width, children.width);
        content.height = std::max(content.height, children.height);
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Label::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void Label::paint_content(RenderContext& context) {
        // an empty run carries no glyphs, so recording it would draw nothing (§6.2)
        if (m_layout.empty())
            return;
        // bounds
        const Rect& b = bounds();
        // the run centres in the box, so a label stretched by a list row lines up on the row's middle (§8.2)
        const float offset_y = std::max(0.0f, (b.height - m_layout.size.height) * 0.5f);
        // the run is recorded at the top of its box, and the colour resolves by role and state (§6.1, §9.1)
        context.draw_text(m_layout, Point{.x = b.x, .y = b.y + offset_y}, {.fill = foreground_color()});
    }

    void Label::dispatch_event(Event& event) {
        // a hidden label takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run through the base, which is where every control inherits hover for free (§8.3)
        handle_hover_transitions(event);
        // a label has no activation of its own, so every event belongs to whatever is behind it (§8.3)
        UiNode::dispatch_event(event);
    }

    SemanticNode Label::semantic_node() const {
        // the role is a label, because the run is static descriptive text (§10.1)
        SemanticNode node{SemanticRole::Label};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
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
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size, m_font_spec);
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
        // an unchanged size may still have to leave a role, because the size decides beneath the role (§8.4)
        if (m_font_size == font_size) {
            // a role resolved the last measure, so leaving it sizes the run back to the explicit value (§8.4)
            if (m_role != TextRole::None) {
                m_role = TextRole::None;
                // the effective metrics change with the role, so the layout passes must run again (§8.4, §8.2)
                invalidate_layout();
            }
            // exit
            return;
        }
        // an explicit size is what the run resolves through again, which is what a chosen size names (§8.4)
        m_role = TextRole::None;
        // the size is stored first, so the invalidation walks a label that already knows its metrics (§9.1)
        m_font_size = font_size;
        // a label with no engine has nothing to remeasure, so its run stays as it was (§9.1)
        if (m_text_engine != nullptr)
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size, m_font_spec);
        // a font size change resizes the label, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    void Label::set_font_family(std::string family) {
        // the family is stored, because the next measure resolves the face through it (§9.1, AD-34)
        m_font_spec.family = std::move(family);
        // a label with no engine has nothing to remeasure, so its run stays as it was (§9.1)
        if (m_text_engine != nullptr)
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size, m_font_spec);
        // a family change may resize the label, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    void Label::set_font_weight(FontWeight weight) {
        // the weight is stored, because the next measure resolves the face through it (§9.1, AD-34)
        m_font_spec.weight = weight;
        // a label with no engine has nothing to remeasure, so its run stays as it was (§9.1)
        if (m_text_engine != nullptr)
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size, m_font_spec);
        // a weight change may resize the label, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    void Label::set_font_fallback(std::vector<std::string> fallback) {
        // the fallback chain is stored, because the next measure resolves the face through it (§9.1, AD-34)
        m_font_spec.fallback = std::move(fallback);
        // a label with no engine has nothing to remeasure, so its run stays as it was (§9.1)
        if (m_text_engine != nullptr)
            m_layout = m_text_engine->measure(m_text, 0.0f, m_font_size, m_font_spec);
        // a fallback change may resize the label, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    float Label::font_size() const {
        // the size is what the run was last measured at (§9.1)
        return m_font_size;
    }

    void Label::set_role(TextRole role) {
        // an unchanged role changes nothing, because the metrics already resolved (§8.4)
        if (m_role == role)
            return;
        // the role is stored first, so the next measure resolves through it (§8.4)
        m_role = role;
        // a role change resizes the run, so the layout passes must run again (§8.4, §8.2)
        invalidate_layout();
    }

    TextRole Label::role() const {
        // the role is what the measure pass resolves through (§8.4)
        return m_role;
    }

    float Label::font_size_used() const {
        // without a theme the run keeps its explicit size, which is the unthemed behaviour (§9.1)
        if (theme() == nullptr || m_role == TextRole::None)
            return m_font_size;
        // a body role takes the size the hierarchy sets body copy at (§8.4)
        switch (m_role) {
        // the body size is where a plain label sits (§8.4)
        case TextRole::Body:
            return theme()->typography().body;
        // the caption size is the smallest hierarchy entry (§8.4)
        case TextRole::Caption:
            return theme()->typography().caption;
        // the headline size is the largest entry (§8.4)
        case TextRole::Headline:
            return theme()->typography().headline;
        // the subheadline size is one step stronger than body copy (§8.4)
        case TextRole::Subheadline:
            return theme()->typography().subheadline;
        // the none role never reaches here, because the explicit size already answered (§8.4)
        default:
            return m_font_size;
        }
    }

    Color Label::foreground_color() const {
        // without a theme the run keeps its own colour, which is the unthemed behaviour (§6.1)
        if (theme() == nullptr)
            return m_foreground;
        // a caption is the secondary role, so it dims through the token the theme carries rather than a literal (§8.4)
        if (m_role == TextRole::Caption)
            return resolve(foreground_colors(), m_foreground).transparentized(1.0f - theme()->typography().secondary_opacity);
        // every other role reads the resolved foreground (§8.4)
        return resolve(foreground_colors(), m_foreground);
    }

    const TextLayout& Label::layout() const {
        // the run is what the render pass draws and what the measure pass produced (§9.1)
        return m_layout;
    }
    const Color* Label::foreground_colors() const {
        // without a theme the table is unused, because the resolver returns the label's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().label_foreground;
    }
} // namespace albor
