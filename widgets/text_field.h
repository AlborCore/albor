#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "animation/animation_engine.h"
#include "commands/undo_stack.h"
#include "core/clipboard.h"
#include "text/text_engine.h"
#include "ui/caret_target.h"
#include "ui/focus_target.h"
#include "widgets/widget.h"

namespace albor
{
    // the widget that edits one or more lines of text, which is what a form field or a search box is (§8)
    class TextField final : public Widget, public CaretTarget, public TextInputTarget
    {
    public:
        // build a field over the engines it reads, with an empty value (§8) the text engine is borrowed, not owned (§9.1)
        TextField(BindingEngine* engine, TextEngine* text_engine, std::string value = {}, std::string explicit_id = {});

        // end the blink before the field dies, because the animation the engine holds would keep calling into it (§7.3)
        ~TextField() override;

        // measure against the text engine, because a field takes the extent of the run it edits (§8.2, §9.1)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the field, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the value or the placeholder and the caret between them, which is the whole paint of a field (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a hidden or a disabled field is not a hit, because it refuses everything it would answer (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: answer every event a field edits through, claiming the ones it acts on (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the field as a text field whose value is the text it edits (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: hand out the plate fill the value draws over, which the theme owns when one is installed (§8.4)
        [[nodiscard]] Color background() const override;

        // override: hand out how wide the field strokes its border, which the theme owns (§8.4)
        [[nodiscard]] float border_width() const override;

        // override: hand out the border colour the field strokes, which resolves by state (§8.4)
        [[nodiscard]] Color border_color() const override;

        // replace the edited text, which remeasures and moves the caret inside the new extent (§9.1)
        void set_value(std::string value);

        // hand out the edited text, which is the published value (§10.1)
        [[nodiscard]] const std::string& value() const;

        // replace the hint an empty field draws, which remeasures because a hint has a run of its own (§9.1)
        void set_placeholder(std::string placeholder);

        // hand out the hint an empty field draws (§8)
        [[nodiscard]] const std::string& placeholder() const;

        // set or clear the read only state, which refuses edits but still takes focus and still permits selection (§8.3)
        void set_read_only(bool read_only);

        // report whether the field refuses edits but still takes focus (§8.3)
        [[nodiscard]] bool read_only() const;

        // set or clear the disabled state, which refuses everything a field would answer (§18.7)
        void set_disabled(bool disabled);

        // override: report whether the field refuses everything, which the hit test and the dispatch both refuse on (§8.3)
        [[nodiscard]] bool disabled() const override;

        // set or clear the multi line mode, which remeasures because the runs wrap and the plate grows (§8.2)
        void set_multiline(bool multiline);

        // report whether the field edits more than one line (§8.3)
        [[nodiscard]] bool multiline() const;

        // replace the font size the value is measured at, which remeasures because the metrics changed (§9.1)
        void set_font_size(float font_size);

        // hand out the font size the value is measured at (§9.1)
        [[nodiscard]] float font_size() const;

        // place the caret at a byte offset, clamped into the value, which is where it draws (§8.3)
        void set_caret(std::size_t byte);

        // hand out the byte offset the caret stands at, which is the active end of a selection (§8.3)
        [[nodiscard]] std::size_t caret() const;

        // set the selection over a byte span, which collapses when the offsets meet (§8.3)
        void set_selection(std::size_t start, std::size_t end);

        // hand out the first byte of the selection, which is where an edit replaces (§8.3)
        [[nodiscard]] std::size_t selection_start() const;

        // hand out the last byte of the selection, which is where an edit stops (§8.3)
        [[nodiscard]] std::size_t selection_end() const;

        // report whether a non empty selection is active, which is what copy and cut read (§8.3)
        [[nodiscard]] bool has_selection() const;

        // select the whole value, which is what select all means on a field (§8.3)
        void select_all();

        // place the selection onto the clipboard, which a read only field still answers (§8.3)
        void copy();

        // cut the selection to the clipboard, which an edit a read only field refuses (§8.3)
        void cut();

        // insert the clipboard at the caret over the selection, which is what a paste is (§8.3)
        void paste();

        // reverse the last edit, which the bound stack answers when one is bound (§18)
        void undo();

        // reapply the last reversed edit, which the bound stack answers when one is bound (§18)
        void redo();

        // bind the stack the edits record into, which is borrowed and outlives the edits it records (§18)
        void bind_undo_stack(UndoStack* stack);

        // hand out the bound stack, which is null while edits record nothing (§18)
        [[nodiscard]] UndoStack* undo_stack() const;

