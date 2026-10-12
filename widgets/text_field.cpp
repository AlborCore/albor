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
        // the selection is painted under the value, because a highlight sits behind the text it covers (§6.1)
        paint_selection(context);
        // the hint or the value records at the inset and the centre (§6.1)
        if (!displayed.empty())
            context.draw_text(displayed, Point{.x = origin.x, .y = centered_y}, {.fill = resolve(shows_placeholder ? placeholder_colors() : value_colors(), shows_placeholder ? m_placeholder_foreground : m_foreground)});
        // the caret is drawn only over an editable field with no selection (§8.3)
        if (!m_read_only && !has_selection())
            paint_caret(context);
        // the clip is restored, so nothing after the field paints clipped by it (§6.2)
        context.restore();
    }

    void TextField::paint_selection(RenderContext& context) {
        // an empty selection has no span to highlight, which is what collapse means (§8.3)
        if (!has_selection())
            return;
        // a run the engine did not shape has no x to read, so the highlight draws nothing (§9.1)
        if (m_layout.run == nullptr || m_layout.run->lines().empty())
            return;
        // the metrics carry the inset, which is where the run starts (§8.2)
        const ControlMetrics& m = metrics();
        // the highlights are measured over the one line a single line field places (§9.1)
        const ShapedRun::Line& line = m_layout.run->lines().front();
        // the spans are resolved in run coordinates, which the origin of the line ties to the plate (§9.1)
        const float start_x = bounds().x + m.text_field_padding + line.origin.x + m_layout.run->x_at_byte(0, selection_start());
        const float end_x = bounds().x + m.text_field_padding + line.origin.x + m_layout.run->x_at_byte(0, selection_end());
        // the line box ties the highlight to the pair the run was measured at (§9.1)
        const float top = bounds().y + std::max(0.0f, (bounds().height - m_layout.size.height) * 0.5f);
        // the highlight is one rect over the span, which is what a selection is (§6.2)
        context.draw_rect(Rect{.x = start_x, .y = top, .width = std::max(0.0f, end_x - start_x), .height = m_layout.size.height}, {.fill = resolve(selection_colors(), m_selection_foreground)});
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
            // a shift arrow is a selection extension, which the plain caret keys do not collapse (§8.3)
            if (event.modifiers.shift && !event.modifiers.control && !event.modifiers.option && !event.modifiers.command && (event.key == Key::ArrowLeft || event.key == Key::ArrowRight || event.key == Key::Home || event.key == Key::End)) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // the caret is the active end, which stays where it is while the anchor holds (§8.3)
                if (event.key == Key::ArrowLeft)
                    extend_caret(m_layout.run != nullptr ? m_layout.run->previous_boundary_byte(0, std::min(m_caret, m_value.size())) : (m_caret > 0 ? m_caret - 1 : m_caret));
                else if (event.key == Key::ArrowRight)
                    extend_caret(m_layout.run != nullptr ? m_layout.run->next_boundary_byte(0, std::min(m_caret, m_value.size())) : std::min(m_caret + 1, m_value.size()));
                else if (event.key == Key::Home)
                    extend_caret(0);
                else
                    extend_caret(m_value.size());
                return;
            }
            // the clipboard and undo keys are the bindings a field answers when no shortcut took them (§18.5, §18)
            if (!event.modifiers.none()) {
                // an option chord is platform text, which no editing binding claims (§18.5)
                if (event.modifiers.option || (!event.modifiers.control && !event.modifiers.command))
                    return;
                // the copy key copies whatever the field holds, which a read only field still answers (§8.3)
                if (event.key == Key::C) {
                    event.handled = true;
                    copy();
                    return;
                }
                // the cut key removes what it copies, which a read only field refuses
                if (event.key == Key::X && !m_read_only) {
                    event.handled = true;
                    cut();
                    return;
                }
                // the paste key substitutes the clipboard, which a read only field refuses (§8.3)
                if (event.key == Key::V && !m_read_only) {
                    event.handled = true;
                    paste();
                    return;
                }
                // the select all key spans the value, which a read only field still answers (§8.3)
                if (event.key == Key::A) {
                    event.handled = true;
                    select_all();
                    return;
                }
                // the undo key reverses one edit, which a field with no stack declines (§18)
                if (event.key == Key::Z && m_undo_stack != nullptr) {
                    event.handled = true;
                    if (event.modifiers.shift)
                        redo();
                    else
                        undo();
                    return;
                }
                // any other chord belongs to the application, so the field refuses to claim it (§18.5)
                return;
            }
            // the caret keys are the arrows, the home and the end, which a read only field still moves (§8.3)
            if (event.key == Key::ArrowLeft) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // a selection collapses to its nearer end, which is what a plain arrow does to it (§8.3)
                if (has_selection()) {
                    set_caret(selection_start());
                    return;
                }
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
                // a selection collapses to its nearer end, which is what a plain arrow does to it (§8.3)
                if (has_selection()) {
                    set_caret(selection_end());
                    return;
                }
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
            // the backspace key removes the selection when one is active, or one character (§8.3)
            if (event.key == Key::Backspace) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // a selection removes whole, which is what backspacing a span means (§8.3)
                if (has_selection()) {
                    remove_range(selection_start(), selection_end());
                    return;
                }
                // the removal is over the character the step lands before, which an empty value has none of (§9.1)
                const std::size_t step = m_layout.run != nullptr ? m_layout.run->previous_boundary_byte(0, m_caret) : (m_caret > 0 ? m_caret - 1 : m_caret);
                remove_range(step, m_caret);
                return;
            }
            // the delete key removes the selection when one is active, or one character (§8.3)
            if (event.key == Key::Delete) {
                // the event is claimed, so no ancestor reacts to the same key (§8.3)
                event.handled = true;
                // a selection removes whole, which is what deleting a span means (§8.3)
                if (has_selection()) {
                    remove_range(selection_start(), selection_end());
                    return;
                }
                // the removal is over the character the step lands after, which at the end removes nothing (§9.1)
                const std::size_t step = m_layout.run != nullptr ? m_layout.run->next_boundary_byte(0, m_caret) : std::min(m_caret + 1, m_value.size());
                remove_range(m_caret, step);
                return;
            }
            // any other key belongs to the text channel, so the field refuses to claim it (§8.3)
            return;
        }
        // the committed text replaces the run at the caret, or the selection it lands over (§8.3)
        if (event.kind == EventKind::TextInput) {
            // a read only field takes no commit, because it refuses every edit (§8.3)
            if (m_read_only)
                return;
            // an empty commit inserts nothing, which is not an edit to answer (§8.3)
            if (event.text.empty())
                return;
            // the event is claimed, so no ancestor reacts to the same text (§8.3)
            event.handled = true;
            // the insertion spans the selection when one is active, and the caret when none is (§8.3)
            const std::size_t start = has_selection() ? selection_start() : m_caret;
            const std::size_t end = has_selection() ? selection_end() : m_caret;
            // the replacement is one edit, which signals once (§8)
            replace_range(start, end, event.text);
            return;
        }
        // a press inside the plate puts the caret on the byte it lands over (§8.3, §9.1)
        if (event.kind == EventKind::PointerDown) {
            // a press outside the plate belongs to whatever is behind it (§8.3)
            if (!hit_test(event.position))
                return;
            // the event is claimed, so no ancestor reacts to the same press (§8.3)
            event.handled = true;
            // the press starts a drag, which a release ends (§8.3)
            m_dragging = true;
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
        // a move while a press drag is alive extends the selection, which is what dragging means (§8.3)
        if (event.kind == EventKind::PointerMove && m_dragging) {
            // a move over a run the engine did not shape extends nothing, which an empty value gives (§9.1)
            if (m_layout.run == nullptr || m_layout.run->lines().empty() || m_value.empty())
                return;
            // the move is resolved against the whole line, which clamps past both ends (§9.1)
            const float caret_x = event.position.x - (bounds().x + metrics().text_field_padding);
            extend_caret(m_layout.run->byte_at_x(0, std::max(0.0f, caret_x)));
            return;
        }
        // a release ends the drag, whether or not it landed over the plate (§8.3)
        if (event.kind == EventKind::PointerUp) {
            // the drag is over, which the next press restarts (§8.3)
            m_dragging = false;
            // a release is not claimed, because the capture that carried it is dropped either way (§8.3)
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
        // the selection collapses with the value, because a stale span is no selection at all (§8.3)
        m_anchor = m_caret;
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
        // an unchanged pair draws nothing new, because the paint is the same (§6.1)
        if (m_caret == clamped && m_anchor == clamped)
            return;
        // the offset is stored first, so the invalidation walks a field that already places the caret (§6.1)
        m_caret = clamped;
        // a placement dissolves the selection, because the anchor follows the caret to it (§8.3)
        m_anchor = clamped;
        // a caret move is visual only, because the value did not change (§6.1)
        invalidate();
    }

    std::size_t TextField::caret() const {
        // the offset is the active end of a selection and where the next edit lands (§8.3)
        return m_caret;
    }

    void TextField::set_selection(std::size_t start, std::size_t end) {
        // the offsets are clamped into the value, because a selection ends where characters end (§8.3)
        const std::size_t first = std::min(start, m_value.size());
        const std::size_t last = std::min(end, m_value.size());
        // the anchor takes the lower end and the caret the upper, because the caret is the active end (§8.3)
        m_anchor = std::min(first, last);
        // an equal pair collapses, which is what selecting a degenerate span means (§8.3)
        m_caret = std::max(first, last);
        // a selection change is visual only, because the value did not change (§6.1)
        invalidate();
    }

    std::size_t TextField::selection_start() const {
        // the two ends are clamped into the value, which is what a stale selection reads as (§8.3)
        const std::size_t anchor = std::min(m_anchor, m_value.size());
        const std::size_t caret = std::min(m_caret, m_value.size());
        // the start is the lower of the two ends (§8.3)
        return std::min(anchor, caret);
    }

    std::size_t TextField::selection_end() const {
        // the two ends are clamped into the value, which is what a stale selection reads as (§8.3)
        const std::size_t anchor = std::min(m_anchor, m_value.size());
        const std::size_t caret = std::min(m_caret, m_value.size());
        // the end is the upper of the two ends (§8.3)
        return std::max(anchor, caret);
    }

    bool TextField::has_selection() const {
        // a selection is active when the two ends differ, because an equal pair is a placement (§8.3)
        return std::min(m_anchor, m_value.size()) != std::min(m_caret, m_value.size());
    }

    void TextField::select_all() {
        // the span is the whole value, which an empty value leaves collapsed (§8.3)
        m_anchor = 0;
        // the caret takes the far end, because the active end extends to it (§8.3)
        m_caret = m_value.size();
        // a selection change is visual only, because the value did not change (§6.1)
        invalidate();
    }

    void TextField::copy() {
        // an empty selection copies nothing, which is the answer a spanless field gives (§8.3)
        if (!has_selection())
            return;
        // a clipboard the field was never handed drops the copy, which is the absent surface (§3.5)
        if (m_clipboard == nullptr)
            return;
        // the span is copied as text, which is what a clipboard carries (§3.5)
        m_clipboard->set_text(std::string_view(m_value).substr(selection_start(), selection_end() - selection_start()));
    }

    void TextField::cut() {
        // a read only field still copies but does not cut, because cutting is an edit (§8.3)
        if (m_read_only)
            return;
        // an empty selection cuts nothing, which is the answer a spanless field gives (§8.3)
        if (!has_selection())
            return;
        // a clipboard the field was never handed cannot both hold and lose, so the cut is refused (§3.5)
        if (m_clipboard == nullptr)
            return;
        // the span is copied first, because a cut is a copy that also removes (§8.3)
        copy();
        // the removal is one edit, which removes the whole span at once (§8)
        remove_range(selection_start(), selection_end());
    }

    void TextField::paste() {
        // a read only field refuses every edit, which a paste is (§8.3)
        if (m_read_only)
            return;
        // a clipboard the field was never handed has nothing to paste, which is the absent surface (§3.5)
        if (m_clipboard == nullptr || !m_clipboard->has_text())
            return;
        // the paste is stripped of line breaks, because a single line field edits one line (§8.3)
        std::string text = m_clipboard->text();
        const auto is_line_break = [](char c) { return c == '\n' || c == '\r'; };
        text.erase(std::remove_if(text.begin(), text.end(), is_line_break), text.end());
        // an empty paste inserts nothing, which is not an edit (§8.3)
        if (text.empty())
            return;
        // the paste replaces the selection when one is active, and inserts at the caret when none is (§8.3)
        const std::size_t start = has_selection() ? selection_start() : m_caret;
        const std::size_t end = has_selection() ? selection_end() : m_caret;
        // the replacement is one edit, which records as one step (§18)
        replace_range(start, end, text);
    }

    void TextField::undo() {
        // a field with no stack has nothing to reverse, because history lives on the stack (§18)
        if (m_undo_stack == nullptr)
            return;
        // the stack reverses the entry it holds, which restores through restore (§18)
        (void)m_undo_stack->undo();
    }

    void TextField::redo() {
        // a field with no stack has nothing to reapply, because history lives on the stack (§18)
        if (m_undo_stack == nullptr)
            return;
        // the stack reapplies the entry it holds, which restores through restore (§18)
        (void)m_undo_stack->redo();
    }

    void TextField::bind_undo_stack(UndoStack* stack) {
        // the stack is borrowed, which an unbind clears (§18)
        m_undo_stack = stack;
    }

    UndoStack* TextField::undo_stack() const {
        // the stack is the one that was bound, which is null while edits record nothing (§18)
        return m_undo_stack;
    }

    void TextField::set_clipboard(Clipboard* clipboard) {
        // the clipboard is borrowed, which an unbind clears (§3.5)
        m_clipboard = clipboard;
    }

    Clipboard* TextField::clipboard() const {
        // the clipboard is the one that was installed, which is null while copy has no surface (§3.5)
        return m_clipboard;
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

    void TextField::replace_range(std::size_t start, std::size_t end, std::string_view text) {
        // the offsets are clamped into the value, because a stray byte past the end replaces nothing (§8.3)
        const std::size_t first = std::min(start, m_value.size());
        const std::size_t last = std::min(std::max(end, first), m_value.size());
        // an identity replacement is not an edit to answer, which an empty span with empty text is (§8.3)
        if (first == last && text.empty())
            return;
        // the state before the edit is captured, which is what the entry reverses to (§18)
        const std::string before = m_value;
        const std::size_t before_caret = m_caret;
        // the span is erased and the text inserted where it stood, which is the whole replacement (§8)
        std::string after = m_value;
        after.replace(first, last - first, text);
        // the value is swapped whole, because replacing in place over the same string aliases (§8)
        m_value = std::move(after);
        // the caret stands past what the edit left, which is where the next edit lands (§8.3)
        m_caret = first + text.size();
        // a placement collapses the selection, which is what replacement always means (§8.3)
        m_anchor = m_caret;
        // the run is remeasured, because the value changed (§9.1)
        remeasure();
        // the edit is recorded after the state, which is what the entry needs from both ends (§18)
        record_edit(before, before_caret);
        // the handler runs after the state, so a validation reads the value that was kept (§8)
        if (m_on_change)
            m_on_change(m_value);
    }

    void TextField::remove_range(std::size_t start, std::size_t end) {
        // the removal is a replacement with nothing, which keeps every edit on one funnel (§8)
        replace_range(start, end, {});
    }

    void TextField::record_edit(std::string before_value, std::size_t before_caret) {
        // the before snapshot lives through the reversal, which the entry carries (§18)
        std::string before = std::move(before_value);
        // a field with no stack records nothing, because history lives on the stack (§18)
        if (m_undo_stack == nullptr)
            return;
        // the state after the edit is the one the field now holds (§18)
        const std::string after = m_value;
        const std::size_t after_caret = m_caret;
        // the entry reverses to the before snapshot and reapplies the after one through the field (§18)
        m_undo_stack->push(UndoEntry{.label = "edit", .undo = [this, value = std::move(before), caret = before_caret](const CommandInvocation&) { restore(value, caret); }, .redo = [this, value = after, caret = after_caret](const CommandInvocation&) { restore(value, caret); }});
    }

    void TextField::restore(std::string value, std::size_t caret) {
        // the value is written directly, because a reversal is not an edit to record (§18)
        m_value = std::move(value);
        // the caret cannot live past the value, so it is pulled back first (§8.3)
        m_caret = std::min(caret, m_value.size());
        // a restored state collapsed, because the snapshot carries none (§8.3)
        m_anchor = m_caret;
        // the run is remeasured, because the value changed (§9.1)
        remeasure();
        // the handler runs after the state, so a validation reads the value that was kept (§18)
        if (m_on_change)
            m_on_change(m_value);
    }

    void TextField::extend_caret(std::size_t byte) {
        // an offset past the value clamps to its end, because there is no character to stand at (§8.3)
        const std::size_t clamped = std::min(byte, m_value.size());
        // an unchanged caret draws nothing new, because the span is the same (§6.1)
        if (m_caret == clamped)
            return;
        // the caret is the active end, which the anchor stays away from (§8.3)
        m_caret = clamped;
        // a drag extension is visual only, because the value did not change (§6.1)
        invalidate();
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

    const Color* TextField::selection_colors() const {
        // without a theme the table is unused, because the resolver returns the field's own value (§6.1)
        if (theme() == nullptr)
            return nullptr;
        // the installed palette answers, because the theme owns the colour (§6.1)
        return theme()->palette().text_field_selection;
    }

    const ControlMetrics& TextField::metrics() const {
        // the installed theme answers, because the sizing is one decision rather than a literal per widget (§8.2)
        if (theme() != nullptr)
            return theme()->metrics();
        // the fallback keeps the same numbers an unthemed field used before themes existed (§8.2)
        return default_metrics();
    }
} // namespace albor
