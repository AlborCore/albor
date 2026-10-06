// the retained resource factory of §11.2: it builds immutable, cpu side resource sets that a pan or zoom never rebuilds
// the interface lives in resource_factory.h and stays free of skia types, so this file owns every skia dependency (AD-05)

#include "prototypes/a1/resource_set.h"

#include <atomic>
#include <cmath>
#include <numbers>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

#include <skia/core/SkFontMgr.h>
#include <skia/ports/SkFontMgr_mac_ct.h>

namespace albor::a1
{

    namespace
    {

        // turn a segment list into retained filled quads, split into batches that each fit one draw call
        void fill_batches(const std::vector<SkPoint>& points, float half_width, std::vector<sk_sp<SkVertices>>& batches) {
            // clear the previous batches, because the set may be rebuilt wholesale
            batches.clear();

            // every batch starts on a segment boundary, so a batch is always a whole number of segments
            const std::size_t per_batch = ResourceSet::max_segments_per_batch;
            const std::size_t segments = points.size() / ResourceSet::points_per_segment;
            for (std::size_t base = 0; base < segments; base += per_batch) {
                const std::size_t count = std::min(per_batch, segments - base);

                // four vertices and six indices per segment, which is the cheapest filled line
                std::vector<SkPoint> corners(count * static_cast<std::size_t>(ResourceSet::vertices_per_segment));
                std::vector<std::uint16_t> indices(count * 6U);
                for (std::size_t index = 0; index < count; index++) {
                    const std::size_t point = (base + index) * ResourceSet::points_per_segment;
                    const SkPoint start = points.at(point);
                    const SkPoint end = points.at(point + 1);

                    // offset both ends along the perpendicular of the segment, which is what makes a line a quad
                    const float dx = end.fX - start.fX;
                    const float dy = end.fY - start.fY;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    const float nx = length > 0.0F ? -dy / length * half_width : 0.0F;
                    const float ny = length > 0.0F ? dx / length * half_width : 0.0F;

                    // write the four corners and the six indices of this quad
                    const std::size_t vertex = index * static_cast<std::size_t>(ResourceSet::vertices_per_segment);
                    corners.at(vertex) = SkPoint::Make(start.fX + nx, start.fY + ny);
                    corners.at(vertex + 1) = SkPoint::Make(start.fX - nx, start.fY - ny);
                    corners.at(vertex + 2) = SkPoint::Make(end.fX - nx, end.fY - ny);
                    corners.at(vertex + 3) = SkPoint::Make(end.fX + nx, end.fY + ny);
                    const auto base_index = static_cast<std::uint16_t>(vertex);
                    indices.at(index * 6U) = base_index;
                    indices.at(index * 6U + 1U) = static_cast<std::uint16_t>(base_index + 1U);
                    indices.at(index * 6U + 2U) = static_cast<std::uint16_t>(base_index + 2U);
                    indices.at(index * 6U + 3U) = base_index;
                    indices.at(index * 6U + 4U) = static_cast<std::uint16_t>(base_index + 2U);
                    indices.at(index * 6U + 5U) = static_cast<std::uint16_t>(base_index + 3U);
                }

                // hand the batch to skia as immutable geometry that the frame only reads
                batches.push_back(SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, static_cast<int>(corners.size()), corners.data(), nullptr, nullptr, static_cast<int>(indices.size()), indices.data()));
            }
        }

