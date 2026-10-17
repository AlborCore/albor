#include "render/render-software/software_glyph_atlas.h"

#include <cmath>
#include <cstddef>
#include <utility>

#include <hb-ot.h>
#include <hb-raster.h>
#include <hb.h>

#include "resources/embedded_font.h"

namespace albor::software
{
    // the harfbuzz face the atlas rasterizes through, built once from the carried font bytes (§6.3, AD-34)
    struct SoftwareGlyphAtlas::Face
    {
        // the bytes the blob borrows, which the face reads without a file (§6.3, AD-34)
        std::vector<std::uint8_t> bytes;

        // the blob wrapping those bytes (§6.3)
        hb_blob_t* blob = nullptr;

        // the face built over the blob (§6.3)
        hb_face_t* face = nullptr;

        // the font the raster draws through, built from the face (§6.3)
        hb_font_t* font = nullptr;

        // the design units per em, which converts a design unit into a device pixel (§6.3)
        unsigned int upem = 1000;

        // the hb objects are released here, so no path leaks them and no raw pointer is owned by hand (§6.3)
        ~Face() {
            // the font is destroyed before the face it was built from (§6.3)
            if (font != nullptr)
                hb_font_destroy(font);
            // the face is destroyed before the blob that owns the bytes (§6.3)
            if (face != nullptr)
                hb_face_destroy(face);
            // the blob goes last, because it borrowed bytes the face still held until now (§6.3, AD-34)
            if (blob != nullptr)
                hb_blob_destroy(blob);
        }
    };

    namespace
    {
        // the largest size the atlas rasterizes at, which bounds a nonsense size before it overflows an int (§6.3)
        constexpr float MAXIMUM_GLYPH_SIZE = 4096.0f;

        // the size a bitmap is cached at, quantized to a quarter pixel so a fractional size cannot explode it (§6.3)
        int quantize(float font_size) {
            // the quarter pixel is the atlas's own size grid, which keeps a fractional size from minting a raster (§6.3)
            return static_cast<int>(font_size * 4.0f + 0.5f);
        }
    } // namespace

    SoftwareGlyphAtlas::SoftwareGlyphAtlas() = default;

    SoftwareGlyphAtlas::~SoftwareGlyphAtlas() = default;

