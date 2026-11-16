#pragma once

#include <cstdint>
#include <vector>

#include "albor/core/geometry.h"
#include "albor/core/paint.h"

namespace albor
{
    // the retained triangles the rasterizer reads, which this header names without its include (§3.5)
    class MeshResource;

    // the premultiplied texture the rasterizer samples, which this header names without its include (§3.5)
    class ImageResource;
} // namespace albor

namespace albor::blend2d
{
    // the premultiplied pixels one mesh rasterizes into, positioned at a device pixel origin (§6.4)
    struct MeshRaster
    {
        // the first device pixel column the raster covers
        int x = 0;

        // the first device pixel row the raster covers
        int y = 0;

        // the pixel width of the raster
        int width = 0;

        // the pixel height of the raster
        int height = 0;

        // the premultiplied rgba8 pixels, four bytes per row-major cell (§6.4)
        std::vector<std::uint8_t> pixels;

        // report whether the raster covers no pixel at all
        [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
    };

    // rasterize a mesh into a device pixel box, sampling its texture and interpolating its vertex colours (§6.4)
    [[nodiscard]] MeshRaster rasterize_mesh(const MeshResource& mesh, const ImageResource* texture, const Transform& device, const Color& modulation, const Rect& clip);
} // namespace albor::blend2d
