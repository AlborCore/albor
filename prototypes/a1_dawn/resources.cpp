// the renderer neutral resource layer of prototype a1-dawn, so that dawn and skia are fed identical geometry
// the generator below is copied verbatim from prototypes/a1/resource_factory.macos.mm: if the two ever diverge,
// the comparison between the two renderers stops meaning anything

#include "prototypes/a1_dawn/resources.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

namespace albor::a1_dawn
{

    std::uint64_t next_random(std::uint64_t& state) {
        // use the splitmix64 constants, which are a fixed part of the definition
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    float next_unit(std::uint64_t& state) { return static_cast<float>(static_cast<double>(next_random(state) >> 11U) / 9007199254740992.0); }

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
            counts.at(cell_of(symbol.centre_x, symbol.centre_y))++;
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

    std::size_t measure_bytes(const ResourceSet& set) {
        // start from the retained line geometry, which is what the vertex buffer holds
        std::size_t total = set.lines.size() * sizeof(Segment);

        // add the hit index and the labels
        total += set.symbols.size() * sizeof(Symbol);
        total += set.grid.size() * sizeof(GridCell);
        total += set.grid_indices.size() * sizeof(std::uint32_t);
        total += set.labels.size() * sizeof(Label);
        total += set.label_buffer.size();
        return total;
    }

    ResourceSetInfo describe(const ResourceSet& set) {
        // report what the set holds, so the workload prints the same figures prototype a1 printed
        ResourceSetInfo info;
        info.segments = set.lines.size();
        info.symbols = set.symbols.size();
        info.labels = set.labels.size();
        info.bytes = measure_bytes(set);
        info.extent = set.extent;

        // a dawn line batch is one instanced draw call, which is the whole point of this prototype
        info.draw_calls = set.lines.empty() ? 0U : 1U;
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

    std::shared_ptr<const ResourceSet> create_line_batch(std::size_t segment_count, float extent, float stroke_width, std::uint64_t seed) {
        // a batch needs at least one segment to be worth replaying
        if (segment_count == 0 || extent <= 0.0F) {
            return nullptr;
        }

        // build the retained set that the frame will replay
        auto set = std::make_shared<ResourceSet>();
        set->extent = extent;
        set->stroke_width = stroke_width;

        // generate the segments in exactly the order prototype a1 generated them, so the geometry matches
        set->lines.resize(segment_count);
        std::uint64_t state = seed;
        for (std::size_t index = 0; index < segment_count; index++) {
            const float x0 = next_unit(state) * extent;
            const float y0 = next_unit(state) * extent;
            const float x1 = next_unit(state) * extent;
            const float y1 = next_unit(state) * extent;
            set->lines.at(index) = Segment{x0, y0, x1, y1};
        }

        // publish the set as immutable, which is what crosses into the frame (§10.5)
        return std::shared_ptr<const ResourceSet>(std::move(set));
    }

    std::shared_ptr<const ResourceSet> create_symbol_set(std::size_t symbol_count, std::size_t segments_per_symbol, std::size_t label_count, float extent, float label_size, std::uint64_t seed) {
        // a symbol set needs at least one symbol to be worth drawing
        if (symbol_count == 0 || segments_per_symbol < 3U || extent <= 0.0F) {
            return nullptr;
        }

        // build the retained set that the frame will replay
        auto set = std::make_shared<ResourceSet>();
        set->extent = extent;
        set->stroke_width = 1.0F;

        // generate the symbol centres and outlines in exactly the order prototype a1 generated them
        set->symbols.reserve(symbol_count);
        set->lines.resize(symbol_count * segments_per_symbol);
        std::uint64_t state = seed;
        for (std::size_t index = 0; index < symbol_count; index++) {
            // place the symbol and remember its own half extent for the hit test
            const float centre_x = next_unit(state) * extent;
            const float centre_y = next_unit(state) * extent;
            const float half_extent = 2.0F + next_unit(state) * 6.0F;
            set->symbols.push_back(Symbol{centre_x, centre_y, half_extent});

            // outline the symbol with a closed polygon, which is the retained path primitive of §11.2
            for (std::size_t corner = 0; corner < segments_per_symbol; corner++) {
                const float angle = 2.0F * static_cast<float>(std::numbers::pi) * static_cast<float>(corner) / static_cast<float>(segments_per_symbol);
                const float radius = half_extent * (0.7F + 0.3F * next_unit(state));
                const float dx = centre_x + radius * std::cos(angle) - centre_x;
                const float dy = centre_y + radius * std::sin(angle) - centre_y;
                set->lines.at(index * segments_per_symbol + corner) = Segment{centre_x, centre_y, centre_x + dx, centre_y + dy};
            }
        }

        // place the labels next to their symbols, all text in a single buffer, exactly as prototype a1 did
        static_cast<void>(label_size);
        set->labels.reserve(label_count);
        for (std::size_t index = 0; index < label_count && index < set->symbols.size(); index++) {
            const Symbol& symbol = set->symbols.at(index);
            set->labels.push_back(Label{symbol.centre_x + symbol.half_extent, symbol.centre_y, set->label_buffer.size(), 0});
            set->label_buffer += "S" + std::to_string(index);
            set->labels.back().length = set->label_buffer.size() - set->labels.back().offset;
        }

        // build the uniform grid the hover query walks
        set->rebuild_grid(512);

        // publish the set as immutable, which is what crosses into the frame (§10.5)
        return std::shared_ptr<const ResourceSet>(std::move(set));
    }

    HitResult hit_test(const ResourceSet& set, const ViewTransform& view, const HitQuery& query) {
        // a zoomed out view can never be hit precisely, so report no hit rather than a wrong one
        if (view.scale <= 0.0F) {
            return HitResult{};
        }

        // the grid is the only index, so a set without one has nothing to walk
        if (set.grid_dim <= 0 || set.grid.empty()) {
            return HitResult{};
        }

        // map the pointer back into resource space, which is where the symbols live
        const float resource_x = view.unmap_x(query.x);
        const float resource_y = view.unmap_y(query.y);

        // the tolerance travels into resource space too, because that is the space the distances are measured in
        const float tolerance = query.tolerance / view.scale;

        // a symbol is filed under the cell of its centre, so the pointer may sit in a neighbouring cell; the query
        // walks the three by three neighbourhood, which is the fix prototype a1 needed as well (§12.3)
        const auto dimension = static_cast<std::size_t>(set.grid_dim);
        const std::size_t centre_cell = set.cell_of(resource_x, resource_y);
        const std::size_t centre_x = centre_cell % dimension;
        const std::size_t centre_y = centre_cell / dimension;

        // walk the neighbourhood and keep the closest candidate that was inside the tolerance
        HitResult best{};
        float best_distance = std::numeric_limits<float>::max();
        for (std::ptrdiff_t step_y = -1; step_y <= 1; step_y++) {
            for (std::ptrdiff_t step_x = -1; step_x <= 1; step_x++) {
                // skip the neighbour that falls outside the grid
                const std::ptrdiff_t neighbour_x = static_cast<std::ptrdiff_t>(centre_x) + step_x;
                const std::ptrdiff_t neighbour_y = static_cast<std::ptrdiff_t>(centre_y) + step_y;
                if (neighbour_x < 0 || neighbour_y < 0 || static_cast<std::size_t>(neighbour_x) >= dimension || static_cast<std::size_t>(neighbour_y) >= dimension) {
                    continue;
                }

                // read the bucket this neighbour holds
                const GridCell& bucket = set.grid.at(static_cast<std::size_t>(neighbour_y) * dimension + static_cast<std::size_t>(neighbour_x));
                for (std::uint32_t offset = 0; offset < bucket.count; offset++) {
                    // read the symbol index the cell reserved for this candidate
                    const std::uint32_t index = set.grid_indices.at(static_cast<std::size_t>(bucket.first) + static_cast<std::size_t>(offset));
                    const Symbol& symbol = set.symbols.at(index);

                    // measure the distance in resource space, where the grid is uniform
                    const float dx = symbol.centre_x - resource_x;
                    const float dy = symbol.centre_y - resource_y;
                    const float distance = std::sqrt(dx * dx + dy * dy) - symbol.half_extent;

                    // a symbol is hit when the pointer is within tolerance of its outline or its body
                    if (distance <= tolerance && distance < best_distance) {
                        best_distance = distance;
                        best.hit = true;
                        best.symbol_index = index;

                        // report the distance in window pixels, because that is the unit the pointer is in
                        best.distance = std::max(0.0F, distance) * view.scale;
                    }
                }
            }
        }

        return best;
    }

} // namespace albor::a1_dawn
