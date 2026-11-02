#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace albor::software
{
    // one rasterized glyph's coverage, which the reference draws as a textured quad (§6.3, AD-26)
    struct GlyphBitmap
    {
        // the coverage width and height in device pixels (§6.3)
        unsigned int width = 0;
        unsigned int height = 0;

        // the left bearing from the glyph's pen origin, in device pixels, which places the quad (§9.1)
        int bearing_x = 0;

        // the top bearing from the baseline, in device pixels and pointing down, which places the quad (§9.1)
        int bearing_y = 0;

        // the 8-bit alpha coverage, row major, width by height (§6.3)
        std::vector<std::uint8_t> coverage;
    };

    // the software renderer's own glyph atlas, which rasterizes from the carried font bytes (§6.3, AD-26)
    class SoftwareGlyphAtlas
    {
    public:
        // build an empty atlas, which rasterizes a glyph on its first request (§6.3)
        SoftwareGlyphAtlas();

        // release the faces and bitmaps the atlas holds, which the opaque face type needs defined here (§6.3)
        ~SoftwareGlyphAtlas();

        SoftwareGlyphAtlas(const SoftwareGlyphAtlas&) = delete;
        SoftwareGlyphAtlas& operator=(const SoftwareGlyphAtlas&) = delete;
        SoftwareGlyphAtlas(SoftwareGlyphAtlas&&) = delete;
        SoftwareGlyphAtlas& operator=(SoftwareGlyphAtlas&&) = delete;

        // hand out a glyph's coverage by font hash, glyph id and size, rasterizing and caching it (§6.3)
        [[nodiscard]] std::shared_ptr<const GlyphBitmap> glyph(std::uint64_t font_hash, std::uint32_t glyph_id, float font_size);

        // how many bitmaps the atlas holds, which a test asserts on (§6.3)
        [[nodiscard]] std::size_t count() const;

        // empty the atlas, which a test does so one test cannot answer another (§2.2)
        void clear();

    private:
        // the face a font hash resolves to, which is the freetype-free harfbuzz face the raster walks (§6.3)
        struct Face;

        // resolve a face by font hash, building it once from the registry (§6.3, AD-34)
        [[nodiscard]] Face* face_for(std::uint64_t font_hash);

        // the faces by content hash, because a name would resolve per platform (§15, AD-34)
        std::map<std::uint64_t, std::unique_ptr<Face>> m_faces;

        // the bitmaps by font hash, glyph id and quantized size, because a shape is a function of those (§6.3)
        std::map<std::tuple<std::uint64_t, std::uint32_t, int>, std::shared_ptr<const GlyphBitmap>> m_bitmaps;
    };
} // namespace albor::software