    SoftwareGlyphAtlas::Face* SoftwareGlyphAtlas::face_for(std::uint64_t font_hash) {
        // a face already resolved answers from the cache, so the bytes are parsed once (§6.3)
        const auto found = m_faces.find(font_hash);
        if (found != m_faces.end())
            return found->second.get();
        // the registry is where a hash resolves to the bytes the build carries (§6.3, AD-34)
        const resources::EmbeddedFont* font = resources::FontRegistry::find_hash(font_hash);
        // a hash the build does not carry has no face, which is a refusal rather than a blank (§6.3)
        if (font == nullptr)
            return nullptr;
        // the face is built over the carried bytes, so no file system is consulted (§6.3, AD-34)
        auto resolved = std::make_unique<Face>();
        // the bytes are copied, because the blob borrows them and the registry owns its own (§6.3)
        resolved->bytes = font->bytes;
        // the blob borrows the bytes, which the face owns for its whole life (§6.3)
        resolved->blob = hb_blob_create(reinterpret_cast<const char*>(resolved->bytes.data()), static_cast<unsigned int>(resolved->bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        // the face is read at index zero, which is the first face in the file (§6.3)
        resolved->face = hb_face_create(resolved->blob, 0);
        // a face with no glyph is unusable, and the unique pointer releases it on the way out (§6.3)
        if (resolved->face == nullptr || hb_face_get_glyph_count(resolved->face) == 0)
            return nullptr;
        // the design units convert a design unit into a device pixel, and a zero would divide by nothing (§6.3)
        if (hb_face_get_upem(resolved->face) > 0)
            resolved->upem = hb_face_get_upem(resolved->face);
        // the font is built over the face, which owns no shaping until the funcs are set (§6.3)
        resolved->font = hb_font_create(resolved->face);
        // the opentype funcs give the font its outlines, which the raster walks (§6.3)
        hb_ot_font_set_funcs(resolved->font);
        // the face is kept under the identity it was built for, which is what a run names (§15, AD-34)
        Face* raw = resolved.get();
        m_faces.emplace(font_hash, std::move(resolved));
        return raw;
    }

    std::shared_ptr<const GlyphBitmap> SoftwareGlyphAtlas::glyph(std::uint64_t font_hash, std::uint32_t glyph_id, float font_size) {
        // a size that is not a positive, bounded number rasterizes nothing, which is a refusal (§6.3)
        if (!(font_size > 0.0f) || font_size > MAXIMUM_GLYPH_SIZE)
            return nullptr;
        // the cache key quantizes the size, so a fractional size resolves to one raster (§6.3)
        const int key_size = quantize(font_size);
        // the key is everything that changes the shape, which is the font, the glyph and the size (§6.3)
        const std::tuple<std::uint64_t, std::uint32_t, int> key{font_hash, glyph_id, key_size};
        // a cached raster answers without rasterizing again, because the shape depends on nothing else (§6.3)
        const auto found = m_bitmaps.find(key);
        if (found != m_bitmaps.end())
            return found->second;
        // the face is resolved, and its absence refuses the glyph (§6.3, AD-34)
        Face* face = face_for(font_hash);
        if (face == nullptr || face->font == nullptr)
            return nullptr;
        // the font scale reads design units, because the raster's scale factor converts them into pixels (§6.3)
        hb_font_set_scale(face->font, static_cast<int>(face->upem), static_cast<int>(face->upem));
        // the em size is the size the glyph is rasterized at, which the atlas asks for on the pixel grid (§6.3)
        const float em_size = static_cast<float>(key_size) / 4.0f;
        // the em over the design units is how many pixels one design unit becomes, which bounds the raster (§6.3)
        const float pixels_per_unit = em_size / static_cast<float>(face->upem);
        // the glyph extents bound the raster, which the render needs before it can size its image (§6.3)
        hb_glyph_extents_t extents{};
        if (!hb_font_get_glyph_extents(face->font, glyph_id, &extents))
            return nullptr;
        // the rasterizer accumulates the glyph's outline through its own draw funcs (§6.3)
        hb_raster_draw_t* draw = hb_raster_draw_create_or_fail();
        // a rasterizer that did not come up draws nothing, which is a refusal (§6.3)
        if (draw == nullptr)
            return nullptr;
        // the scale factor is design units per pixel, because the raster divides a coordinate by it (§6.3)
        const float units_per_pixel = static_cast<float>(face->upem) / em_size;
        hb_raster_draw_set_scale_factor(draw, units_per_pixel, units_per_pixel);
        // the pixel bounds are the design extents on the pixel grid, padded so rounding cannot clip a glyph (§6.3)
        hb_raster_extents_t bounds{};
        bounds.x_origin = static_cast<int>(std::floor(static_cast<float>(extents.x_bearing) * pixels_per_unit)) - 1;
        bounds.y_origin = static_cast<int>(std::floor(static_cast<float>(extents.y_bearing + extents.height) * pixels_per_unit)) - 1;
        bounds.width = static_cast<unsigned int>(std::ceil(static_cast<float>(extents.width) * pixels_per_unit)) + 2;
        bounds.height = static_cast<unsigned int>(std::ceil(static_cast<float>(-extents.height) * pixels_per_unit)) + 2;
        hb_raster_draw_set_extents(draw, &bounds);
        // the glyph outline is accumulated, and a failed draw leaves no image to cache (§6.3)
        const hb_bool_t drawn = hb_raster_draw_glyph_or_fail(draw, face->font, glyph_id);
        // the render walks the accumulated outline only when the draw succeeded, so a failure refuses the glyph (§6.3)
        hb_raster_image_t* image = drawn ? hb_raster_draw_render(draw) : nullptr;
        // the bitmap the atlas will cache, which stays null when the raster produced nothing (§6.3)
        std::shared_ptr<const GlyphBitmap> bitmap;
        if (image != nullptr) {
            // the extents place the raster in glyph space and give its padded row stride (§6.3, §9.1)
            hb_raster_extents_t placed{};
            hb_raster_image_get_extents(image, &placed);
            // the alpha buffer is the coverage, which is copied out before the image is destroyed (§6.3)
            const std::uint8_t* buffer = hb_raster_image_get_buffer(image);
            // the stride is the padded row the raster reports, so a width that is not four-aligned is followed (§6.3)
            const std::size_t stride = placed.stride != 0 ? placed.stride : placed.width;
            // the bitmap is built over the alpha coverage, which is the one format the raster renders (§6.3)
            auto built = std::make_shared<GlyphBitmap>();
            built->width = placed.width;
            built->height = placed.height;
            built->bearing_x = placed.x_origin;
            // a glyph space rises upward, so the top bearing points down from the baseline (§9.1)
            built->bearing_y = -static_cast<int>(placed.y_origin + static_cast<int>(placed.height));
            // the coverage is repacked tight, because a frame reads it row by row rather than strided (§6.3)
            built->coverage.resize(static_cast<std::size_t>(placed.width) * static_cast<std::size_t>(placed.height));
            for (unsigned int row = 0; row < placed.height; row++) {
                // the raster stores the glyph bottom up, so a row is read from the other end into screen order (§6.3)
                const unsigned int source = placed.height - 1u - row;
                for (unsigned int col = 0; col < placed.width; col++) {
                    // the byte is read at the raster's own stride, which is what keeps a row from shearing (§6.3)
                    built->coverage[static_cast<std::size_t>(row) * static_cast<std::size_t>(placed.width) + static_cast<std::size_t>(col)] = buffer[static_cast<std::size_t>(source) * stride + static_cast<std::size_t>(col)];
                }
            }
            bitmap = built;
            // the image is dropped once its pixels are copied, because the atlas owns the coverage now (§6.3)
            hb_raster_image_destroy(image);
        }
        // the rasterizer is dropped once the walk is done (§6.3)
        hb_raster_draw_destroy(draw);
        // a raster that produced a coverage joins the cache, so a later request answers from it (§6.3)
        if (bitmap != nullptr)
            m_bitmaps.emplace(key, bitmap);
        return bitmap;
    }

    std::size_t SoftwareGlyphAtlas::count() const {
        // the count is what a test asserts on, because an atlas is otherwise invisible (§6.3)
        return m_bitmaps.size();
    }

    void SoftwareGlyphAtlas::clear() {
        // the bitmaps are dropped, so one test's atlas cannot answer another's (§2.2)
        m_bitmaps.clear();
    }
} // namespace albor::software
