#include "text/text-skia/skia_text_engine.h"

#include <string>
#include <vector>

#include <skia/modules/skparagraph/include/FontCollection.h>
#include <skia/modules/skparagraph/include/Metrics.h>
#include <skia/modules/skparagraph/include/Paragraph.h>
#include <skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <skia/modules/skparagraph/include/ParagraphStyle.h>
#include <skia/modules/skparagraph/include/TextStyle.h>

#include "text/text-skia/skia_text_services.h"

namespace albor
{
    // the shaping state, which holds everything that names a skia type so the header does not (§9.1)
    struct SkiaTextEngine::State
    {
        // the collection that owns the resolved typefaces, which every measurement borrows (§9.1)
        skia::textlayout::FontCollection collection;
        // the unicode implementation, which the paragraph builder borrows for line breaking and bidi (§9.1)
        sk_sp<SkUnicode> unicode;
    };

    SkiaTextEngine::SkiaTextEngine() :
        m_state(std::make_unique<State>()) {
        // the system font manager resolves the families, because a collection with none resolves nothing (§9.1)
        m_state->collection.setDefaultFontManager(skia_text_font_manager());
        // the icu implementation supplies line breaking and bidi, which is what skparagraph needs to run (§9.1)
        m_state->unicode = skia_text_unicode();
    }

    SkiaTextEngine::~SkiaTextEngine() = default;

    bool SkiaTextEngine::ready() const {
        // the constructor always allocates the state, so the unicode is the only thing that can be absent,
        // and an engine without it cannot break a line, so it is not usable (§9.1)
        return static_cast<bool>(m_state->unicode);
    }

    TextLayout SkiaTextEngine::measure(std::string_view text, float max_width) const {
        // the default font size is what a call site that does not care about typography uses (§9.1)
        return measure(text, max_width, DEFAULT_FONT_SIZE);
    }

    TextLayout SkiaTextEngine::measure(std::string_view text, float max_width, float font_size) const {
        // an engine without unicode cannot shape, because skparagraph dereferences it without checking, so it
        // refuses rather than passing a null through; an empty run has no glyphs to shape at all, and an
        // unmeasured run reports nothing because a fabricated extent would size a layout wrongly (§9.1)
        if (!ready() || text.empty())
            return {.ascent = 0.0f, .descent = 0.0f};
        // the text style carries the requested font size, which is what the paragraph lays out against (§9.1)
        skia::textlayout::TextStyle text_style;
        // the font size is the only typography the engine exposes, so it is set and nothing else (§9.1)
        text_style.setFontSize(font_size);
        // the paragraph style wraps the text style, because the builder takes one not the other (§9.1)
        skia::textlayout::ParagraphStyle style;
        style.setTextStyle(text_style);
        // the builder borrows the collection and the unicode, so neither is copied per measurement (§9.1)
        std::unique_ptr<skia::textlayout::ParagraphBuilder> builder = skia::textlayout::ParagraphBuilder::make(style, sk_ref_sp(&m_state->collection), m_state->unicode);
        // the utf8 run is handed to skparagraph, which is what shaping and line breaking operate on (§9.1)
        builder->addText(text.data(), text.size());
        // the paragraph owns the laid out result and its line metrics (§9.1)
        std::unique_ptr<skia::textlayout::Paragraph> paragraph = builder->Build();
        // the layout width is what wraps the run, and an unbounded run takes no wrapping at all (§8.2)
        const float layout_width = max_width > 0.0f ? max_width : UNBOUNDED_WIDTH;
        // the paragraph lays the run out at that width, which is the measure phase of §8.2 (§9.1)
        paragraph->layout(layout_width);
        // the line metrics carry the ascent and descent the baseline draw needs (§9.1)
        std::vector<skia::textlayout::LineMetrics> lines;
        // the metrics are read per line, because a wrapped run carries one ascent per line (§9.1)
        paragraph->getLineMetrics(lines);
        // the run keeps its text, so the render stage has the utf8 bytes to draw later (§6.2)
        TextLayout layout = TextLayout::from_text(std::string(text), font_size);
        // the reported width is the longest line rather than the layout width, because the last line may be short (§9.1)
        const float longest_line = static_cast<float>(paragraph->getLongestLine());
        // the reported height is the whole run, which is what every line of it adds up to (§9.1)
        const float run_height = static_cast<float>(paragraph->getHeight());
        layout.size = Size{.width = longest_line, .height = run_height};
        // a run with no lines carries no metrics, which is what an empty metrics list means (§9.1)
        if (!lines.empty()) {
            // the first line is the whole run for the unwrapped case the widgets ask for (§9.1)
            const float first_ascent = static_cast<float>(lines.front().fAscent);
            // the descent is read beside it, because a baseline draw needs the pair from one line (§9.1)
            const float first_descent = static_cast<float>(lines.front().fDescent);
            layout.ascent = first_ascent;
            layout.descent = first_descent;
        }
        return layout;
    }

    void SkiaTextEngine::install() {
        // the skia engine becomes the one the framework measures with, which is what a startup call is for (§9.1)
        set_text_engine_factory([] { return std::make_unique<SkiaTextEngine>(); });
    }
} // namespace albor
