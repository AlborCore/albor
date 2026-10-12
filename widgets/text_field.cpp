#include "widgets/text_field.h"

#include <algorithm>
#include <utility>

#include "commands/command.h"

namespace albor
{
    TextField::TextField(BindingEngine* engine, TextEngine* text_engine, std::string value, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)), m_text_engine(text_engine), m_value(std::move(value)) {
        // the runs are laid out once up front, so a field is sized before the first layout pass reaches it (§9.1)
        remeasure();
    }

    void TextField::measure(const SizeConstraints& constraints, Size& out_size) {
        // the metrics are asked for once, because every dimension reads them (§8.2)
        const ControlMetrics& m = metrics();
        // the content is the widest of the value run and the hint run, because an empty field shows the hint (§8.2)
        const float run_width = std::max(m_layout.size.width, m_placeholder_layout.size.width) + m.text_field_padding * 2.0f;
        // the height is the run height, because a field vertically centres the line it draws (§8.2)
        const float run_height = std::max(m_layout.size.height, m_placeholder_layout.size.height);
        // the content is at least the minimum extent the metrics carry, and at least the children (§8.2)
        Size content{.width = std::max(run_width, m.text_field_minimum.width), .height = std::max(run_height, m.text_field_minimum.height)};
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

    void TextField::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void TextField::paint_content(RenderContext& context) {
        // a field with no area draws nothing, which keeps an unarranged field invisible (§6.1)
        if (bounds().empty())
            return;
        // the metrics are asked for once, because the inset and the caret read them (§8.2)
        const ControlMetrics& m = metrics();
        // the text starts one inset in, because the value never touches the plate edge (§8.2)
        const Point origin{.x = bounds().x + m.text_field_padding, .y = 0.0f};
        // the value is what the field shows when it has one, and the hint is what shows beside nothing (§8)
        const bool shows_placeholder = m_value.empty() && !m_placeholder.empty();
        // the run that paints is the one the present state displays (§6.1)
        const TextLayout& displayed = shows_placeholder ? m_placeholder_layout : m_layout;
        // the line hangs vertically centred, because a field is fixed height and the run is its guest (§8.2)
        const float centered_y = bounds().y + std::max(0.0f, (bounds().height - displayed.size.height) * 0.5f);
        // the content is clipped into the plate, because a run wider than the field must not bleed over it (§6.2)
        context.save();
        context.clip_rect(bounds());
        // the hint or the value is recorded at the inset and the centre, which is the whole text paint (§6.1)
        if (!displayed.empty())
            context.draw_text(displayed, Point{.x = origin.x, .y = centered_y}, {.fill = resolve(shows_placeholder ? placeholder_colors() : value_colors(), shows_placeholder ? m_placeholder_foreground : m_foreground)});
        // the caret is drawn only over an editable field, because a plain writer has no bar to stand at (§8.3)
        if (!m_read_only)
            paint_caret(context);
        // the clip is restored, so nothing after the field paints clipped by it (§6.2)
        context.restore();
    }

    void TextField::paint_caret(RenderContext& context) {
        // the rect the caret draws at is asked for as the only geometry, which is what an overlay reads too (§9.1)
        const Rect rect = caret_rect();
        // a caret of no extent draws nothing, which is what an unarranged field carries (§6.2)
        if (rect.empty())
            return;
        // the bar is one rect, because a caret is nothing but a bar (§6.2)
        context.draw_rect(rect, {.fill = resolve(caret_colors(), m_caret_foreground)});
    }

    bool TextField::hit_test(Point logical_point) const {
        // a hidden field is not a hit, because it draws nothing (§8.3)
        if (!visible())
            return false;
        // a disabled field is not a hit, because it refuses input (§8.3)
        if (disabled())
            return false;
        // the press must land inside the arranged bounds, with the far edge excluded so neighbours stay separate (§8.3)
        const Rect& b = bounds();
        const bool inside = logical_point.x >= b.x && logical_point.x < b.x + b.width && logical_point.y >= b.y && logical_point.y < b.y + b.height;
        // a press outside the plate falls through to whatever is behind the field (§8.3)
        return inside && !b.empty();
    }

    void TextField::dispatch_event(Event& event) {
        // a hidden field takes no event at all, because it draws nothing to aim at (§8.3)
        if (!visible())
            return;
        // the transitions run first, so the hover and press flags mean the same thing they mean elsewhere (§8.3)
        handle_hover_transitions(event);
        // a disabled field takes no key and no press, because it refuses everything (§18.7)
        if (disabled())
            return;
        // the keyboard edits come through the key kind, which the focused walk delivers (§8.3)
        if (event.kind == EventKind::KeyDown) {
            // a modified key is an application shortcut, so the plain edits ignore it (§8.3, §18.5)
            if (!event.modifiers.none())
                return;
            // the caret keys are the arrows, the home and the end, which a read only field still moves (§8.3)
            if (event.key == Key::ArrowLeft) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // the caret steps back over one character, which the run maps (§9.1)
                if (m_layout.run != nullptr)
                    set_caret(m_layout.run->previous_boundary_byte(0, std::min(m_caret, m_value.size())));
                else if (m_caret > 0)
                    set_caret(m_caret - 1);
                return;
            }
            if (event.key == Key::ArrowRight) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // the caret steps forward over one character, which the run maps (§9.1)
                if (m_layout.run != nullptr)
                    set_caret(m_layout.run->next_boundary_byte(0, std::min(m_caret, m_value.size())));
                else if (m_caret < m_value.size())
                    set_caret(m_caret + 1);
                return;
            }
            // the home key puts the caret at the start, which a read only field still moves (§8.3)
            if (event.key == Key::Home) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                set_caret(0);
                return;
            }
            // the end key puts the caret at the end of the value, which a read only field still moves (§8.3)
            if (event.key == Key::End) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                set_caret(m_value.size());
                return;
            }
            // the return key is the explicit commit, which is what a form field is finalised by (§8)
            if (event.key == Key::Return) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // the handler runs only when one was installed, because a bare field commits to nothing (§8)
                if (m_on_commit)
                    m_on_commit(m_value);
                return;
            }
            // a read only field still moves its caret and still commits, but it refuses every edit (§8.3)
            if (m_read_only)
                return;
            // the backspace key removes the character before the caret (§8.3)
            if (event.key == Key::Backspace) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // the removal is over the character the step lands before, which an empty value has none of (§9.1)
                const std::size_t step = m_layout.run != nullptr ? m_layout.run->previous_boundary_byte(0, m_caret) : (m_caret > 0 ? m_caret - 1 : m_caret);
                remove_range(step, m_caret);
                return;
            }
            // the delete key removes the character at the caret (§8.3)
            if (event.key == Key::Delete) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // the removal is over the character the step lands after, which at the end removes nothing (§9.1)
                const std::size_t step = m_layout.run != nullptr ? m_layout.run->next_boundary_byte(0, m_caret) : std::min(m_caret + 1, m_value.size());
                remove_range(m_caret, step);
                return;
            }
            // any other key belongs to the text channel, so the field refuses to claim it (§8.3)
            return;
        }
        // the committed text replaces the run at the caret, which is what a commit means (§8.3)
        if (event.kind == EventKind::TextInput) {
            // a read only field takes no commit, because it refuses every edit (§8.3)
            if (m_read_only)
                return;
            // an empty commit inserts nothing, which is not an edit to answer (§8.3)
            if (event.text.empty())
                return;
            // the event is claimed, so no ancestor reacts to the same text (§8.3)
            event.handled = true;
            // the insertion is one edit, which signals once (§8)
            edit(event.text, m_caret + event.text.size());
            return;
        }
        // a press inside the plate puts the caret on the byte it lands over (§8.3, §9.1)
        if (event.kind == EventKind::PointerDown) {
            // a press outside the plate belongs to whatever is behind it (§8.3)
            if (!hit_test(event.position))
                return;
            // the event is claimed, so no ancestor reacts to the same press (§8.3)
            event.handled = true;
            // the caret reads a press only over a run the engine drew, which an empty value keeps at the start (§9.1)
            if (m_layout.run == nullptr || m_layout.run->lines().empty() || m_value.empty()) {
                set_caret(0);
                return;
            }
            // the press is resolved against the whole line, which a single line field places in one row (§9.1)
            const float caret_x = event.position.x - (bounds().x + metrics().text_field_padding);
            set_caret(m_layout.run->byte_at_x(0, std::max(0.0f, caret_x)));
            return;
        }
        // every other event belongs to a parent, so the field does not claim it (§8.3)
    }

    SemanticNode TextField::semantic_node() const {
        // the role is a text field, because the control edits text (§10.1)
        SemanticNode node{SemanticRole::TextField};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the accessible name is the identity, because a field is named by its label, not its content (§10.1, AD-11)
        node.set_name(identity().explicit_id);
        // the value is what the node edits, which is the text the field carries (§10.1)
        node.set_value(m_value);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // the states are written directly, which is what a freshly built node is (§10.1)
        node.mutable_states().disabled = disabled();
        // the read only state travels beside disabled, because the two refuse different things (§10.1)
        node.set_read_only(m_read_only);
        return node;
    }

    void TextField::set_value(std::string value) {
        // an unchanged value changes nothing, because the field looks and reads the same (§9.1)
        if (m_value == value)
            return;
        // the value is stored first, so the invalidation walks a field that already shows the new value (§6.1)
        m_value = std::move(value);
        // the caret cannot live past the value, so it is pulled back before anything else reads it (§8.3)
        m_caret = std::min(m_caret, m_value.size());
        // a new value has a new run, so the layout passes must run again (§8.2, §9.1)
        remeasure();
    }

    const std::string& TextField::value() const {
        // the edited text is what the semantic node publishes and what a handler reads (§8)
        return m_value;
    }

    void TextField::set_placeholder(std::string placeholder) {
        // an unchanged hint changes nothing, because the field looks the same (§9.1)
        if (m_placeholder == placeholder)
            return;
        // the hint is stored first, so the invalidation walks a field that already knows it (§6.1)
        m_placeholder = std::move(placeholder);
        // a new hint has a run of its own, so the layout passes must run again (§8.2, §9.1)
        remeasure();
    }

    const std::string& TextField::placeholder() const {
        // the hint is what an empty field draws (§8)
        return m_placeholder;
    }

    void TextField::set_read_only(bool read_only) {
        // an unchanged state changes nothing, because the field answers the same (§8.3)
        if (m_read_only == read_only)
            return;
        // the state is stored first, so unbinding a command later restores what the form declared (§18.7)
        m_read_only = read_only;
        // a read only field shows no caret and paints its disabled text, so the frame changes (§6.1)
        invalidate();
    }

    bool TextField::read_only() const {
        // the state is what the edits and the paint refuse on (§8.3)
        return m_read_only;
    }

    void TextField::set_disabled(bool disabled) {
        // an unchanged state redraws nothing, because the field looks the same (§6.1)
        if (m_disabled == disabled)
            return;
        // the state is stored first, so unbinding a command later restores what the form declared (§18.7)
        m_disabled = disabled;
        // a bound command owns the state, so the imperative write is recorded and not applied (§18.7)
        if (command() != nullptr)
            return;
        // the focus ring and the hit test read the state, so the frame changes (§8.3)
        invalidate();
    }

    bool TextField::disabled() const {
        // a bound command is the single answer, which is what three consumers reading one object means (§18.7)
        if (command() != nullptr)
            return !command()->state().enabled;
        // without a command the state is what the hit test and the dispatch both refuse on (§8.3)
        return m_disabled;
    }

    void TextField::set_font_size(float font_size) {
        // an unchanged size remeasures nothing, because the metrics did not change (§9.1)
        if (m_font_size == font_size)
            return;
        // the size is stored first, so the invalidation walks a field that already knows its metrics (§9.1)
        m_font_size = font_size;
        // a size change resizes both runs, so the layout passes must run again (§8.2, §9.1)
        remeasure();
    }

    float TextField::font_size() const {
        // the size is what the runs were last measured at (§9.1)
        return m_font_size;
    }

    void TextField::set_caret(std::size_t byte) {
        // an offset past the value clamps to its end, because there is no character to stand at (§8.3)
        const std::size_t clamped = std::min(byte, m_value.size());
        // an unchanged caret draws nothing new, because the paint is the same (§6.1)
        if (m_caret == clamped)
            return;
        // the offset is stored first, so the invalidation walks a field that already places the caret (§6.1)
        m_caret = clamped;
        // a caret move is visual only, because the value did not change (§6.1)
        invalidate();
    }

    std::size_t TextField::caret() const {
        // the offset is where the next edit lands (§8.3)
        return m_caret;
    }

    float TextField::line_ascent() const {
        // the ascent is what the run was measured at, which an overlay places content above (§9.1)
        return m_layout.ascent;
    }

    float TextField::line_descent() const {
        // the descent is what the run was measured at, which is the pair member (§9.1)
        return m_layout.descent;
    }

    float TextField::byte_advance() const {
        // a run the engine did not shape has no advance, so the answer is nothing (§9.1)
        if (m_layout.run == nullptr || m_layout.run->lines().empty())
            return 0.0f;
        // the advance of one byte is the run width over the value length, which is what the tests assume (§9.1)
        return m_value.empty() ? 0.0f : m_layout.run->lines().front().width / static_cast<float>(m_value.size());
    }

    Rect TextField::caret_rect() const {
        // an unarranged field has no place to put a caret, so nothing draws (§6.2)
        if (bounds().empty())
            return Rect{};
        // the metrics are asked for once, because the inset and the width read them (§8.2)
        const ControlMetrics& m = metrics();
        // the caret cannot live past the value, so it is clamped before any geometry is computed (§8.3)
        const std::size_t clamped = std::min(m_caret, m_value.size());
        // the line is where the caret hangs, which a single line field places first (§9.1)
        const ShapedRun::Line* line = nullptr;
        if (m_layout.run != nullptr && !m_layout.run->lines().empty())
            line = &m_layout.run->lines().front();
        // the x is one inset in plus the run offset, which is where the caret stands over the value (§9.1)
        const float x = bounds().x + m.text_field_padding + (line != nullptr ? line->origin.x + m_layout.run->x_at_byte(0, clamped) : 0.0f);
        // the line box is the ascent and descent of the run, which a run with none keeps at the plate height (§9.1)
        const float top = bounds().y + std::max(0.0f, (bounds().height - m_layout.size.height) * 0.5f);
        // the rect is the bar the caret draws at (§9.1)
        return Rect{.x = x, .y = top, .width = m.text_field_caret_width, .height = m_layout.size.height > 0.0f ? m_layout.size.height : bounds().height};
    }

    Point TextField::text_origin() const {
        // the metrics carry the inset, which is where the text starts (§8.2)
        const ControlMetrics& m = metrics();
        // the line hangs vertically centred, which is the same decision the paint makes (§8.2)
        return Point{.x = bounds().x + m.text_field_padding, .y = bounds().y + std::max(0.0f, (bounds().height - m_layout.size.height) * 0.5f)};
    }

    void TextField::set_on_change(std::function<void(const std::string&)> handler) {
        // the handler is replaced rather than chained, because one edit answers one handler (§8)
        m_on_change = std::move(handler);
    }

    void TextField::set_on_commit(std::function<void(const std::string&)> handler) {
        // the handler is replaced rather than chained, because one commit answers one handler (§8)
        m_on_commit = std::move(handler);
    }

    const TextLayout& TextField::layout() const {
        // the run is what the paint draws and what the measure pass produced (§9.1)
        return m_layout;
    }

    void TextField::edit(std::string replacement, std::size_t caret_byte) {
        // the text is inserted at the caret, which is the open span of the value (§8)
        m_value.insert(m_caret, std::move(replacement));
        // the caret stands past what was inserted, which is where the next edit lands (§8.3)
        m_caret = std::min(caret_byte, m_value.size());
        // the run is remeasured, because the value changed (§9.1)
        remeasure();
        // the handler runs after the state, so a validation reads the value that was kept (§8)
        if (m_on_change)
            m_on_change(m_value);
    }

    void TextField::remove_range(std::size_t start, std::size_t end) {
        // the offsets are clamped into the value, because a stray byte past the end removes nothing (§8.3)
        const std::size_t first = std::min(start, m_value.size());
        const std::size_t last = std::min(std::max(end, first), m_value.size());
        // an empty span removes nothing, which is not an edit to answer (§8.3)
        if (first == last)
            return;
        // the bytes are erased, which is the whole removal (§8)
        m_value.erase(first, last - first);
        // the caret stands where the removal started, which is where the next edit lands (§8.3)
        m_caret = first;
        // the run is remeasured, because the value changed (§9.1)
        remeasure();
        // the handler runs after the state, so a validation reads the value that was kept (§8)
        if (m_on_change)
            m_on_change(m_value);
    }

    void TextField::remeasure() {
        // a field with no engine cannot measure, so the runs stay empty and the plate keeps its minimum (§9.1)
        if (m_text_engine == nullptr) {
            m_layout = TextLayout{};
            m_placeholder_layout = TextLayout{};
            invalidate_layout();
            invalidate();
            return;
        }
        // the runs are measured unbounded, because a field never wraps its value in this shape (§8.2)
        m_layout = m_text_engine->measure(m_value, 0.0f, m_font_size);
        // the hint run is measured too, because an empty field shows it (§8)
        m_placeholder_layout = m_text_engine->measure(m_placeholder, 0.0f, m_font_size);
        // a text change is a size change, so the layout passes must run again (§8.2)
        invalidate_layout();
        // the value paint changed either way, which is what an edit means (§6.1)
        invalidate();
    }

    const Color* TextField::value_colors() const {
        // without a theme the table is unused, because the resolver returns the field's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().text_field_text;
    }

    const Color* TextField::placeholder_colors() const {
        // without a theme the table is unused, because the resolver returns the field's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().text_field_placeholder;
    }

    const Color* TextField::caret_colors() const {
        // without a theme the table is unused, because the resolver returns the field's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().text_field_caret;
    }

    const ControlMetrics& TextField::metrics() const {
        // the installed theme answers, because the sizing is one decision rather than a literal per widget (§8.2)
        if (theme() != nullptr)
            return theme()->metrics();
        // the fallback keeps the same numbers an unthemed field used before themes existed (§8.2)
        return default_metrics();
    }
} // namespace albor