        // install the clipboard the copy and paste reach through, which is borrowed and may be null (§3.5)
        void set_clipboard(Clipboard* clipboard);

        // hand out the clipboard the field copies through, which is the one handed over or the process one (§3.5)
        [[nodiscard]] Clipboard* clipboard() const;

        // hand out the ascent of the line the field draws, which is what an overlay places content by (§9.1)
        [[nodiscard]] float line_ascent() const;

        // hand out the descent of the line the field draws, which is the pair member (§9.1)
        [[nodiscard]] float line_descent() const;

        // hand out the x the given byte offset sits at within its own line, which is what an overlay aligns to (§9.1)
        [[nodiscard]] float x_at_byte(std::size_t byte) const;

        // hand out the rect the caret draws at, in bounds coordinates, which an anchor resolves relatives against (§9.1)
        [[nodiscard]] Rect caret_rect() const override;

        // override: take or drop the focus, which starts or ends the blink a caret is (§8.3)
        void set_focused(bool focused) override;

        // override: hand out the selection in utf16 units, which is what an input method replaces by range (§8.3)
        [[nodiscard]] TextInputRange selection_range() const override;

        // report whether the field shows a caret at this instant, which the blink decides (§8.3)
        [[nodiscard]] bool shows_caret() const;

        // report whether a composition is active, which is what an input method paints (§8.3)
        [[nodiscard]] bool has_marked_text() const;

        // hand out the marked string a composition holds, which is empty when none is (§8.3)
        [[nodiscard]] const std::string& marked_text() const;

        // hand out where the field places the first byte of its text, which is what an overlay aligns to (§9.1)
        [[nodiscard]] Point text_origin() const;

        // install the handler every edit reaches, which a live validation reads (§8)
        void set_on_change(std::function<void(const std::string&)> handler);

        // install the handler an explicit commit reaches, which a field finalisation reads (§8)
        void set_on_commit(std::function<void(const std::string&)> handler);

        // hand out the run the value was measured at, which the paint draws and the tests read (§9.1)
        [[nodiscard]] const TextLayout& layout() const;

    private:
        // replace the bytes of a span with the text given, which is what every mutation goes through to signal once (§8)
        void replace_range(std::size_t start, std::size_t end, std::string_view text);

        // remove the bytes between two offsets, which is what a removal edit is (§8)
        void remove_range(std::size_t start, std::size_t end);

        // strip the line breaks a single line field cannot hold, which every write path goes through (§8.3)
        [[nodiscard]] std::string strip_line_breaks(std::string text) const;

        // record the edit that just ran, which the bound stack reverses by (§18)
        void record_edit(std::string before_value, std::size_t before_caret);

        // restore a snapshot, which is what an undo and a redo both arrive through (§18)
        void restore(std::string value, std::size_t caret);

        // move the caret without collapsing the selection, which is what a drag or a shift key does (§8.3)
        void extend_caret(std::size_t byte);

        // hand out the byte one caret step lands before the given one, which crosses a line break (§9.1)
        [[nodiscard]] std::size_t step_backward_byte(std::size_t byte) const;

        // hand out the byte one caret step lands after the given one, which crosses a line break (§9.1)
        [[nodiscard]] std::size_t step_forward_byte(std::size_t byte) const;

        // hand out the byte span an event asks to replace, which an input method names in its own units (§8.3)
        [[nodiscard]] std::pair<std::size_t, std::size_t> replacement_span(const std::optional<TextInputRange>& range) const;

        // hand out the row a y coordinate falls in, which a press on a multi line field resolves against (§8.3)
        [[nodiscard]] std::size_t line_at_y(float pointer_y) const;

        // hand out the byte a point resolves to, which is the row it landed on and the x within it (§8.3)
        [[nodiscard]] std::size_t byte_at_point(Point logical_point) const;

        // hand out the byte one line above the caret, which keeps the column goal (§8.3)
        [[nodiscard]] std::size_t step_up_byte(std::size_t byte) const;

        // hand out the byte one line below the caret, which keeps the column goal (§8.3)
        [[nodiscard]] std::size_t step_down_byte(std::size_t byte) const;

        // hand out the byte the line of the caret begins at, which is what a home key hits (§8.3)
        [[nodiscard]] std::size_t line_start_byte(std::size_t byte) const;

        // hand out the byte the line of the caret ends at, which is what an end key hits (§8.3)
        [[nodiscard]] std::size_t line_end_byte(std::size_t byte) const;

        // the x coordinate of the caret within the line it stands on, which a vertical move keeps (§8.3)
        [[nodiscard]] float caret_goal_x() const;

