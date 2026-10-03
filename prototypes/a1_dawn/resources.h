#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace albor::a1_dawn
{

    // one cosmetic line segment, which is exactly what a webgpu instance needs as per-instance vertex data
    struct Segment
    {
        float x0 = 0.0F;
        float y0 = 0.0F;
        float x1 = 0.0F;
        float y1 = 0.0F;
    };

    // one symbol of the cad workload: a centre and a half extent, which is all a hover query needs (§12.3)
    struct Symbol
    {
        float centre_x = 0.0F;
        float centre_y = 0.0F;
        float half_extent = 0.0F;
    };

    // one bucket of the uniform grid that makes a hover query independent of the symbol count (§12.3)
    struct GridCell
    {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    // one text label of the cad workload: where it sits and which slice of the label buffer it reads
    struct Label
    {
        float x = 0.0F;
        float y = 0.0F;
        std::size_t offset = 0;
        std::size_t length = 0;
    };

    // what a retained set holds, reported so a workload can print the same figures a1.3 printed
    struct ResourceSetInfo
    {
        std::size_t segments = 0;
        std::size_t symbols = 0;
        std::size_t labels = 0;
        std::size_t bytes = 0;
        std::size_t draw_calls = 0;
        float extent = 0.0F;
    };

    // the immutable retained resources of a custom render node, built once and replayed per frame (§11.2)
    class ResourceSet
    {
    public:
        // the retained line geometry, which becomes one vertex buffer and one instanced draw call
        std::vector<Segment> lines;
        float stroke_width = 1.0F;

        // the symbols, the uniform grid over them, and the labels of the cad workload
        std::vector<Symbol> symbols;
        std::vector<GridCell> grid;
        std::vector<std::uint32_t> grid_indices;
        std::vector<Label> labels;
        std::string label_buffer;

        // the side of the square the resources were generated in
        float extent = 0.0F;

        // how many cells the uniform grid has per side
        int grid_dim = 0;

        // rebuild the uniform grid over the symbols, which is the structure a hover query walks (§12.3)
        void rebuild_grid(int dimension);

        // find the grid cell a resource space point falls into (§12.3)
        [[nodiscard]] std::size_t cell_of(float x, float y) const;
    };

    // the generator is copied verbatim from prototype a1 so both renderers are fed byte identical geometry;
    // if this ever diverges from resource_factory.macos.mm the comparison stops meaning anything
    std::uint64_t next_random(std::uint64_t& state);
    float next_unit(std::uint64_t& state);

    // build the same line batch prototype a1 built, from the same seed and the same call order
    [[nodiscard]] std::shared_ptr<const ResourceSet> create_line_batch(std::size_t segment_count, float extent, float stroke_width, std::uint64_t seed);

    // build the same cad symbol set prototype a1 built, from the same seed and the same call order
    [[nodiscard]] std::shared_ptr<const ResourceSet> create_symbol_set(std::size_t symbol_count, std::size_t segments_per_symbol, std::size_t label_count, float extent, float label_size, std::uint64_t seed);

    // count the bytes a retained set occupies
    [[nodiscard]] std::size_t measure_bytes(const ResourceSet& set);

    // report what a retained set holds
    [[nodiscard]] ResourceSetInfo describe(const ResourceSet& set);

    // read one symbol centre out of the hit index
    [[nodiscard]] bool symbol_centre(const ResourceSet& set, std::size_t index, float& x, float& y);

    // find the symbol under a pointer, which runs on the ui thread during pan and zoom (§12.3)
    // the query walks the three by three grid neighbourhood, because a symbol is filed under its centre cell
    struct HitResult
    {
        bool hit = false;
        std::size_t symbol_index = 0;
        float distance = 0.0F;
    };

    struct HitQuery
    {
        float x = 0.0F;
        float y = 0.0F;
        float tolerance = 4.0F;
    };

    // the view transform the workload pans and zooms with, which prototype a1 already defines skia free
    struct ViewTransform
    {
        float scale = 1.0F;
        float offset_x = 0.0F;
        float offset_y = 0.0F;

        [[nodiscard]] float map_x(float x) const { return x * scale + offset_x; }

        [[nodiscard]] float map_y(float y) const { return y * scale + offset_y; }

        [[nodiscard]] float unmap_x(float x) const { return (x - offset_x) / scale; }

        [[nodiscard]] float unmap_y(float y) const { return (y - offset_y) / scale; }
    };

    [[nodiscard]] HitResult hit_test(const ResourceSet& set, const ViewTransform& view, const HitQuery& query);

} // namespace albor::a1_dawn
