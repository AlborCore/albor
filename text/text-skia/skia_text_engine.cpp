#include "text/text-skia/skia_text_engine.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <skia/core/SkFont.h>
#include <skia/core/SkString.h>
#include <skia/core/SkTypeface.h>
#include <skia/modules/skparagraph/include/FontCollection.h>
#include <skia/modules/skparagraph/include/Metrics.h>
#include <skia/modules/skparagraph/include/Paragraph.h>
#include <skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <skia/modules/skparagraph/include/ParagraphStyle.h>
#include <skia/modules/skparagraph/include/TextStyle.h>

#include "resources/embedded_font.h"
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

        // the content hash of a shaped typeface, which is registered so every backend can draw the run (§15, AD-34)
        std::uint64_t registered_font_hash(const SkTypeface* typeface) {
            // a run that shaped through no typeface carries no identity, which is the unresolvable zero (§15)
            if (typeface == nullptr)
                return 0;
            // the map and the registration share one lock, because a measure may run on more than one thread (§10.5)
            static std::mutex guarded;
            // the serializations are remembered per typeface, because shaping happens on every measure (§9.1)
            static std::unordered_map<std::uint32_t, std::uint64_t> known;
            const std::lock_guard<std::mutex> lock(guarded);
            // a typeface seen before answers only while the registry still carries it, because a clear may drop it (§15)
            const std::uint32_t identifier = static_cast<std::uint32_t>(typeface->uniqueID());
            const auto found = known.find(identifier);
            if (found != known.end() && resources::FontRegistry::find_hash(found->second) != nullptr)
                return found->second;
            // the bytes come from the platform half, which is the only place that may name a platform type (§3.5, AD-19)
            std::vector<std::uint8_t> bytes = platform_font_face_bytes(typeface);
            // the hash is over the bytes, and a face with none the platform can hand over stays unresolvable (§15)
            const std::uint64_t hash = bytes.empty() ? std::uint64_t{0} : resources::hash_font_bytes(bytes.data(), bytes.size());
            // a hash the registry already carries needs no second registration (§15, AD-34)
            if (hash != 0 && resources::FontRegistry::find_hash(hash) == nullptr) {
                // the family label is unique per hash, because two weights of one family must both survive (§15)
                SkString family;
                typeface->getFamilyName(&family);
                const std::string label = std::string(family.c_str()) + "-" + std::to_string(hash);
                // the bytes travel into the registry, which is what makes the run drawable everywhere (§11.3, AD-34)
                resources::FontRegistry::register_font(resources::EmbeddedFont{.family = label, .content_hash = hash, .bytes = std::move(bytes)});
            }
            known[identifier] = hash;
            // the run carries the hash, which is what a replaying backend resolves its font by (§15, AD-34)
            return hash;
        }
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
        // the shaped lines are collected before the paragraph goes away, one entry per visual line (§9.1, AD-18)
        std::vector<ShapedRun::Line> lines;
        // which visual lines already took their first run, because a fallback font splits one row into several (§9.1)
        std::vector<bool> opened;
        // the first typeface the paragraph shaped with, which is the identity the run is stamped with (§15, AD-34)
        const SkTypeface* primary = nullptr;
        // the visitor reports one font run at a time and names its line, so a run folds in (§9.1)
        paragraph->visit([&lines, &opened, &primary](int line_number, const skia::textlayout::Paragraph::VisitorInfo* info) {
            // the line the run belongs to may not be open yet, because a row shaped no glyph before this run (§9.1)
            if (lines.size() <= static_cast<std::size_t>(line_number)) {
                // the row exists as an empty line, which is what a blank row is (§9.1)
                lines.resize(static_cast<std::size_t>(line_number) + 1);
                // the opened flags follow the rows, so a later run knows whether it is the first (§9.1)
                opened.resize(static_cast<std::size_t>(line_number) + 1, false);
            }
            // the end of a line carries nothing to record, because the metrics below fold its row (§9.1)
            if (info == nullptr)
                return;
            // a line carrying no glyph has no advance to record, but the row it marks still stands (§9.1)
            if (info->count <= 0)
                return;
            // the row the run belongs to is addressed for the whole run (§9.1)
            ShapedRun::Line& line = lines[static_cast<std::size_t>(line_number)];
            // the first run of a row names the typeface and the baseline the whole row hangs from (§9.1)
            if (!opened[static_cast<std::size_t>(line_number)]) {
                // the typeface the row was shaped with, which the renderer draws from (§9.1)
                line.font = FontHandle::from_object(info->font.getTypeface());
                // the first typeface seen is the primary, whose bytes the run is identified by (§15, AD-34)
                if (primary == nullptr)
                    primary = info->font.getTypeface();
                // the line origin is the baseline the glyph positions of the row hang from (§9.1)
                line.origin = Point{.x = info->origin.x(), .y = info->origin.y()};
                // the row begins at the first cluster its first run carries (§9.1)
                line.start_byte = static_cast<std::size_t>(info->utf8Starts[0]);
                // the row is open now, so a fallback run does not reset it (§9.1)
                opened[static_cast<std::size_t>(line_number)] = true;
            }
            // the glyphs are reserved, because the visitor reports how many there are (§9.1)
            line.glyphs.reserve(line.glyphs.size() + static_cast<std::size_t>(info->count));
            // every glyph contributes its index, its position and its byte (§9.1)
            for (int index = 0; index < info->count; index++)
                line.glyphs.push_back(ShapedGlyph{.id = static_cast<std::uint32_t>(info->glyphs[index]), .position = Point{.x = info->positions[index].x(), .y = info->positions[index].y()}, .cluster = info->utf8Starts[index]});
            // the row is as wide as every run on it, which is where a caret past the last glyph stands (§9.1)
            line.width += static_cast<float>(info->advanceX);
            // the row ends past the last cluster of this run, which is the byte a caret rests at inside a split row (§9.1)
            line.end_byte = static_cast<std::size_t>(info->utf8Starts[info->count]);
        });
        // the line metrics carry the ascent and descent the caller places content by (§9.1)
        std::vector<skia::textlayout::LineMetrics> line_metrics;
        // the metrics are read per line, because a wrapped run carries one ascent per line (§9.1)
        paragraph->getLineMetrics(line_metrics);
        // every row the metrics cover is folded onto the line of the same index, because the two are the same rows (§9.1)
        for (std::size_t index = 0; index < line_metrics.size() && index < lines.size(); index++) {
            // the row is addressed once, because the metrics and the run agree (§9.1)
            ShapedRun::Line& line = lines[index];
            // the ascent of the row is what the caret above its glyphs hangs from (§9.1)
            line.ascent = static_cast<float>(line_metrics[index].fAscent);
            // the descent is the pair member, so a caller places the same pair on the row (§9.1)
            line.descent = static_cast<float>(line_metrics[index].fDescent);
            // the byte the row begins at is what a press on its left edge resolves to (§9.1)
            line.start_byte = static_cast<std::size_t>(line_metrics[index].fStartIndex);
            // a row that shaped nothing ends where it began, because it advanced no byte (§9.1)
            if (line.glyphs.empty()) {
                line.end_byte = static_cast<std::size_t>(line_metrics[index].fStartIndex);
                // the empty row still hangs from its own baseline, which the metrics carry for every row (§9.1)
                line.origin = Point{.x = 0.0f, .y = static_cast<float>(line_metrics[index].fBaseline)};
            }
        }
        // the layout starts as an unmeasured default, so a run that shaped no line reports nothing (§9.1)
        TextLayout layout;
        // the hash keys the run on the primary typeface, which makes it drawable everywhere (§15, AD-34)
        const std::uint64_t font_hash = registered_font_hash(primary);
        // the shaped run is what the render stage replays verbatim (§9.1, AD-18)
        layout.run = ShapedRun::make(std::move(lines), font_size, font_hash, ShapeSource::Platform);
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
