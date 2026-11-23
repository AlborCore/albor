#include "albor/render/render-blend2d/blend2d_mesh_raster.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "albor/render/image_resource.h"
#include "albor/render/mesh_resource.h"
#include "albor/render/premultiplied_color.h"

namespace albor::blend2d
{
    namespace
    {
        // one mesh vertex after the device transform, with its premultiplied colour unpacked (§6.4)
        struct DeviceVertex
        {
            // the device space horizontal position
            double x = 0.0;

            // the device space vertical position
            double y = 0.0;

            // the normalized horizontal texture coordinate
            double u = 0.0;

            // the normalized vertical texture coordinate
            double v = 0.0;

            // the premultiplied red channel
            float red = 1.0f;

            // the premultiplied green channel
            float green = 1.0f;

            // the premultiplied blue channel
            float blue = 1.0f;

            // the premultiplied alpha channel
            float alpha = 1.0f;
        };

        // the edge function a point splits the triangle by, whose sign is the side it sits on (§6.4)
        [[nodiscard]] double edge(const DeviceVertex& a, const DeviceVertex& b, double x, double y) {
            // the cross product is positive on the side the interior sits on (§6.4)
            return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        }

        // report whether an edge owns a pixel exactly on it, which keeps a shared edge with one triangle (§6.4)
        [[nodiscard]] bool top_left(const DeviceVertex& a, const DeviceVertex& b) {
            // a horizontal edge owns its pixels when it runs right, because the interior sits below it (§6.4)
            if (a.y == b.y)
                return b.x > a.x;
            // a sloping edge owns its pixels when it runs up, which is the left boundary of this winding (§6.4)
            return b.y < a.y;
        }

