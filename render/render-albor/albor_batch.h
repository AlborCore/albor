#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"

namespace albor
{
    // the immutable pixels a frame uploads, which the factory stamps (§6.4, AD-06)
    class ImageResource;
} // namespace albor

namespace albor::render
{
    // how the fragment shader turns the lateral coverage of a draw into alpha (§6.2)
    enum class CoverageKind
    {
        // the lateral coverage is the alpha itself, which a fill fan and an image emit
        Plain,

        // the lateral coverage folds around the stroke centre, which a stroke strip emits
        Folded,
    };

    // one vertex of a frame batch, which carries the position and the two coverage seeds the shader interpolates (§6.2)
    struct BatchVertex
    {
        // the device space position
        float x = 0.0f;
        float y = 0.0f;

        // the lateral coverage seed, which the ramp the draw names turns into alpha
        float lateral = 1.0f;

        // the longitudinal coverage seed, which a cap ramps and a body holds at one
        float longitudinal = 1.0f;

        // the paint space coordinates, which an image draw maps into its texture
        float u = 0.0f;
        float v = 0.0f;
    };

    // the scissor one draw is cut by, carried as the inverse transform of the clip box (nanovg technique)
    struct BatchScissor
    {
        // whether the draw is cut at all, which an unclipped draw leaves false
        bool enabled = false;

        // the inverse of the device transform, which maps a raster position onto the clip centre
        Transform inverse;

        // the half extent of the clip box in the space the inverse maps onto
        float extent_x = 0.0f;
        float extent_y = 0.0f;

        // the device space feather factors per axis, which turn the extent into a one pixel ramp
        float scale_x = 0.0f;
        float scale_y = 0.0f;

        // the corner radius of the clip box, which zero leaves the clip rectangular
        float radius = 0.0f;

        // the edge feather of the clip box, which a rounded clip ramps its boundary over
        float feather = 0.0f;
    };

    // one recorded draw of a frame batch, which the backend translates into its own pipeline state (§6.3)
    struct BatchDraw
    {
        // the half open index range of the frame mesh this draw consumes
        std::uint32_t index_begin = 0;
        std::uint32_t index_count = 0;

        // whether the draw fills, strokes or blits, which decides the coverage math the shader runs
        CoverageKind coverage = CoverageKind::Plain;

        // the ramp the folded coverage folds by, which is one for plain draws
        float ramp = 1.0f;

        // the premultiplied colour the draw paints with, packed red first in the low byte (§6.3)
        std::uint32_t color = 0;

        // how the draw combines with the target, which the backend maps to its blend state (§6.2)
        CompositionMode composition = CompositionMode::SourceOver;

        // the scissor the draw is cut by
        BatchScissor scissor;

        // the texture the draw samples, or zero for a solid colour (AD-06)
        std::uint64_t texture = 0;

        // whether the fill region is convex, which decides a fan fill against a stencilled one
        bool convex = false;

        // the shadow the draw composites first, or a negative index when it casts none (§6.2)
        int shadow = -1;
    };

    // the index range one expansion appended, which a draw record carries (§6.3)
    struct EmittedRange
    {
        // the first index the expansion appended
        std::uint32_t index_begin = 0;

        // how many indices the expansion appended
        std::uint32_t index_count = 0;
    };

    // what one stroke expansion produced, which the builder folds into its draw record (§6.3)
    struct StrokeEmission
    {
        // the index range the expansion appended
        EmittedRange range;

        // the alpha a stroke thinner than the fringe emulates its coverage with, which is one otherwise
        float alpha = 1.0f;

        // the ramp the folded coverage folds by, which the post clamp width decides
        float ramp = 1.0f;
    };

    // one shadow a frame casts, which a backend blurs offscreen and composites under the shape (§6.2)
    struct BatchShadow
    {
        // the caster in device space, which already carries the shadow offset and bounds the placed shape
        Rect rect;

        // the transform that places the caster's own box onto the device, offset, device scale and rotation alike
        Transform transform;

        // the caster box in its own space, which the silhouette evaluates its rounded shape against
        Size extent;

        // the corner radius in the caster's own space, which a placed corner keeps its shape under
        float radius = 0.0f;

        // the gaussian standard deviation in device pixels, which the blur kernel divides
        float sigma = 0.0f;

        // how far the blur reaches past the caster, which sizes the offscreen target
        float reach = 0.0f;

        // the premultiplied colour of the cast
        std::uint32_t color = 0;

        // the scissor the cast is cut by
        BatchScissor scissor;
    };

    // one image a frame uploads, which the backend caches by the identifier it carries (§6.4, AD-06)
    struct BatchUpload
    {
        // the identifier the backend keys its texture on, which never repeats (AD-06)
        std::uint64_t id = 0;

        // the immutable pixels the upload copies, which outlive the frame (§6.4)
        std::shared_ptr<const ImageResource> image;
    };

    // one composited layer a frame opens, whose draws paint offscreen and composite once at its opacity (§6.2)
    struct BatchLayer
    {
        // the opacity the whole group composites at, which is applied once rather than per draw (§6.2)
        float opacity = 1.0f;

        // the first draw the layer carries, which its offscreen pass begins before (§6.2)
        std::size_t draw_begin = 0;

        // the first draw after the layer, which its offscreen pass ends at (§6.2)
        std::size_t draw_end = 0;
    };

    // the batch one frame builds, which the builder owns until it seals and uploads it (§6.3)
    struct FrameBatch
    {
        // the vertices of every draw, in draw order
        std::vector<BatchVertex> vertices;

        // the triangle indices over the vertices, in draw order
        std::vector<std::uint32_t> indices;

        // the draws in paint order
        std::vector<BatchDraw> draws;

        // the composited layers the frame opens, in push order (§6.2)
        std::vector<BatchLayer> layers;

        // the images the frame uploads, which the backend caches across frames (§6.4, AD-06)
        std::vector<BatchUpload> uploads;

        // the shadows the frame casts, which the draws reference by index (§6.2)
        std::vector<BatchShadow> shadows;
    };

    // pack a colour into premultiplied rgba8888 with red in the low byte, which a device raster reads (§6.3)
    [[nodiscard]] std::uint32_t pack_color(const Color& color);
} // namespace albor::render
