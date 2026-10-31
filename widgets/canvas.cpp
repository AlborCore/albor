#include <algorithm>

#include "widgets/canvas.h"

#include "text/text_engine.h"

namespace albor
{
    Canvas::Canvas(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    Canvas::Canvas(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine) {}

    void Canvas::measure(const SizeConstraints& constraints, Size& out_size) {
        // the declared extent is what the canvas offers, because a callback cannot be asked what it will draw (§8.2)
        Size content = m_intrinsic_size;
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

    void Canvas::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void Canvas::paint_content(RenderContext& context) {
        // the pass is counted before the callback runs, so a canvas that draws nothing still records that it was asked (§6.4)
        m_draw_count++;
        // an empty canvas draws nothing, because there is no callback to draw with (§6.4)
        if (!m_draw)
            return;
        // the callback records into the stage one context, which is what keeps the frame immutable (§6.1, AD-18)
        m_draw(context);
    }

    void Canvas::dispatch_event(Event& event) {
        // a hidden canvas takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run first, so a press is a state before it reaches the callback (§8.3)
        handle_hover_transitions(event);
        // a canvas has no activation of its own, so every event belongs to whatever is behind it (§8.3)
        UiNode::dispatch_event(event);
    }

    Color Canvas::background() const {
        // a themed canvas resolves by state, so a hover and a press read without a second literal (§6.1)
        if (theme() != nullptr)
            return resolve(plate_colors(), Color::rgba(0.0f, 0.0f, 0.0f, 0.0f));
        // an unthemed canvas keeps its own plate, which is the transparent default it always had (§6.1)
        return Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const Color* Canvas::plate_colors() const {
        // the palette answers, because the caller already established that a theme is installed (§6.1)
        return theme()->palette().canvas_background;
    }

    SemanticNode Canvas::semantic_node() const {
        // the role is a canvas, because the user may interact with what is drawn (§10.1)
        SemanticNode node{SemanticRole::Canvas};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the accessible name is the identity the author assigned, because a canvas carries no text (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // a canvas that draws nothing is announced as disabled, because there is nothing to interact with (§10.1)
        node.set_disabled(!draws());
        return node;
    }

    void Canvas::set_draw(std::function<void(RenderContext&)> draw) {
        // the callback is swapped in place, because the next pass must run the new work (§6.4)
        m_draw = std::move(draw);
        // the content changed, so the next frame must be built (§6.1)
        invalidate();
    }

    bool Canvas::draws() const {
        // a canvas with no callback records nothing, so there is no canvas to interact with (§6.4)
        return static_cast<bool>(m_draw);
    }

    void Canvas::set_intrinsic_size(Size size) {
        // an unchanged extent remeasures nothing, because the layout did not change (§8.2)
        if (m_intrinsic_size == size)
            return;
        // the extent is stored first, so the invalidation walks a canvas that already knows its new size (§8.2)
        m_intrinsic_size = size;
        // a new extent changes what the canvas fills, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    Size Canvas::intrinsic_size() const {
        // the declared extent is what the measure pass used (§8.2)
        return m_intrinsic_size;
    }

    void Canvas::set_text_engine(TextEngine* text_engine) {
        // the engine is borrowed rather than owned, because the composition root outlives the canvas (§9.1)
        m_text_engine = text_engine;
    }

    TextEngine* Canvas::text_engine() const {
        // a canvas whose host installed no engine reports that, which the draw block checks (§9.1)
        return m_text_engine;
    }

    TextLayout Canvas::measure_text(std::string_view text, float font_size) const {
        // without an engine nothing can be shaped, so the empty layout stands in rather than guessing (§9.1)
        if (m_text_engine == nullptr)
            return {};
        // the run is measured unbounded, because a draw block places each run itself (§9.1)
        return m_text_engine->measure(text, TextEngine::UNBOUNDED_WIDTH, font_size);
    }

    std::uint64_t Canvas::draw_count() const {
        // the count is how many times the callback was offered a context (§6.4)
        return m_draw_count;
    }
} // namespace albor