        // convert a premultiplied float channel into the byte the raster stores (§6.4)
        [[nodiscard]] std::uint8_t to_byte(float value) {
            // the channel clamps into the unit range and rounds to the nearest byte (§6.4)
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        // clamp a device coordinate into the range an integer pixel box can hold, which a huge vertex needs (§6.4)
        [[nodiscard]] int to_pixel(double value) {
            // the coordinate clamps before the cast, because a value outside the integer range cannot be cast (§6.4)
            return static_cast<int>(std::clamp(value, -1.0e9, 1.0e9));
        }

        // sample one premultiplied texel bilinearly with clamp to edge, which is the sampler the issue names (§6.4)
        void sample_texture(const ImageResource& texture, double u, double v, float& red, float& green, float& blue, float& alpha) {
            // an empty texture has no texel to sample, so the sampler reads white (§6.4)
            if (texture.width() == 0 || texture.height() == 0) {
                red = 1.0f;
                green = 1.0f;
                blue = 1.0f;
                alpha = 1.0f;
                return;
            }
            // the texel centre sits at half a pixel, which the bilinear weights measure from (§6.4)
            const double x = u * static_cast<double>(texture.width()) - 0.5;
            const double y = v * static_cast<double>(texture.height()) - 0.5;
            // the integer cell and the fractional weight of each axis are read together (§6.4)
            const double cell_x = std::floor(x);
            const double cell_y = std::floor(y);
            const double weight_x = x - cell_x;
            const double weight_y = y - cell_y;
            // a tap outside the texture clamps to the edge texel, which is what clamp to edge means (§6.4)
            const auto tap = [](double value, std::size_t extent) {
                // the clamped cell is turned into the index the pixel buffer holds (§6.4)
                return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(extent - 1)));
            };
            // the four neighbours are the corners of the cell the sample lands in (§6.4)
            const std::size_t left = tap(cell_x, texture.width());
            const std::size_t right = tap(cell_x + 1.0, texture.width());
            const std::size_t top = tap(cell_y, texture.height());
            const std::size_t bottom = tap(cell_y + 1.0, texture.height());
            // the pixel bytes and row stride are read once, because every channel walks them (§6.4)
            const std::span<const std::uint8_t> pixels = texture.pixels();
            const std::size_t row = texture.row_bytes();
            // one channel of one texel is a byte the weight scales (§6.4)
            const auto channel = [&pixels, row](std::size_t column, std::size_t texel_row, std::size_t channel_index) {
                // the byte is normalized into the unit range the interpolation works in (§6.4)
                return static_cast<double>(pixels[texel_row * row + column * 4 + channel_index]) / 255.0;
            };
            // every channel interpolates the same four taps, so one loop covers them (§6.4)
            float channels[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t c = 0; c < 4; c++) {
                // the two top and bottom taps are blended horizontally first (§6.4)
                const double top_value = channel(left, top, c) * (1.0 - weight_x) + channel(right, top, c) * weight_x;
                const double bottom_value = channel(left, bottom, c) * (1.0 - weight_x) + channel(right, bottom, c) * weight_x;
                // the vertical blend of the two rows is the bilinear sample (§6.4)
                channels[c] = static_cast<float>(top_value * (1.0 - weight_y) + bottom_value * weight_y);
            }
            // the four channels become the premultiplied sample the fragment multiplies (§6.4)
            red = channels[0];
            green = channels[1];
            blue = channels[2];
            alpha = channels[3];
        }
    } // namespace

    MeshRaster rasterize_mesh(const MeshResource& mesh, const ImageResource* texture, const Transform& device, const Color& modulation, const Rect& clip) {
        // the empty raster is the quiet answer for a mesh that covers no pixel (§6.4)
        MeshRaster raster;
        // an empty clip or a mesh without a triangle leaves no raster (§6.4)
        if (clip.empty() || mesh.empty())
            return raster;
        // the clip is read as the whole device pixel box the caller clamped to the target (§6.4)
        const int clip_left = static_cast<int>(clip.x);
        const int clip_top = static_cast<int>(clip.y);
        const int clip_right = clip_left + static_cast<int>(clip.width);
        const int clip_bottom = clip_top + static_cast<int>(clip.height);
        // the paint modulation packs once, which the reference quantizes like the gpu uniform (§6.4)
        const std::uint32_t packed_modulation = pack_color(modulation);
        // every modulation channel unpacks into the unit range the fragment multiplies (§6.4)
        const float modulation_red = static_cast<float>(packed_modulation & 0xFFu) / 255.0f;
        const float modulation_green = static_cast<float>((packed_modulation >> 8) & 0xFFu) / 255.0f;
        const float modulation_blue = static_cast<float>((packed_modulation >> 16) & 0xFFu) / 255.0f;
        const float modulation_alpha = static_cast<float>((packed_modulation >> 24) & 0xFFu) / 255.0f;
        // every vertex is placed and unpacked once, because each triangle reads three of them (§6.4)
        std::vector<DeviceVertex> vertices;
        vertices.reserve(mesh.vertices().size());
        // the mesh bound starts at the extreme, because the first vertex replaces it (§6.4)
        double min_x = std::numeric_limits<double>::max();
        double min_y = std::numeric_limits<double>::max();
        double max_x = std::numeric_limits<double>::lowest();
        double max_y = std::numeric_limits<double>::lowest();
        for (const MeshVertex& vertex : mesh.vertices()) {
            // the device transform places the user space position on the raster (§6.3)
            const Point position = device.apply(vertex.position);
            // the placed vertex carries its position and its texture coordinate (§6.4)
            DeviceVertex placed;
            placed.x = position.x;
            placed.y = position.y;
            placed.u = vertex.uv.x;
            placed.v = vertex.uv.y;
            // the vertex colour packs exactly as the gpu vertex does, so both quantize alike (§6.4)
            const std::uint32_t packed_color = pack_color(vertex.color);
            placed.red = static_cast<float>(packed_color & 0xFFu) / 255.0f;
            placed.green = static_cast<float>((packed_color >> 8) & 0xFFu) / 255.0f;
            placed.blue = static_cast<float>((packed_color >> 16) & 0xFFu) / 255.0f;
            placed.alpha = static_cast<float>((packed_color >> 24) & 0xFFu) / 255.0f;
            vertices.push_back(placed);
            // every placed vertex widens the mesh bound, which the raster box follows (§6.4)
            min_x = std::min(min_x, placed.x);
            min_y = std::min(min_y, placed.y);
            max_x = std::max(max_x, placed.x);
            max_y = std::max(max_y, placed.y);
        }
        // the mesh box is snapped outward to the pixels it can touch, because a pixel centre decides coverage (§6.4)
        const int left = std::max(clip_left, to_pixel(std::floor(min_x)));
        const int top = std::max(clip_top, to_pixel(std::floor(min_y)));
        const int right = std::min(clip_right, to_pixel(std::ceil(max_x)));
        const int bottom = std::min(clip_bottom, to_pixel(std::ceil(max_y)));
        // a mesh outside the clip covers no pixel, so it leaves no raster (§6.4)
        if (right <= left || bottom <= top)
            return raster;
        // the raster covers the clipped mesh box, which is the only area a pixel can land in (§6.4)
        raster.x = left;
        raster.y = top;
        raster.width = right - left;
        raster.height = bottom - top;
        raster.pixels.assign(static_cast<std::size_t>(raster.width) * static_cast<std::size_t>(raster.height) * 4, 0);
        // every whole triangle rasterizes in index order, which is the paint order the mesh was built in (§6.4)
        for (std::size_t first = 0; first + 2 < mesh.indices().size(); first += 3) {
            // the three index values are read together, because a triangle needs all of them (§6.4)
            const std::uint32_t index0 = mesh.indices()[first];
            const std::uint32_t index1 = mesh.indices()[first + 1];
            const std::uint32_t index2 = mesh.indices()[first + 2];
            // a triangle that names no vertex is skipped, so a hand-built mesh cannot read past the run (§6.4)
            if (index0 >= vertices.size() || index1 >= vertices.size() || index2 >= vertices.size())
                continue;
            // the three placed vertices are the triangle the coverage tests read (§6.4)
            const DeviceVertex& a = vertices[index0];
            DeviceVertex b = vertices[index1];
            DeviceVertex c = vertices[index2];
            // the signed area is the winding and the barycentric denominator (§6.4)
            double area = edge(a, b, c.x, c.y);
            // a negative area is the opposite winding, which the edges are swapped to make positive (§6.4)
            if (area < 0.0) {
                std::swap(b, c);
                area = -area;
            }
            // a degenerate triangle covers no pixel, so it draws nothing (§6.4)
            if (area == 0.0)
                continue;
            // the triangle box is clamped to the raster, which keeps a partly visible triangle cheap (§6.4)
            const int triangle_left = std::max(left, to_pixel(std::floor(std::min({a.x, b.x, c.x}))));
            const int triangle_top = std::max(top, to_pixel(std::floor(std::min({a.y, b.y, c.y}))));
            const int triangle_right = std::min(right, to_pixel(std::ceil(std::max({a.x, b.x, c.x}))));
            const int triangle_bottom = std::min(bottom, to_pixel(std::ceil(std::max({a.y, b.y, c.y}))));
            // every pixel centre the triangle covers is shaded (§6.4)
            for (int y = triangle_top; y < triangle_bottom; y++) {
                for (int x = triangle_left; x < triangle_right; x++) {
                    // the pixel centre is the sample the coverage tests read (§6.4)
                    const double px = static_cast<double>(x) + 0.5;
                    const double py = static_cast<double>(y) + 0.5;
                    // the three edge values are the barycentric numerators (§6.4)
                    const double weight0_edge = edge(b, c, px, py);
                    const double weight1_edge = edge(c, a, px, py);
                    const double weight2_edge = edge(a, b, px, py);
                    // a centre on an edge belongs to the triangle that owns the edge, which shares it once (§6.4)
                    const bool inside0 = weight0_edge > 0.0 || (weight0_edge == 0.0 && top_left(b, c));
                    const bool inside1 = weight1_edge > 0.0 || (weight1_edge == 0.0 && top_left(c, a));
                    const bool inside2 = weight2_edge > 0.0 || (weight2_edge == 0.0 && top_left(a, b));
                    // a centre outside any edge is left to the other triangle (§6.4)
                    if (!inside0 || !inside1 || !inside2)
                        continue;
                    // the barycentric weights normalize the edges by the area (§6.4)
                    const double weight0 = weight0_edge / area;
                    const double weight1 = weight1_edge / area;
                    const double weight2 = weight2_edge / area;
                    // the texture coordinate interpolates across the triangle, which the texel sample reads (§6.4)
                    const double u = a.u * weight0 + b.u * weight1 + c.u * weight2;
                    const double v = a.v * weight0 + b.v * weight1 + c.v * weight2;
                    // the premultiplied vertex colour interpolates the same way, which keeps the gradient linear (§6.4)
                    const float vertex_red = a.red * static_cast<float>(weight0) + b.red * static_cast<float>(weight1) + c.red * static_cast<float>(weight2);
                    const float vertex_green = a.green * static_cast<float>(weight0) + b.green * static_cast<float>(weight1) + c.green * static_cast<float>(weight2);
                    const float vertex_blue = a.blue * static_cast<float>(weight0) + b.blue * static_cast<float>(weight1) + c.blue * static_cast<float>(weight2);
                    const float vertex_alpha = a.alpha * static_cast<float>(weight0) + b.alpha * static_cast<float>(weight1) + c.alpha * static_cast<float>(weight2);
                    // a missing texture samples white, which leaves the vertex colour alone (§6.4)
                    float texture_red = 1.0f;
                    float texture_green = 1.0f;
                    float texture_blue = 1.0f;
                    float texture_alpha = 1.0f;
                    if (texture != nullptr && !texture->pixels().empty())
                        sample_texture(*texture, u, v, texture_red, texture_green, texture_blue, texture_alpha);
                    // the texel, the vertex colour and the paint modulation multiply into the source (§6.4)
                    const float source_red = vertex_red * texture_red * modulation_red;
                    const float source_green = vertex_green * texture_green * modulation_green;
                    const float source_blue = vertex_blue * texture_blue * modulation_blue;
                    const float source_alpha = vertex_alpha * texture_alpha * modulation_alpha;
                    // the source composites over the raster by its own alpha, premultiplied (§6.4)
                    const std::size_t at = (static_cast<std::size_t>(y - top) * static_cast<std::size_t>(raster.width) + static_cast<std::size_t>(x - left)) * 4;
                    const float inverse = 1.0f - source_alpha;
                    // every channel takes the source plus the destination scaled by the uncovered fraction (§6.4)
                    raster.pixels[at] = to_byte(source_red + static_cast<float>(raster.pixels[at]) / 255.0f * inverse);
                    raster.pixels[at + 1] = to_byte(source_green + static_cast<float>(raster.pixels[at + 1]) / 255.0f * inverse);
                    raster.pixels[at + 2] = to_byte(source_blue + static_cast<float>(raster.pixels[at + 2]) / 255.0f * inverse);
                    raster.pixels[at + 3] = to_byte(source_alpha + static_cast<float>(raster.pixels[at + 3]) / 255.0f * inverse);
                }
            }
        }
        // the raster is returned whole, and the caller composites it at its own origin (§6.4)
        return raster;
    }
} // namespace albor::blend2d
