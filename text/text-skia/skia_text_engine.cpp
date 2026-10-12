#include "text/text-skia/skia_text_engine.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <skia/core/SkFont.h>
#include <skia/modules/skparagraph/include/FontCollection.h>
#include <skia/modules/skparagraph/include/Metrics.h>
#include <skia/modules/skparagraph/include/Paragraph.h>
#include <skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <skia/modules/skparagraph/include/ParagraphStyle.h>
#include <skia/modules/skparagraph/include/TextStyle.h>

#include "text/text-skia/skia_text_services.h"

namespace albor
{
    // how many shaped runs the cache holds before it drops them (§9.1)
    constexpr std::size_t MAXIMUM_CACHED_RUNS = 512;

    namespace
    {
        // what a shaped run is cached under, which owns the text because the cache has to keep it alive (§9.1)
        struct CacheKey
        {
            // the text the run was shaped from (§9.1)
            std::string text;

            // the ceiling the run was wrapped at, where a non positive one means no wrapping at all (§8.2)
            float max_width = 0.0f;

            // the size every line was shaped at (§9.1)
            float font_size = 0.0f;
        };

        // what a lookup carries, which views the text so that finding a run allocates nothing (§9.1)
        struct CacheLookup
        {
            // the text the caller measured, which the cache never copies (§9.1)
            std::string_view text;

            // the ceiling the run was wrapped at (§8.2)
            float max_width = 0.0f;

            // the size the run is shaped at (§9.1)
            float font_size = 0.0f;
        };

        // compare keys, in either direction, so a lookup can test against an owned key (§9.1)
        struct CacheEqual
        {
            // the transparent marker is what tells the map a lookup may carry a different key type (§9.1)
            using is_transparent = void;

            // two owned keys are the same run when all three fields agree (§9.1)
            bool operator()(const CacheKey& lhs, const CacheKey& rhs) const {
                // the three fields are the whole key, so anything else would serve a run shaped for other inputs (§9.1)
                return lhs.text == rhs.text && lhs.max_width == rhs.max_width && lhs.font_size == rhs.font_size;
            }

            // a lookup and an owned key are the same run when the view matches the owned text (§9.1)
            bool operator()(const CacheKey& owned, const CacheLookup& lookup) const {
                // the ceiling and the size have to agree, because the same text wraps differently (§9.1)
                return owned.max_width == lookup.max_width && owned.font_size == lookup.font_size && owned.text == lookup.text;
            }
        };

        // hash a key into a bucket, which is what the cache looks the run up by (§9.1)
        struct CacheHasher
        {
            // the transparent marker is what tells the map a lookup may carry a different key type (§9.1)
            using is_transparent = void;

            // fold three fields into one hash, because a run shaped for other metrics must not share an entry (§9.1)
            static std::size_t fold(std::string_view text, float max_width, float font_size) {
                // the text seeds the hash, because the text is what distinguishes one run from another (§9.1)
                std::size_t seed = std::hash<std::string_view>{}(text);
                // the ceiling is folded in, because the same text wrapped differently is another run (§9.1)
                seed ^= std::hash<float>{}(max_width) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
                // the font size is folded in, because the same text shaped at another size is another run (§9.1)
                seed ^= std::hash<float>{}(font_size) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
                return seed;
            }

            // an owned key hashes by the text it owns (§9.1)
            std::size_t operator()(const CacheKey& key) const { return fold(key.text, key.max_width, key.font_size); }

            // a lookup hashes by the text it views, so both shapes agree on a bucket (§9.1)
            std::size_t operator()(const CacheLookup& lookup) const { return fold(lookup.text, lookup.max_width, lookup.font_size); }
        };
    } // namespace

    // the shaping state, which holds everything that names a skia type so the header does not (§9.1)
    struct SkiaTextEngine::State
    {
        // the collection that owns the resolved typefaces, which every measurement borrows (§9.1)
        skia::textlayout::FontCollection collection;

        // the unicode implementation, which the paragraph builder borrows for line breaking and bidi (§9.1)
        sk_sp<SkUnicode> unicode;

        // the services this engine was built over, kept so the state and the constructor cannot disagree (§9.1)
        TextServices services;

        // the shaped runs measured so far, which stage one replays instead of shaping again (§9.1, §6.1)
        mutable std::unordered_map<CacheKey, TextLayout, CacheHasher, CacheEqual> cache;

        // how many shaping passes ran, which a test reads to prove the cache absorbs a repeated measure (§9.1)
        mutable std::size_t shaped_runs = 0;
    };

    SkiaTextEngine::SkiaTextEngine() :
        SkiaTextEngine(platform_text_services()) {}

