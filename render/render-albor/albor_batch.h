#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"

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

        // the corner radius of the clip box, which stays zero until the rounded scissor lands
        float radius = 0.0f;

        // the edge feather of the clip box, which stays zero until the rounded scissor lands
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

        // the scissor the draw is cut by
        BatchScissor scissor;

        // the texture the draw samples, or zero for a solid colour (AD-06)
        std::uint64_t texture = 0;

        // whether the fill region is convex, which decides a fan fill against a stencilled one
        bool convex = false;
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

    // the batch one frame builds, which the builder owns until it seals and uploads it (§6.3)
    struct FrameBatch
    {
        // the vertices of every draw, in draw order
        std::vector<BatchVertex> vertices;

        // the triangle indices over the vertices, in draw order
        std::vector<std::uint32_t> indices;

        // the draws in paint order
        std::vector<BatchDraw> draws;
    };

    // pack a colour into premultiplied rgba8888 with red in the low byte, which a device raster reads (§6.3)
    [[nodiscard]] std::uint32_t pack_color(const Color& color);
} // namespace albor::render
