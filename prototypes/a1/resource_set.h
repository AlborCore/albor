#pragma once

// the definition of the retained resource set, which is the one place skia types are allowed to appear
// nothing outside the implementation includes this header, so no skia type reaches a public header (AD-05, AD-19)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <skia/core/SkColorSpace.h>
#include <skia/core/SkData.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPicture.h>
#include <skia/core/SkPoint.h>
#include <skia/core/SkTypeface.h>
#include <skia/core/SkVertices.h>

#include "prototypes/a1/resource_factory.h"

namespace albor::a1
{

    // one symbol of the cad workload: a centre and a half extent, which is all a hover query needs (§12.3)
    struct Symbol
    {
        float centre_x = 0.0F;
        float centre_y = 0.0F;
        float half_extent = 0.0F;
    };

    // one text label of the cad workload: where it sits and which slice of the label buffer it reads
    struct Label
    {
        float x = 0.0F;
        float y = 0.0F;
        std::size_t offset = 0; // the byte offset into the single label buffer
        std::size_t length = 0; // how many bytes of the buffer belong to this label
    };

    // one bucket of the uniform grid that makes a hit query independent of the symbol count (§12.3)
    struct GridCell
    {
        std::uint32_t first = 0; // the first symbol index in the index array
        std::uint32_t count = 0; // how many symbol indices follow
    };

    // the immutable retained resources of a custom render node, as §11.2 wants them: built once, replayed per frame
    class ResourceSet : public std::enable_shared_from_this<ResourceSet>
    {
    public:
        // a retained line batch holds two points per segment
        static constexpr std::size_t points_per_segment = 2;

        // one filled quad per segment, so four vertices and six indices
        static constexpr int vertices_per_segment = 4;

        // skia indexes a batch with 16 bits, so one draw call cannot exceed 65535 vertices; the measured
        // consequence is recorded in RESULTS.md and is the reason the geometry is split into batches
        static constexpr std::size_t max_segments_per_batch = 8192;

        // the retained line geometry as filled quads, split into batches that each fit one draw call
        std::vector<sk_sp<SkVertices>> line_batches;

        // how many segments the retained geometry holds
        std::size_t segment_count = 0;

        // the retained picture, which workload w2 uses for its symbol outlines (§11.2)
        sk_sp<SkPicture> picture;

        // the resolved platform typeface and font, because milestone 148 has no default font manager
        // the font is held by value because milestone 148 made it a plain object rather than a ref counted one
        sk_sp<SkTypeface> typeface;
        SkFont font;

        // the paint shared by every line, so a pan or zoom never rebuilds it
        SkPaint line_paint;
        SkPaint label_paint;

        // the symbols, the uniform grid over them, and the labels of the cad workload
        std::vector<Symbol> symbols;
        std::vector<GridCell> grid;
        std::vector<std::uint32_t> grid_indices;
        std::vector<Label> labels;
        std::string label_buffer;

        // the side of the square the resources were generated in, which the view transform maps onto the window
        float extent = 0.0F;

        // how many cells the uniform grid has per side
        int grid_dim = 0;

        // the bytes the retained resources occupy, reported so an o(1) claim can be checked against something
        std::size_t retained_bytes = 0;

        // rebuild the uniform grid over the symbols, which is the structure a hover query walks (§12.3)
        void rebuild_grid(int dimension);

        // find the grid cell a resource space point falls into, which is the first step of a hit query (§12.3)
        [[nodiscard]] std::size_t cell_of(float x, float y) const;
    };

    // count the bytes a resource set occupies, including the label buffer and the index arrays
    std::size_t measure_bytes(const ResourceSet& set);

} // namespace albor::a1