        // the deterministic generator every workload uses, so a run reproduces exactly (§28.4)
        std::uint64_t next_random(std::uint64_t& state) {
            // use the splitmix64 constants, which are a fixed part of the definition
            state += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = state;
            z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31U);
        }

        // map a generator word into the unit interval, without touching the sign bits
        float next_unit(std::uint64_t& state) { return static_cast<float>(static_cast<double>(next_random(state) >> 11U) / 9007199254740992.0); }

        // resolve the platform font manager, because milestone 148 dropped the default one (see RESULTS.md)
        sk_sp<SkFontMgr> platform_font_manager() {
            // ask core text for the system collection, which is the only manager this build offers
            return SkFontMgr_New_CoreText(nullptr);
        }

        // resolve the typeface the labels are drawn with, falling back to the empty typeface on failure
        sk_sp<SkTypeface> resolve_typeface() {
            // ask the platform manager for the default system face
            const sk_sp<SkFontMgr> manager = platform_font_manager();
            if (manager == nullptr) {
                return sk_sp<SkTypeface>(nullptr);
            }

            // a null family name asks the manager for its own default, which is what a plain text run wants
            return manager->matchFamilyStyle(nullptr, SkFontStyle());
        }

    } // namespace

    void ResourceSet::rebuild_grid(int dimension) {
        // remember how fine the grid is, because a query needs it to find its cell
        grid_dim = dimension;

        // clear the previous grid, because the symbol list may have been replaced wholesale
        grid.clear();
        grid_indices.clear();

        // a grid that cannot hold a cell per side is not usable
        if (dimension <= 0 || symbols.empty()) {
            return;
        }

        // size the grid and the index array for the new dimension
        grid.assign(static_cast<std::size_t>(dimension) * static_cast<std::size_t>(dimension), GridCell{0, 0});
        grid_indices.resize(symbols.size());

        // count how many symbols land in each cell
        std::vector<std::uint32_t> counts(grid.size(), 0);
        for (const Symbol& symbol : symbols) {
            const std::size_t cell = cell_of(symbol.centre_x, symbol.centre_y);
            counts.at(cell)++;
        }

        // turn the counts into start offsets, which is the standard counting sort prefix step
        std::uint32_t running = 0;
        for (std::size_t index = 0; index < grid.size(); index++) {
            grid.at(index).first = running;
            running += counts.at(index);
        }

        // fill the index array, using a cursor per cell so the offsets stay valid
        std::vector<std::uint32_t> cursors;
        cursors.reserve(grid.size());
        for (const GridCell& cell : grid) {
            cursors.push_back(cell.first);
        }

        // place every symbol into the slot its cell reserved
        for (std::size_t index = 0; index < symbols.size(); index++) {
            const std::size_t cell = cell_of(symbols.at(index).centre_x, symbols.at(index).centre_y);
            grid_indices.at(cursors.at(cell)++) = static_cast<std::uint32_t>(index);
            grid.at(cell).count++;
        }
    }

    std::size_t ResourceSet::cell_of(float x, float y) const {
        // a grid without a dimension has no cells to find
        if (grid_dim <= 0 || extent <= 0.0F) {
            return 0;
        }

        // clamp the point into the generated square, because resources never fall outside it
        const float side = static_cast<float>(grid_dim);
        const auto dimension = static_cast<std::uint32_t>(grid_dim);
        const float clamped_x = std::clamp(x, 0.0F, extent);
        const float clamped_y = std::clamp(y, 0.0F, extent);

        // convert the point into a cell, keeping the last cell inside the grid
        const std::uint32_t cell_x = std::min(static_cast<std::uint32_t>(clamped_x / extent * side), dimension - 1U);
        const std::uint32_t cell_y = std::min(static_cast<std::uint32_t>(clamped_y / extent * side), dimension - 1U);
        return static_cast<std::size_t>(cell_y) * static_cast<std::size_t>(dimension) + static_cast<std::size_t>(cell_x);
    }

    ResourceSetInfo describe(const ResourceSet& set) {
        // report the segment count, which is what the retained geometry holds
        ResourceSetInfo info;
        info.points = set.segment_count * ResourceSet::points_per_segment;

        // report the hit index and the labels, which is what the cad workload carries
        info.symbols = set.symbols.size();
        info.labels = set.labels.size();
        info.bytes = set.retained_bytes;
        info.draw_calls = set.line_batches.size();
        info.extent = set.extent;
        return info;
    }

    bool symbol_centre(const ResourceSet& set, std::size_t index, float& x, float& y) {
        // an index outside the hit index has no centre to report
        if (index >= set.symbols.size()) {
            return false;
        }

        // hand the centre back to the caller, which needs it to aim a hover query
        x = set.symbols.at(index).centre_x;
        y = set.symbols.at(index).centre_y;
        return true;
    }

    std::size_t measure_bytes(const ResourceSet& set) {
        // start from the retained line geometry, which is the largest part of a waveform
        std::size_t total = 0;
        for (const sk_sp<SkVertices>& batch : set.line_batches) {
            // the retained batch reports its own approximate size, which is what the claim is measured against
            total += batch->approximateSize();
        }

        // add the recorded picture, which carries the symbol outlines
        if (set.picture) {
            total += set.picture->approximateBytesUsed();
        }

        // add the hit index, the labels and the label buffer
        total += set.symbols.size() * sizeof(Symbol);
        total += set.grid.size() * sizeof(GridCell);
        total += set.grid_indices.size() * sizeof(std::uint32_t);
        total += set.labels.size() * sizeof(Label);
        total += set.label_buffer.size();
        return total;
    }

    struct ResourceFactory::Impl
    {
        // the bytes every retained set handed out so far, which the workload reports
        std::atomic<std::size_t> retained_bytes{0};
    };

    ResourceFactory::ResourceFactory() :
        m_impl(new Impl()) {}

    ResourceFactory::~ResourceFactory() {
        // release the bookkeeping state
        delete m_impl;
        m_impl = nullptr;
    }

    ResourceSetPtr ResourceFactory::create_line_batch(const LineBatchSpec& spec) const {
        // a batch needs at least one segment to be worth replaying
        if (spec.segment_count == 0 || spec.extent <= 0.0F) {
            return nullptr;
        }

        // build the retained set that the frame will replay
        auto set = std::make_shared<ResourceSet>();
        set->extent = spec.extent;

        // the geometry is two points per segment, which is what a cosmetic line batch needs
        std::vector<SkPoint> points(spec.segment_count * 2U);

        // generate the segments deterministically from the seed, so the run reproduces
        std::uint64_t state = spec.seed;
        for (std::size_t index = 0; index < spec.segment_count; index++) {
            const float x0 = next_unit(state) * spec.extent;
            const float y0 = next_unit(state) * spec.extent;
            const float x1 = next_unit(state) * spec.extent;
            const float y1 = next_unit(state) * spec.extent;
            points.at(index * 2U) = SkPoint::Make(x0, y0);
            points.at(index * 2U + 1U) = SkPoint::Make(x1, y1);
        }

        // turn the points into retained filled quads, because ganesh strokes lines on the cpu
        fill_batches(points, spec.stroke_width * 0.5F, set->line_batches);
        set->segment_count = spec.segment_count;

        // configure the one paint every frame replays with, which is what makes a pan o(1)
        set->line_paint.setColor(SK_ColorWHITE);
        set->line_paint.setAntiAlias(false);
        set->line_paint.setStyle(SkPaint::kFill_Style);

        // record what the retained set occupies
        set->retained_bytes = measure_bytes(*set);
        m_impl->retained_bytes.fetch_add(set->retained_bytes, std::memory_order_relaxed);

        // publish the set as immutable, which is what crosses into the frame (§10.5)
        return ResourceSetPtr(std::move(set));
    }

    ResourceSetPtr ResourceFactory::create_symbol_set(const SymbolSetSpec& spec) const {
        // a symbol set needs at least one symbol to be worth drawing
        if (spec.symbol_count == 0 || spec.segments_per_symbol < 3U || spec.extent <= 0.0F) {
            return nullptr;
        }

        // build the retained set that the frame will replay
        auto set = std::make_shared<ResourceSet>();
        set->extent = spec.extent;

        // generate the symbol centres deterministically from the seed, so the run reproduces
        std::uint64_t state = spec.seed;
        set->symbols.reserve(spec.symbol_count);
        std::vector<SkPoint> points(spec.symbol_count * spec.segments_per_symbol * 2U);
        for (std::size_t index = 0; index < spec.symbol_count; index++) {
            // place the symbol and remember its own half extent for the hit test
            const float centre_x = next_unit(state) * spec.extent;
            const float centre_y = next_unit(state) * spec.extent;
            const float half_extent = 2.0F + next_unit(state) * 6.0F;
            set->symbols.push_back(Symbol{centre_x, centre_y, half_extent});

            // outline the symbol with a closed polygon, which is the retained path primitive of §11.2
            const std::size_t base = index * spec.segments_per_symbol * 2U;
            for (std::size_t corner = 0; corner < spec.segments_per_symbol; corner++) {
                const float angle = 2.0F * static_cast<float>(std::numbers::pi) * static_cast<float>(corner) / static_cast<float>(spec.segments_per_symbol);
                const float radius = half_extent * (0.7F + 0.3F * next_unit(state));
                points.at(base + corner * 2U) = SkPoint::Make(centre_x + radius * std::cos(angle), centre_y + radius * std::sin(angle));
                points.at(base + corner * 2U + 1U) = SkPoint::Make(centre_x, centre_y);
            }
        }

        // turn the points into retained filled quads, because ganesh strokes lines on the cpu
        fill_batches(points, 0.5F, set->line_batches);
        set->segment_count = spec.symbol_count;

        // configure the paints the frame replays with
        set->line_paint.setColor(SK_ColorWHITE);
        set->line_paint.setAntiAlias(false);
        set->line_paint.setStyle(SkPaint::kFill_Style);
        set->label_paint.setColor(0xFF9FD0FF);
        set->label_paint.setAntiAlias(false);

        // resolve the typeface and the font the labels are measured and drawn with
        set->typeface = resolve_typeface();
        if (set->typeface) {
            set->font = SkFont(set->typeface, spec.label_size);
        }

        // place the labels next to their symbols, all text in a single buffer
        set->labels.reserve(spec.label_count);
        for (std::size_t index = 0; index < spec.label_count && index < set->symbols.size(); index++) {
            const Symbol& symbol = set->symbols.at(index);
            set->labels.push_back(Label{symbol.centre_x + symbol.half_extent, symbol.centre_y, set->label_buffer.size(), 0});
            set->label_buffer += "S" + std::to_string(index);
            set->labels.back().length = set->label_buffer.size() - set->labels.back().offset;
        }

        // build the uniform grid the hover query walks
        set->rebuild_grid(512);

        // record what the retained set occupies
        set->retained_bytes = measure_bytes(*set);
        m_impl->retained_bytes.fetch_add(set->retained_bytes, std::memory_order_relaxed);

        // publish the set as immutable, which is what crosses into the frame (§10.5)
        return ResourceSetPtr(std::move(set));
    }

    std::size_t ResourceFactory::retained_bytes() const { return m_impl->retained_bytes.load(std::memory_order_relaxed); }

} // namespace albor::a1
