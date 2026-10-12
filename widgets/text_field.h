#pragma once

#include <functional>
#include <string>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // the widget that edits a single line of text, which is what a form field or a search box is (§8)
    class TextField final : public Widget
    {
    public:
        // build a field over the engines it reads, with an empty value (§8) the text engine is borrowed, not owned (§9.1)
        TextField(BindingEngine* engine, TextEngine* text_engine, std::string value = {}, std::string explicit_id = {});

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

        // report whether the field refuses everything, which the hit test and the dispatch both refuse on (§8.3)
        [[nodiscard]] bool disabled() const;

        // replace the font size the value is measured at, which remeasures because the metrics changed (§9.1)
        void set_font_size(float font_size);

        // hand out the font size the value is measured at (§9.1)
        [[nodiscard]] float font_size() const;

        // place the caret at a byte offset, clamped into the value, which is where it draws (§8.3)
        void set_caret(std::size_t byte);

        // hand out the byte offset the caret stands at (§8.3)
        [[nodiscard]] std::size_t caret() const;

        // hand out the ascent of the line the field draws, which is what an overlay places content by (§9.1)
        [[nodiscard]] float line_ascent() const;

        // hand out the descent of the line the field draws, which is the pair member (§9.1)
        [[nodiscard]] float line_descent() const;

        // hand out the advance one byte of the value takes, which is what an overlay aligns a run to (§9.1)
        [[nodiscard]] float byte_advance() const;

        // hand out the rect the caret draws at, in bounds coordinates, which an anchor resolves relatives against (§9.1)
        [[nodiscard]] Rect caret_rect() const;

        // hand out where the field places the first byte of its text, which is what an overlay aligns to (§9.1)
        [[nodiscard]] Point text_origin() const;

        // install the handler every edit reaches, which a live validation reads (§8)
        void set_on_change(std::function<void(const std::string&)> handler);

        // install the handler an explicit commit reaches, which a field finalisation reads (§8)
        void set_on_commit(std::function<void(const std::string&)> handler);

        // hand out the run the value was measured at, which the paint draws and the tests read (§9.1)
        [[nodiscard]] const TextLayout& layout() const;

    private:
        // run one edit over the value, which is what every mutation goes through to signal once (§8)
        void edit(std::string replacement, std::size_t caret_byte);

        // remove the bytes between two offsets, which is what a removal edit is (§8)
        void remove_range(std::size_t start, std::size_t end);

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

        // hand out the metrics the field measures from, which the theme sets and the default answers (§8.2)
        [[nodiscard]] const ControlMetrics& metrics() const;

        // the text engine that measures the runs, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the value was measured at (§9.1)
        TextLayout m_layout;

        // the run the placeholder was measured at, which an empty field draws instead (§9.1)
        TextLayout m_placeholder_layout;

        // the edited text, which is the value a handler reads as well (§8)
        std::string m_value;

        // the hint an empty field draws (§8)
        std::string m_placeholder;

        // the byte offset the caret stands at, which is where an edit lands (§8.3)
        std::size_t m_caret = 0;

        // the size the runs are measured at (§9.1)
        float m_font_size = TextEngine::DEFAULT_FONT_SIZE;

        // whether the field refuses edits but still takes focus (§8.3)
        bool m_read_only = false;

        // whether the field refuses everything, which a bound command wins over (§18.7)
        bool m_disabled = false;

        // the fill of the value text, which the resolver replaces when a theme is installed (§6.1)
        Color m_foreground{0.92f, 0.94f, 0.96f, 1.0f};

        // the fill of the placeholder, which sits de-emphasised behind the value (§6.1)
        Color m_placeholder_foreground{0.58f, 0.60f, 0.64f, 1.0f};

        // the fill of the caret, which is the bar the value hangs beside (§6.1)
        Color m_caret_foreground{0.94f, 0.96f, 0.98f, 1.0f};

        // the handler every edit reaches, which carries the value (§8)
        std::function<void(const std::string&)> m_on_change;

        // the handler an explicit commit reaches, which carries the value (§8)
        std::function<void(const std::string&)> m_on_commit;
    };
} // namespace albor