    SkiaTextEngine::SkiaTextEngine(TextServices services) :
        m_state(std::make_unique<State>()) {
        // the services are recorded first, because the state resolves from them and must not diverge (§9.1)
        m_state->services = services;
        // the font manager resolves the families, because a collection with none resolves nothing (§9.1)
        if (m_state->services.font_manager)
            m_state->collection.setDefaultFontManager(m_state->services.font_manager);
        // the unicode is taken as given, so a caller that supplied none leaves the engine refusing (§9.1)
        m_state->unicode = m_state->services.unicode;
    }

    SkiaTextEngine::~SkiaTextEngine() = default;

    bool SkiaTextEngine::ready() const {
        // the constructor always allocates the state, so the unicode is the only thing that can be absent, and an engine without it cannot break a line, so it is not usable (§9.1)
        return static_cast<bool>(m_state->unicode);
    }

    TextLayout SkiaTextEngine::measure(std::string_view text, float max_width) const {
        // the default font size is what a call site that does not care about typography uses (§9.1)
        return measure(text, max_width, DEFAULT_FONT_SIZE);
    }

    TextLayout SkiaTextEngine::measure(std::string_view text, float max_width, float font_size) const {
        // an engine without unicode cannot shape, so it refuses rather than passing a null through (§9.1)
        if (!ready() || text.empty())
            return TextLayout{};
        // the lookup views the caller's text, because copying it would allocate on every measure (§9.1)
        const CacheLookup lookup{.text = text, .max_width = max_width, .font_size = font_size};
        // a cached run is handed back as it was measured, so a steady state frame pays no shaping (§9.1)
        const auto cached = m_state->cache.find(lookup);
        if (cached != m_state->cache.end())
            return cached->second;
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
        // the shaped lines are collected before the paragraph goes away (§9.1, AD-18)
        std::vector<ShapedRun::Line> lines;
        // the half open range of runs each text line occupies, which folds a fallback split back into one ascent (§9.1)
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        // the visitor reports every line in reading order and names the font each line was shaped with (§9.1)
        paragraph->visit([&lines, &spans](int line_number, const skia::textlayout::Paragraph::VisitorInfo* info) {
            // the end of a line carries nothing to record, but a line that shaped no glyph still owns a run (§9.1)
            if (info == nullptr) {
                // a line with no glyph reports no run, so an empty one stands in for it (§9.1)
                if (spans.size() <= static_cast<std::size_t>(line_number)) {
                    // the empty line is the one the caret of a blank row is placed on (§9.1)
                    lines.push_back(ShapedRun::Line{});
                    // the span records that the line shaped nothing, which is what its byte range says (§9.1)
                    spans.push_back(std::make_pair(lines.size() - 1, lines.size()));
                }
                return;
            }
            // a line carrying no glyph has no advance to record, but it still ends its span (§9.1)
            if (info->count <= 0)
                return;
            // the run is built aside because the visitor reports one at a time (§9.1)
            ShapedRun::Line line;
            // the typeface the run was shaped with, which the renderer draws from (§9.1)
            line.font = FontHandle::from_object(info->font.getTypeface());
            // the line origin is the baseline the glyph positions of the run hang from (§9.1)
            line.origin = Point{.x = info->origin.x(), .y = info->origin.y()};
            // the glyphs are reserved, because the visitor reports how many there are (§9.1)
            line.glyphs.reserve(static_cast<std::size_t>(info->count));
            // every glyph contributes its index, its position and the byte it carries (§9.1)
            for (int index = 0; index < info->count; index++)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(info->glyphs[index]), .position = Point{.x = info->positions[index].x(), .y = info->positions[index].y()}, .cluster = info->utf8Starts[index]});
            // the run's own advance is where a caret stands past its last glyph (§9.1)
            line.width = static_cast<float>(info->advanceX);
            // the byte span of the run, which is what a caret inside this fallback resolves against (§9.1)
            line.start_byte = static_cast<std::size_t>(info->utf8Starts[0]);
            // the span ends past the last cluster, which is where the caret of a split line rests (§9.1)
            line.end_byte = static_cast<std::size_t>(info->utf8Starts[info->count]);
            // the run joins the run in the order the visitor reported it (§9.1)
            lines.push_back(std::move(line));
            // a line already collecting runs extends the span it opened, because the metrics are per line (§9.1)
            if (spans.size() <= static_cast<std::size_t>(line_number))
                spans.push_back(std::make_pair(lines.size() - 1, lines.size()));
            // otherwise the span covers this run too, because the line shaped more than one (§9.1)
            else
                spans[static_cast<std::size_t>(line_number)].second = lines.size();
        });
        // the line metrics carry the ascent and descent the caller places content by (§9.1)
        std::vector<skia::textlayout::LineMetrics> line_metrics;
        // the metrics are read per line, because a wrapped run carries one ascent per line (§9.1)
        paragraph->getLineMetrics(line_metrics);
        // every line the metrics cover is folded onto its runs, because a fallback split one line into several (§9.1)
        for (std::size_t index = 0; index < spans.size() && index < line_metrics.size(); index++) {
            // the runs of the line are addressed through the span the visitor opened for it (§9.1)
            const std::pair<std::size_t, std::size_t>& span = spans[index];
            // the ascent of the line is what every one of its runs is measured against (§9.1)
            const float ascent = static_cast<float>(line_metrics[index].fAscent);
            // the descent is read beside it, because a caller places content by the pair (§9.1)
            const float descent = static_cast<float>(line_metrics[index].fDescent);
            // a run of the line carries the ascent and the descent of the line it belongs to (§9.1)
            for (std::size_t run = span.first; run < span.second && run < lines.size(); run++) {
                // the ascent belongs to the line, so a caret sits on the same baseline in either run (§9.1)
                lines[run].ascent = ascent;
                // the descent is the pair member, so a caller places the same pair on either run (§9.1)
                lines[run].descent = descent;
            }
            // a line that shaped nothing has no run to fold onto, so it is addressed by its own index (§9.1)
            if (span.first == span.second && span.first < lines.size()) {
                // the empty line still carries the ascent, because a blank row occupies a real baseline (§9.1)
                lines[span.first].ascent = ascent;
                // the descent is the pair member, for the same reason (§9.1)
                lines[span.first].descent = descent;
            }
            // the first run of the line carries where the line begins, because a caret resolves against it (§9.1)
            if (span.first < lines.size()) {
                // the byte the line starts at is what a press on its left edge resolves to (§9.1)
                lines[span.first].start_byte = static_cast<std::size_t>(line_metrics[index].fStartIndex);
                // a line shaped nothing ends where it began, because it advanced no byte (§9.1)
                if (span.first == span.second)
                    lines[span.first].end_byte = static_cast<std::size_t>(line_metrics[index].fStartIndex);
            }
        }
        // the layout starts as an unmeasured default, so a run that shaped no line reports nothing (§9.1)
        TextLayout layout;
        // the shaped run is what the render stage replays verbatim (§9.1, AD-18)
        layout.run = ShapedRun::make(std::move(lines), font_size);
        // the reported width is the longest line rather than the layout width, because the last line may be short (§9.1)
        const float longest_line = static_cast<float>(paragraph->getLongestLine());
        // the reported height is the whole run, which is what every line of it adds up to (§9.1)
        const float run_height = static_cast<float>(paragraph->getHeight());
        layout.size = Size{.width = longest_line, .height = run_height};
        // a run with no lines carries no metrics, which is what an empty metrics list means (§9.1)
        if (!line_metrics.empty()) {
            // the first line is the whole run for the unwrapped case the widgets ask for (§9.1)
            layout.ascent = static_cast<float>(line_metrics.front().fAscent);
            // the descent is read beside it, because a caller places content by the pair (§9.1)
            layout.descent = static_cast<float>(line_metrics.front().fDescent);
        }
        // the shaping pass is counted, so a test can tell a cache hit from a fresh shaping (§9.1)
        m_state->shaped_runs++;
        // a full cache drops its runs, because an unbounded cache is a leak an application cannot see (§9.1)
        if (m_state->cache.size() >= MAXIMUM_CACHED_RUNS)
            m_state->cache.clear();
        // the run is cached under a key that owns the text, which only a miss ever builds (§9.1)
        m_state->cache.emplace(CacheKey{.text = std::string(text), .max_width = max_width, .font_size = font_size}, layout);
        return layout;
    }

    std::size_t SkiaTextEngine::shaped_run_count() const {
        // the count is what the engine has shaped, which excludes every run the cache served (§9.1)
        return m_state->shaped_runs;
    }

    std::size_t SkiaTextEngine::cache_size() const {
        // the size is what a memory pressure policy reads before it decides to drop (§9.1)
        return m_state->cache.size();
    }

    void SkiaTextEngine::clear_cache() const {
        // the cache holds only runs the ui thread can rebuild, so dropping them costs nothing but the next shaping (§9.1)
        m_state->cache.clear();
    }

    void SkiaTextEngine::install() {
        // the skia engine becomes the one the framework measures with, which is what a startup call is for (§9.1)
        set_text_engine_factory([] { return std::make_unique<SkiaTextEngine>(); });
    }
} // namespace albor