        // schedule the blink on the engine the tree was handed, because time is the engine's to advance (§7.3)
        void start_blink();

        // cancel the blink that is running, which losing the focus or a new phase does (§7.3)
        void stop_blink();

        // show the caret and start its phase again, which a move or an edit does (§8.3)
        void restart_blink();

        // paint the selection over the value, which the value paints over (§6.1)
        void paint_selection(RenderContext& context);

        // paint the underline a composition marks its provisional string with (§8.3)
        void paint_mark_underline(RenderContext& context);

        // paint the caret of the field, which the value and the hint both sit beside (§6.1)
        void paint_caret(RenderContext& context);

        // lay out the run of a fresh state, which is what every text member change needs (§9.1)
        void remeasure();

        // the colours the value text resolves through, which are the palette when a theme is installed (§6.1)
        [[nodiscard]] const Color* value_colors() const;

        // the colours the placeholder resolves through, which are the palette when a theme is installed (§6.1)
        [[nodiscard]] const Color* placeholder_colors() const;

        // the colours the caret resolves through, which are the palette when a theme is installed (§6.1)
        [[nodiscard]] const Color* caret_colors() const;

        // the colours the selection resolves through, which are the palette when a theme is installed (§6.1)
        [[nodiscard]] const Color* selection_colors() const;

        // the plate colours the field resolves through, which are the installed palette when a theme is present (§8.4)
        [[nodiscard]] const Color* plate_colors() const;

        // the border colours the field resolves through, which are the installed palette when a theme is present (§8.4)
        [[nodiscard]] const Color* border_colors() const;

        // hand out the metrics the field measures from, which the theme sets and the default answers (§8.2)
        [[nodiscard]] const ControlMetrics& metrics() const;

        // the text engine that measures the runs, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the value was measured at (§9.1)
        TextLayout m_layout;

        // the run the placeholder was measured at, which an empty field draws instead (§9.1)
        TextLayout m_placeholder_layout;

        // the run the composed text was measured at, which a mark paints instead of the value (§9.1)
        TextLayout m_marked_layout;

        // the marked string a composition holds, which a commit resolves (§8.3)
        std::string m_marked;

        // the edited text, which is the value a handler reads as well (§8)
        std::string m_value;

        // the hint an empty field draws (§8)
        std::string m_placeholder;

        // the byte offset the caret stands at, which is the active end of a selection (§8.3)
        std::size_t m_caret = 0;

        // the byte offset a selection is anchored at, which the caret moves away from (§8.3)
        std::size_t m_anchor = 0;

        // the x the caret aims for within its line, which a vertical move carries between lines (§8.3)
        float m_goal_x = 0.0f;

        // whether a press drag is extending the selection, which a release or a caret placement ends (§8.3)
        bool m_dragging = false;

        // whether the field edits more than one line, which wraps the runs and grows the plate (§8.2)
        bool m_multiline = false;

        // the size the runs are measured at (§9.1)
        float m_font_size = TextEngine::DEFAULT_FONT_SIZE;

        // whether the field refuses edits but still takes focus (§8.3)
        bool m_read_only = false;

        // whether the caret is in its visible blink phase, which a focused field alternates (§8.3)
        bool m_caret_visible = false;

        // the running blink, which the engine hands out and a focus change ends (§7.3)
        AnimationHandle m_blink = 0;

        // whether the field refuses everything, which a bound command wins over (§18.7)
        bool m_disabled = false;

        // the fill of the value text, which the resolver replaces when a theme is installed (§6.1)
        Color m_foreground{0.92f, 0.94f, 0.96f, 1.0f};

        // the fill of the placeholder, which sits de-emphasised behind the value (§6.1)
        Color m_placeholder_foreground{0.58f, 0.60f, 0.64f, 1.0f};

        // the fill of the caret, which is the bar the value hangs beside (§6.1)
        Color m_caret_foreground{0.94f, 0.96f, 0.98f, 1.0f};

        // the fill of the selection, which the value sits over (§6.1)
        Color m_selection_foreground{0.20f, 0.55f, 0.90f, 0.35f};

        // the handler every edit reaches, which carries the value (§8)
        std::function<void(const std::string&)> m_on_change;

        // the handler an explicit commit reaches, which carries the value (§8)
        std::function<void(const std::string&)> m_on_commit;

        // the clipboard the copy and paste reach through, which is null while none is installed (§3.5)
        Clipboard* m_clipboard = nullptr;

        // the stack the edits record into, which is null while edits record nothing (§18)
        UndoStack* m_undo_stack = nullptr;
    };
} // namespace albor
