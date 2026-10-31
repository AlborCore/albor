#include "render/render-blend2d/blend2d_render_context.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <blend2d.h>

#include "render/draw_command.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"
#include "render/text_layout.h"
#include "resources/embedded_font.h"

namespace albor::blend2d
{
    namespace
    {
        // the bytes and the face of one registered font, which the cache owns for the process (§9.2)
        struct CachedFontFace
        {
            // the font bytes the face was built from, owned here because blend2d borrows them (§9.2, AD-34)
            std::vector<std::uint8_t> bytes;

            // the font data over those bytes, which borrows them for the face below (§9.2)
            BLFontData data;

            // the face the fonts of a run are built from, which is immutable and independent of size (§9.2)
            BLFontFace face;
        };

        // the faces built from the registered font bytes, keyed by content hash and reused (§9.2)
        std::unordered_map<std::uint64_t, CachedFontFace>& font_face_cache() {
            // the cache outlives every frame, because a face is immutable and independent of size (§9.2)
            static std::unordered_map<std::uint64_t, CachedFontFace> cache;
            return cache;
        }

        std::mutex& font_face_cache_mutex() {
            static std::mutex mutex;
            return mutex;
        }

        // map a composition mode onto the blend2d component operator it corresponds to (§6.2)
        BLCompOp to_blend_comp(CompositionMode mode) {
            switch (mode) {
            case CompositionMode::SourceIn:
                return BL_COMP_OP_SRC_IN;
            case CompositionMode::SourceOut:
                return BL_COMP_OP_SRC_OUT;
            case CompositionMode::Atop:
                return BL_COMP_OP_SRC_ATOP;
            case CompositionMode::DestinationOver:
                return BL_COMP_OP_DST_OVER;
            case CompositionMode::DestinationIn:
                return BL_COMP_OP_DST_IN;
            case CompositionMode::DestinationOut:
                return BL_COMP_OP_DST_OUT;
            case CompositionMode::DestinationAtop:
                return BL_COMP_OP_DST_ATOP;
            case CompositionMode::Lighter:
                return BL_COMP_OP_PLUS;
            case CompositionMode::Copy:
                return BL_COMP_OP_SRC_COPY;
            case CompositionMode::Xor:
                return BL_COMP_OP_XOR;
            case CompositionMode::SourceOver:
                break;
            }
            return BL_COMP_OP_SRC_OVER;
        }

        // average a window sum with rounding, because truncation biases every blurred edge downward (§6.2)
        [[nodiscard]] std::uint8_t box_average(std::int64_t sum, int window) {
            // the half window rounds to the nearest byte rather than always down (§6.2)
            return static_cast<std::uint8_t>((sum + window / 2) / window);
        }

        // clamp a window offset to the raster, because a blur samples the edge rather than beyond it (§6.2)
        [[nodiscard]] int box_index(std::int64_t index, int extent) {
            // the extent is positive, so the clamp has a valid last index (§6.2)
            return static_cast<int>(std::clamp(index, std::int64_t{0}, static_cast<std::int64_t>(extent - 1)));
        }

        // blur an image in place with two sliding box passes, the soft shadow edge blend2d has no primitive for (§6.2)
        void blur_box(BLImageData& data, int radius) {
            // a radius below one leaves the image untouched, because a zero window is no blur at all (§6.2)
            if (radius < 1)
                return;
            // a window that overflows the channel sums is refused, which keeps every count bounded (§6.2)
            if (radius > (std::numeric_limits<int>::max() - 1) / 2)
                return;
            // the extent is read once, because both passes walk the same rows and then the same columns (§6.2)
            const int width = data.size.w;
            const int height = data.size.h;
            // an extent with no area has nothing to blur, so the pass is skipped (§6.2)
            if (width <= 0 || height <= 0)
                return;
            // the source is the premultiplied raster, whose palette is blue, green, red and alpha in memory (§6.3, AD-26)
            std::uint8_t* base = static_cast<std::uint8_t*>(data.pixelData);
            const std::intptr_t stride = data.stride;
            // the window is the count either side of the centre plus the centre itself (§6.2)
            const int window = radius * 2 + 1;
            // the horizontal result is held aside, because the vertical pass reads it back rather than the source (§6.2)
            std::vector<std::uint8_t> temporary(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            // every row slides a window, which costs one add and one remove per pixel (§6.2)
            for (int y = 0; y < height; y++) {
                const std::uint8_t* source_row = base + static_cast<std::intptr_t>(y) * stride;
                std::uint8_t* temporary_row = temporary.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
                // the window starts at the left edge, where every sample clamps to the first pixel (§6.2)
                std::int64_t sum_blue = 0;
                std::int64_t sum_green = 0;
                std::int64_t sum_red = 0;
                std::int64_t sum_alpha = 0;
                for (int offset = -radius; offset <= radius; offset++) {
                    // the sample is clamped to the row, because a blur has no pixel beyond the edge to read (§6.2)
                    const std::uint8_t* pixel = source_row + static_cast<std::size_t>(box_index(offset, width)) * 4;
                    sum_blue += pixel[0];
                    sum_green += pixel[1];
                    sum_red += pixel[2];
                    sum_alpha += pixel[3];
                }
                for (int x = 0; x < width; x++) {
                    // the average is written in the same palette order the source used (§6.3)
                    std::uint8_t* destination = temporary_row + static_cast<std::size_t>(x) * 4;
                    destination[0] = box_average(sum_blue, window);
                    destination[1] = box_average(sum_green, window);
                    destination[2] = box_average(sum_red, window);
                    destination[3] = box_average(sum_alpha, window);
                    // the next window drops the pixel leaving behind and adds the one entering (§6.2)
                    const std::uint8_t* leaving = source_row + static_cast<std::size_t>(box_index(static_cast<std::int64_t>(x) - radius, width)) * 4;
                    const std::uint8_t* entering = source_row + static_cast<std::size_t>(box_index(static_cast<std::int64_t>(x) + radius + 1, width)) * 4;
                    sum_blue += entering[0] - leaving[0];
                    sum_green += entering[1] - leaving[1];
                    sum_red += entering[2] - leaving[2];
                    sum_alpha += entering[3] - leaving[3];
                }
            }
            // every column slides the same window down the temporary rows, which softens the edge vertically (§6.2)
            for (int x = 0; x < width; x++) {
                // the window starts at the top edge, where every sample clamps to the first row (§6.2)
                std::int64_t sum_blue = 0;
                std::int64_t sum_green = 0;
                std::int64_t sum_red = 0;
                std::int64_t sum_alpha = 0;
                for (int offset = -radius; offset <= radius; offset++) {
                    // the sample is clamped to the column, because a blur has no pixel beyond the edge to read (§6.2)
                    const std::uint8_t* pixel = temporary.data() + (static_cast<std::size_t>(box_index(offset, height)) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
                    sum_blue += pixel[0];
                    sum_green += pixel[1];
                    sum_red += pixel[2];
                    sum_alpha += pixel[3];
                }
                for (int y = 0; y < height; y++) {
                    // the average lands in the same palette order the source used (§6.3)
                    std::uint8_t* pixel = base + static_cast<std::intptr_t>(y) * stride + static_cast<std::size_t>(x) * 4;
                    pixel[0] = box_average(sum_blue, window);
                    pixel[1] = box_average(sum_green, window);
                    pixel[2] = box_average(sum_red, window);
                    pixel[3] = box_average(sum_alpha, window);
                    // the next window drops the pixel leaving behind and adds the one entering (§6.2)
                    const std::uint8_t* leaving = temporary.data() + (static_cast<std::size_t>(box_index(static_cast<std::int64_t>(y) - radius, height)) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
                    const std::uint8_t* entering = temporary.data() + (static_cast<std::size_t>(box_index(static_cast<std::int64_t>(y) + radius + 1, height)) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
                    sum_blue += entering[0] - leaving[0];
                    sum_green += entering[1] - leaving[1];
                    sum_red += entering[2] - leaving[2];
                    sum_alpha += entering[3] - leaving[3];
                }
            }
        }

        // report whether every coordinate a shadow rasterizes is finite, because a non finite edge has no bound (§6.2)
        [[nodiscard]] bool shadow_is_finite(const Rect& rect, const Shadow& shadow) {
            // the caster, the offset and the blur all feed a device rectangle, so each must be finite (§6.2)
            return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) && std::isfinite(rect.height) && std::isfinite(shadow.offset.x) && std::isfinite(shadow.offset.y) && std::isfinite(shadow.blur);
        }
    } // namespace

    namespace
    {
        // one rounded clip a group intersects, kept in the space it was recorded in (§6.2)
        struct RoundedClip
        {
            // the rectangle and radius the caller asked for
            Rect rect;

            // the radius the corners round by, where zero is a plain rectangle
            float radius = 0.0f;

            // the transform in effect when the clip was recorded
            Transform transform;
        };

        // the coverage a rounded clip leaves at one device pixel centre, which maps back through the clip transform (§6.2)
        float rounded_coverage(const RoundedClip& clip, float device_x, float device_y, float device_scale) {
            // a clip with no radius is the rectangle the caller already intersected, so it covers fully (§6.2)
            if (clip.radius <= 0.0f)
                return 1.0f;
            // the inverse of the affine clip transform maps the device point onto the space it was recorded in (§6.2)
            const float det = clip.transform.scale_x * clip.transform.scale_y - clip.transform.skew_x * clip.transform.skew_y;
            // a singular transform has no inverse, so the clip is treated as covering rather than vanishing (§6.2)
            if (std::abs(det) < 1e-6f)
                return 1.0f;
            const float dx = device_x / device_scale - clip.transform.translate_x;
            const float dy = device_y / device_scale - clip.transform.translate_y;
            const float x = (clip.transform.scale_y * dx - clip.transform.skew_x * dy) / det;
            const float y = (-clip.transform.skew_y * dx + clip.transform.scale_x * dy) / det;
            // one device pixel in the clip space decides the one pixel ramp the edge is feathered by (§6.2)
            const float pixel_x = std::hypot(clip.transform.scale_y, clip.transform.skew_y) / (std::abs(det) * device_scale);
            const float pixel_y = std::hypot(clip.transform.scale_x, clip.transform.skew_x) / (std::abs(det) * device_scale);
            const float pixel = std::max(pixel_x, pixel_y);
            // the rounded rectangle distance is negative inside, which the ramp turns into coverage (§6.2)
            const float half_w = clip.rect.width * 0.5f;
            const float half_h = clip.rect.height * 0.5f;
            const float radius = std::min({clip.radius, half_w, half_h});
            const float qx = std::abs(x - (clip.rect.x + half_w)) - (half_w - radius);
            const float qy = std::abs(y - (clip.rect.y + half_h)) - (half_h - radius);
            const float distance = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
            return std::clamp(0.5f - distance / pixel, 0.0f, 1.0f);
        }

        // the device box a rounded clip can cover, where every edge is half open (§6.2)
        struct DeviceBounds
        {
            // the first pixel column the clip can cover
            int left = 0;

            // the first pixel row the clip can cover
            int top = 0;

            // the pixel column after the last one the clip can cover
            int right = 0;

            // the pixel row after the last one the clip can cover
            int bottom = 0;

            // report whether the box holds no pixel at all
            [[nodiscard]] bool empty() const { return right <= left || bottom <= top; }
        };

        // bound the device pixels a rounded clip can cover, leaving an unrestricted clip open (§6.2)
        [[nodiscard]] bool rounded_clip_bounds(const RoundedClip& clip, float device_scale, int width, int height, DeviceBounds& bounds) {
            // a clip with no radius covers through the rectangle, which the group already clips (§6.2)
            if (clip.radius <= 0.0f)
                return false;
            // the inverse of the clip transform decides both the corners and the one pixel feather (§6.2)
            const float det = clip.transform.scale_x * clip.transform.scale_y - clip.transform.skew_x * clip.transform.skew_y;
            // a singular transform covers rather than vanishes, so it cannot bound the flatten (§6.2)
            if (!std::isfinite(det) || std::abs(det) < 1e-6f || !std::isfinite(device_scale) || device_scale <= 0.0f)
                return false;
            // the four corners map onto the raster, which is the box the rounded shape can reach (§6.2)
            const Transform device = clip.transform.then(Transform::scaling(device_scale, device_scale));
            const Point first = device.apply(Point{clip.rect.x, clip.rect.y});
            const Point second = device.apply(Point{clip.rect.x + clip.rect.width, clip.rect.y});
            const Point third = device.apply(Point{clip.rect.x + clip.rect.width, clip.rect.y + clip.rect.height});
            const Point fourth = device.apply(Point{clip.rect.x, clip.rect.y + clip.rect.height});
            // a corner no affine transform can place has no finite box to bound (§6.2)
            if (!std::isfinite(first.x) || !std::isfinite(first.y) || !std::isfinite(second.x) || !std::isfinite(second.y) || !std::isfinite(third.x) || !std::isfinite(third.y) || !std::isfinite(fourth.x) || !std::isfinite(fourth.y))
                return false;
            // one device pixel of feather is the margin the coverage ramp reaches beyond the transformed rectangle (§6.2)
            const float pixel = std::max(std::hypot(clip.transform.scale_y, clip.transform.skew_y), std::hypot(clip.transform.scale_x, clip.transform.skew_x)) / (std::abs(det) * device_scale);
            const float margin = std::max(1.0f, pixel * device_scale);
            // a margin or corner outside the finite range falls back to the whole target rather than guessing (§6.2)
            if (!std::isfinite(margin))
                return false;
            // the mapped corners are snapped outward, so no pixel the feather touches is left outside (§6.2)
            const float min_x = std::min({first.x, second.x, third.x, fourth.x}) - margin;
            const float min_y = std::min({first.y, second.y, third.y, fourth.y}) - margin;
            const float max_x = std::max({first.x, second.x, third.x, fourth.x}) + margin;
            const float max_y = std::max({first.y, second.y, third.y, fourth.y}) + margin;
            // the box is clamped to the target, because a pixel outside it has no raster to mask (§6.2)
            const float clamped_left = std::clamp(min_x, 0.0f, static_cast<float>(width));
            const float clamped_top = std::clamp(min_y, 0.0f, static_cast<float>(height));
            const float clamped_right = std::clamp(max_x, 0.0f, static_cast<float>(width));
            const float clamped_bottom = std::clamp(max_y, 0.0f, static_cast<float>(height));
            // the floor and ceiling keep the half open edges conservative (§6.2)
            bounds.left = static_cast<int>(std::floor(clamped_left));
            bounds.top = static_cast<int>(std::floor(clamped_top));
            bounds.right = static_cast<int>(std::ceil(clamped_right));
            bounds.bottom = static_cast<int>(std::ceil(clamped_bottom));
            // a box with no area cannot narrow the target, so it is left open for the caller (§6.2)
            return !bounds.empty();
        }
    } // namespace

    struct Blend2DRenderContext::ClipGroup
    {
        // the image the group draws into, which starts as a copy of the destination (§6.2)
        BLImage target;

        // the destination the mask leaves outside the shape, kept for the composite (§6.2)
        BLImage backup;

        // the raster context over that image, which every draw inside the group lands in (§6.2)
        BLContext context;

        // the parent raster the group composites back into, which a nested group restores (§6.2)
        BLContext* parent = nullptr;

        // the group that was open before this one, or null when the parent is the frame (§6.2)
        ClipGroup* parent_group = nullptr;

        // the state the group began under, which its flatten restores (§6.2)
        State parent_state;

        // the rounded clips the group intersects, in recording order (§6.2)
        std::vector<RoundedClip> clips;
    };

    struct Blend2DRenderContext::Layer
    {
        // the state the group began under, which its pop returns to (§6.2)
        State saved;

        // the parent raster the layer composites back into, which a nested layer restores (§6.2)
        BLContext* parent = nullptr;

        // the group opacity the layer composites at, applied once rather than per draw (§6.2)
        float opacity = 1.0f;

        // the offscreen raster the group paints into, whose pixels the composite blits (§6.2)
        BLImage image;

        // the raster context over that image, which every draw inside the group lands in (§6.2)
        BLContext context;
    };

    Blend2DRenderContext::~Blend2DRenderContext() = default;
    Blend2DRenderContext::Blend2DRenderContext(BLContext* context, float device_scale, Rect full_clip) :
        m_context(context), m_device_scale(device_scale), m_clip(full_clip) {
        // a context with no raster context draws nothing, so it keeps its own bookkeeping only (§6.2)
        if (m_context == nullptr)
            return;
        // the raster context spans the physical pixels, so the base scale maps the logical points onto them (§6.3, AD-27)
        m_context->scale(m_device_scale, m_device_scale);
        // the scale is folded into the meta matrix, so every later setTransform stays in logical points (§6.3, AD-27)
        m_context->userToMeta();
    }

    void Blend2DRenderContext::save() {
        // the albor state is pushed first, because the raster save is what the pop restores (§6.2)
        m_states.push_back(State{m_transform, m_clip, m_opacity, m_group});
        // a context with no raster context keeps its own stack only (§6.2)
        if (m_context == nullptr)
            return;
        m_context->save();
    }

    void Blend2DRenderContext::restore() {
        // a restore without a matching save is a programming error, so the stack is left alone (§6.2)
        if (m_states.empty())
            return;
        // every clip group opened after this save closes before its state returns (§6.2)
        while (m_group != nullptr && m_group != m_states.back().group)
            flatten_clip_group();
        // the raster context is restored before the albor state, because it holds the real clip stack (§6.2)
        if (m_context != nullptr)
            m_context->restore();
        // the saved state becomes the current one again (§6.2)
        const State state = m_states.back();
        m_states.pop_back();
        m_transform = state.transform;
        m_clip = state.clip;
        m_opacity = state.opacity;
    }

    void Blend2DRenderContext::set_transform(const Transform& transform) {
        // a context with no raster context keeps its own transform bookkeeping only (§6.2)
        if (m_context == nullptr) {
            m_transform = transform;
            return;
        }
        // the matrix is rebuilt from the albor transform, so a save and a restore round trip (§6.2)
        m_context->setTransform(to_blend_matrix(transform));
        // the base scale sits in the meta matrix under the scene transform, so a logical point spans the raster (§6.3)
        m_transform = transform;
    }

    void Blend2DRenderContext::apply_clip(BLContext& context, const Rect& rect) const {
        // an empty clip cuts everything away, which a zero rectangle expresses to the raster (§6.2)
        if (rect.empty()) {
            context.clipToRect(BLRect(0.0, 0.0, 0.0, 0.0));
            return;
        }
        // the device transform maps the logical clip onto the raster it was pushed for (§6.3, AD-27)
        const Transform device = m_transform.then(Transform::scaling(m_device_scale, m_device_scale));
        // a rotated or skewed clip is applied exactly, which no pixel grid can snap without moving it (§6.2)
        const bool axis_aligned = std::abs(device.skew_x) < 0.0001f && std::abs(device.skew_y) < 0.0001f;
        if (!axis_aligned) {
            context.clipToRect(BLRect(rect.x, rect.y, rect.width, rect.height));
            return;
        }
        // the two corners map onto the raster, which is snapped to the pixel grid the gpu scissor uses (§6.2, AD-26)
        const Point origin = device.apply(Point{.x = rect.x, .y = rect.y});
        const Point corner = device.apply(Point{.x = rect.x + rect.width, .y = rect.y + rect.height});
        const float left = std::round(std::min(origin.x, corner.x));
        const float top = std::round(std::min(origin.y, corner.y));
        const float right = std::round(std::max(origin.x, corner.x));
        const float bottom = std::round(std::max(origin.y, corner.y));
        // the snapped box maps back into the logical space the raster context clips in (§6.3)
        const float det = device.scale_x * device.scale_y - device.skew_x * device.skew_y;
        const auto to_user = [&](float x, float y, float& out_x, float& out_y) {
            // the inverse of the affine device transform, which maps a raster point onto logical space (§6.3)
            out_x = (device.scale_y * x - device.skew_x * y) / det;
            out_y = (-device.skew_y * x + device.scale_x * y) / det;
        };
        float user_left = 0.0f;
        float user_top = 0.0f;
        float user_right = 0.0f;
        float user_bottom = 0.0f;
        // the corners are inverted with the translation removed, because the inverse is linear around it (§6.3)
        to_user(left - device.translate_x, top - device.translate_y, user_left, user_top);
        to_user(right - device.translate_x, bottom - device.translate_y, user_right, user_bottom);
        context.clipToRect(BLRect(user_left, user_top, user_right - user_left, user_bottom - user_top));
    }

    void Blend2DRenderContext::clip_rect(const Rect& rect) {
        // the clip freezes the transform it is pushed under, which a later transform must not move (§6.2)
        const Transform device = m_transform.then(Transform::scaling(m_device_scale, m_device_scale));
        // a clip pushed under a collapsed transform spans no device area, so it cuts everything away (§6.2)
        const float determinant = device.scale_x * device.scale_y - device.skew_x * device.skew_y;
        if (std::abs(determinant) < 1e-6f) {
            m_clip = Rect{};
            if (m_context != nullptr)
                m_context->clipToRect(BLRect(0.0, 0.0, 0.0, 0.0));
            return;
        }
        // the recorded clip is the intersection of what was there and what was asked for (§6.2)
        const float left = std::max(m_clip.x, rect.x);
        const float top = std::max(m_clip.y, rect.y);
        const float right = std::min(m_clip.x + m_clip.width, rect.x + rect.width);
        const float bottom = std::min(m_clip.y + m_clip.height, rect.y + rect.height);
        m_clip = Rect::from_origin_size(Point{left, top}, Size{std::max(0.0f, right - left), std::max(0.0f, bottom - top)});
        // a context with no raster context keeps its clip bookkeeping only (§6.2)
        if (m_context == nullptr)
            return;
        // the clip is applied as the raster context's cumulative clip, which a save restores (§6.2)
        apply_clip(*m_context, m_clip);
    }

    void Blend2DRenderContext::clip_rounded_rect(const Rect& rect, float radius) {
        // the rectangle narrows the clip like any other, which also bounds the rounded mask (§6.2)
        clip_rect(rect);
        // a context with no raster context keeps its clip bookkeeping only (§6.2)
        if (m_context == nullptr)
            return;
        // a save since the group began starts a nested group, so the clip ends where that save restores (§6.2)
        if (m_group == nullptr || (!m_states.empty() && m_states.back().group != m_group))
            begin_clip_group();
        // the shape is kept in the space it was asked for, which the mask maps device pixels back through (§6.2)
        if (m_group != nullptr)
            m_group->clips.push_back(RoundedClip{.rect = rect, .radius = std::max(0.0f, radius), .transform = m_transform});
    }

    void Blend2DRenderContext::set_opacity(float opacity) {
        // the opacity is clamped into the unit range, because content computes it from data (§6.1)
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float Blend2DRenderContext::opacity() const {
        // the opacity is read back by content that composes its own on top of it (§6.1)
        return m_opacity;
    }

    void Blend2DRenderContext::push_layer(float opacity) {
        // the layer owns the state it begins under, so an unmatched save inside it cannot shift the restore (§6.2)
        auto layer = std::make_unique<Layer>();
        layer->saved = State{m_transform, m_clip, m_opacity};
        layer->opacity = std::clamp(opacity, 0.0f, 1.0f);
        // a context with no raster context keeps the saved state only (§6.2)
        if (m_context == nullptr) {
            m_layers.push_back(std::move(layer));
            return;
        }
        layer->parent = m_context;
        // the offscreen raster spans the whole target, so the group keeps the device space it recorded in (§6.2)
        const int width = static_cast<int>(m_context->targetWidth());
        const int height = static_cast<int>(m_context->targetHeight());
        // a refused raster or context draws no group, which the pop then composites as nothing (§6.2)
        const bool ready = layer->image.create(width, height, BL_FORMAT_PRGB32) == BL_SUCCESS && layer->context.begin(layer->image) == BL_SUCCESS;
        layer->parent = ready ? m_context : nullptr;
        if (ready) {
            // the offscreen starts transparent, because a pixel the group never paints must composite nothing (§6.2)
            layer->context.clearAll();
            // the layer context maps logical points onto the same pixels the parent does (§6.3, AD-27)
            layer->context.scale(m_device_scale, m_device_scale);
            layer->context.userToMeta();
            layer->context.setTransform(to_blend_matrix(m_transform));
            // the clip the context stood at carries into the layer, because a group may not paint outside it (§6.2)
            layer->context.clipToRect(BLRect(m_clip.x, m_clip.y, m_clip.width, m_clip.height));
            // the parent is saved, so its state returns when the layer composites (§6.2)
            m_context->save();
            // the later draws land in the offscreen raster, which the pop composites (§6.2)
            m_context = &layer->context;
        }
        m_layers.push_back(std::move(layer));
    }

    void Blend2DRenderContext::pop_layer() {
        // a pop with no layer open is a no operation, because the pair is the caller's contract (§6.2)
        if (m_layers.empty())
            return;
        std::unique_ptr<Layer> layer = std::move(m_layers.back());
        m_layers.pop_back();
        // the state the layer saved becomes current again, which an inner save cannot shift (§6.2)
        m_transform = layer->saved.transform;
        m_clip = layer->saved.clip;
        m_opacity = layer->saved.opacity;
        // a context with no raster context kept the state only, so there is nothing to composite (§6.2)
        if (layer->parent == nullptr)
            return;
        // the parent state returns to what the push saved, which is the clip and transform the group began under (§6.2)
        m_context = layer->parent;
        m_context->restore();
        // the layer raster is finished, so its pixels are stable for the composite (§6.2)
        layer->context.end();
        m_context->save();
        // the blit maps device pixels one to one, so the meta scale is undone in the user matrix (§6.3)
        m_context->setTransform(BLMatrix2D::makeScaling(1.0 / m_device_scale, 1.0 / m_device_scale));
        m_context->setGlobalAlpha(layer->opacity);
        m_context->setCompOp(BL_COMP_OP_SRC_OVER);
        // the whole group lands once at its own opacity, which dims overlapping children together (§6.2)
        m_context->blitImage(BLRect(0.0, 0.0, layer->image.width(), layer->image.height()), layer->image);
        m_context->restore();
    }

    const Transform& Blend2DRenderContext::transform() const {
        // the recorded transform is what a test asserts against (§6.2)
        return m_transform;
    }

    const Rect& Blend2DRenderContext::clip() const {
        // the recorded clip is what a test asserts against (§6.2)
        return m_clip;
    }

    void Blend2DRenderContext::draw_rect(const Rect& rect, const Paint& paint) {
        // a plain and a rounded rectangle differ only in the shape they build (§6.2)
        paint_shape(paint, false, rect, 0.0f);
    }

    void Blend2DRenderContext::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // the corner radii are what distinguishes this shape from a plain rectangle (§6.2)
        paint_shape(paint, true, rect, radius);
    }

    void Blend2DRenderContext::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // a context with no raster context draws nothing (§6.2)
        if (m_context == nullptr)
            return;
        // a polyline with fewer than two vertices has no segment to stroke (§6.2)
        if (points.size() < 2 || stroke.width <= 0.0f)
            return;
        // the vertices are joined into one contour, which is what makes the elbows solid rather than notched (§6.2)
        BLPath path;
        path.moveTo(points.front().x, points.front().y);
        // every vertex after the first is a line, which is what a polyline is (§6.2)
        for (std::size_t index = 1; index < points.size(); index++)
            path.lineTo(points[index].x, points[index].y);
        // the stroke carries the colour with the opacity folded in, like every stroke of a frame (§6.1)
        m_context->setStrokeStyle(to_blend_color(stroke.color));
        m_context->setStrokeWidth(stroke.width);
        m_context->setCompOp(to_blend_comp(stroke.composition));
        // the ends and the elbows both close roundly, which is how a control glyph reads (§6.2)
        m_context->setStrokeCaps(BL_STROKE_CAP_ROUND);
        m_context->setStrokeJoin(BL_STROKE_JOIN_ROUND);
        // the polyline draws at the current transform, which is where the scene placed it (§6.2)
        m_context->strokePath(path);
        m_context->setCompOp(BL_COMP_OP_SRC_OVER);
    }

    void Blend2DRenderContext::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a context with no raster context casts nothing (§6.2)
        if (m_context == nullptr)
            return;
        // a cast needs both a colour and a shape, because one without the other puts no ink down (§6.2)
        if (!shadow.casts() || rect.empty())
            return;
        // a non finite caster or offset has no device rectangle, so it is refused before any raster is sized (§6.2)
        if (!shadow_is_finite(rect, shadow))
            return;
        // the cast is the caster moved by the shadow offset, which is what lifts it off the surface (§6.2)
        const Rect cast{.x = rect.x + shadow.offset.x, .y = rect.y + shadow.offset.y, .width = rect.width, .height = rect.height};
        // a blur of zero is a hard copy of the caster, which needs no offscreen pass (§6.2)
        if (shadow.blur <= 0.0f) {
            paint_shape(Paint{.fill = shadow.color}, radius > 0.0f, cast, radius);
            return;
        }
        // three sigmas is the reach of the blur, so the offscreen covers the whole soft edge (§6.2)
        const float reach = shadow.blur * 3.0f;
        // a reach or cast that overflows the float range has no offscreen extent (§6.2)
        if (!std::isfinite(reach) || !std::isfinite(cast.x) || !std::isfinite(cast.y))
            return;
        // offscreen is rendered at device resolution, so the blur is not softened twice by the meta scale (§6.2)
        const double scaled_width = (static_cast<double>(rect.width) + static_cast<double>(reach) * 2.0) * static_cast<double>(m_device_scale);
        const double scaled_height = (static_cast<double>(rect.height) + static_cast<double>(reach) * 2.0) * static_cast<double>(m_device_scale);
        // a device extent outside the int range cannot become an image, so it is refused rather than truncated (§6.2)
        if (!std::isfinite(scaled_width) || !std::isfinite(scaled_height) || scaled_width > static_cast<double>(std::numeric_limits<int>::max()) || scaled_height > static_cast<double>(std::numeric_limits<int>::max()))
            return;
        // the offscreen takes the ceiling of the scaled extent, because a partial pixel still needs a column (§6.2)
        const int width = std::max(1, static_cast<int>(std::ceil(scaled_width)));
        const int height = std::max(1, static_cast<int>(std::ceil(scaled_height)));
        // the blur radius is bounded before rounding, because a wider window cannot be summed in an int (§6.2)
        const double scaled_blur = static_cast<double>(shadow.blur) * static_cast<double>(m_device_scale);
        if (!std::isfinite(scaled_blur) || scaled_blur > static_cast<double>(std::numeric_limits<int>::max()) / 2.0)
            return;
        // the offscreen is premultiplied, matching the raster the frame draws into (§6.3, AD-26)
        BLImage offscreen;
        if (offscreen.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS)
            return;
        {
            // the pass renders the silhouette alone, so the blur sees nothing but the casting shape (§6.2)
            BLContext pass;
            if (pass.begin(offscreen) != BL_SUCCESS)
                return;
            // the pass clears to transparent, because only the silhouette puts ink down (§6.2)
            pass.clearAll();
            // the pass draws at device resolution, which places the caster at the reach inside the offscreen (§6.3, AD-27)
            pass.setTransform(BLMatrix2D(m_device_scale, 0.0, 0.0, m_device_scale, m_device_scale * (reach - rect.x), m_device_scale * (reach - rect.y)));
            pass.setFillStyle(to_blend_color(shadow.color));
            // a rounded caster keeps its radii in the cast, clamped like every rounded shape (§6.2)
            if (radius > 0.0f)
                pass.fillRoundRect(rect.x, rect.y, rect.width, rect.height, std::min(radius, std::min(rect.width, rect.height) * 0.5f));
            else
                pass.fillRect(rect.x, rect.y, rect.width, rect.height);
            // the pass flushes before the silhouette is blurred in place (§6.2)
            pass.end();
        }
        // the silhouette is blurred in place, which is the soft edge blend2d has no primitive for (§6.2, AD-26)
        BLImageData data;
        if (offscreen.makeMutable(&data) != BL_SUCCESS)
            return;
        blur_box(data, static_cast<int>(std::lround(scaled_blur)));
        // the blurred cast is blitted one to one at device resolution, which the meta scale maps back (§6.2)
        m_context->blitImage(BLRect(cast.x - reach, cast.y - reach, static_cast<double>(width) / m_device_scale, static_cast<double>(height) / m_device_scale), offscreen);
    }

    void Blend2DRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a context with no raster context draws nothing (§6.2)
        if (m_context == nullptr)
            return;
        // an empty run has no glyphs to draw, so it costs nothing (§6.2)
        if (layout.empty())
            return;
        // the text takes the fill colour when the paint fills, and the stroke colour otherwise (§6.2)
        m_context->setFillStyle(to_blend_color(paint.fills() ? paint.fill : paint.stroke));
        m_context->setCompOp(to_blend_comp(paint.composition));
        // the run is the shaped output stage one measured, so the glyphs are replayed as positioned (§9.1, AD-18)
        const ShapedRun& run = *layout.run;
        // every line carries its own typeface and its own baseline (§9.1)
        for (const ShapedRun::Line& line : run.lines()) {
            // a line with no glyph draws nothing, which a space is (§6.2)
            if (line.glyphs.empty())
                continue;
            // the font is resolved from the registered bytes, because blend2d loads a face rather than borrowing (§9.2)
            BLFont font = resolve_font(run.font_hash(), run.font_size());
            // a line this backend cannot resolve is skipped rather than guessed at (§3.5, §9.1)
            if (font.empty())
                continue;
            // the glyph indices and their positions are packed into the two arrays a glyph run reads (§9.1)
            std::vector<std::uint32_t> glyph_ids;
            glyph_ids.reserve(line.glyphs.size());
            std::vector<BLPoint> placements;
            placements.reserve(line.glyphs.size());
            // every shaped glyph contributes its index and its position relative to the line origin (§9.1)
            for (const ShapedGlyph& glyph : line.glyphs) {
                glyph_ids.push_back(glyph.id);
                placements.push_back(BLPoint(glyph.position.x, glyph.position.y));
            }
            // the run carries absolute user space placements, which the glyph outline pass honours (§9.1)
            BLGlyphRun glyph_run;
            glyph_run.glyphData = glyph_ids.data();
            glyph_run.placementData = placements.data();
            glyph_run.size = glyph_ids.size();
            glyph_run.reserved = 0;
            glyph_run.placementType = BL_GLYPH_PLACEMENT_TYPE_USER_UNITS;
            glyph_run.glyphAdvance = static_cast<std::int8_t>(sizeof(std::uint32_t));
            glyph_run.placementAdvance = static_cast<std::int8_t>(sizeof(BLPoint));
            glyph_run.flags = BL_GLYPH_RUN_NO_FLAGS;
            // the line origin is the baseline, added to the run origin the caller placed the run at (§9.1)
            m_context->fillGlyphRun(BLPoint(origin.x + line.origin.x, origin.y + line.origin.y), font, glyph_run);
        }
        m_context->setCompOp(BL_COMP_OP_SRC_OVER);
    }

    std::size_t Blend2DRenderContext::depth() const {
        // the depth is what a test asserts the saves and restores produced (§6.2)
        return m_states.size();
    }

    void Blend2DRenderContext::finish() {
        // a top level rounded clip is closed at the end of the frame, because no save owns it (§6.2)
        while (m_group != nullptr)
            flatten_clip_group();
    }

    void Blend2DRenderContext::begin_clip_group() {
        // a context with no raster context keeps no offscreen, which the rectangular clip above already covers (§6.2)
        BLImage* destination = m_context != nullptr ? m_context->targetImage() : nullptr;
        BLImageData destination_data{};
        auto group = std::make_unique<ClipGroup>();
        // a destination blend2d cannot describe keeps the rectangular clip rather than guessing (§6.2)
        bool ready = destination != nullptr && destination->getData(&destination_data) == BL_SUCCESS;
        if (ready) {
            group->parent = m_context;
            group->parent_group = m_group;
            group->parent_state = State{m_transform, m_clip, m_opacity, m_group};
            // the group and its backup match the destination, so a pixel keeps its meaning across the composite (§6.2)
            ready = group->target.create(destination_data.size.w, destination_data.size.h, BL_FORMAT_PRGB32) == BL_SUCCESS && group->backup.create(destination_data.size.w, destination_data.size.h, BL_FORMAT_PRGB32) == BL_SUCCESS;
        }
        if (ready)
            ready = group->context.begin(group->target) == BL_SUCCESS;
        if (ready) {
            // the group starts as a copy of the destination, so the draws inside see the pixels they blend with (§6.2)
            group->context.blitImage(BLPoint(0.0, 0.0), *destination);
            // the group draws in the same space the frame was recorded in (§6.3, AD-27)
            group->context.scale(m_device_scale, m_device_scale);
            group->context.userToMeta();
            group->context.setTransform(to_blend_matrix(m_transform));
            // the group clips to the intersected rectangle, which bounds the work the mask does (§6.2)
            apply_clip(group->context, m_clip);
            // the backup keeps the destination the mask leaves outside the shape (§6.2)
            BLContext copy;
            if (copy.begin(group->backup) == BL_SUCCESS) {
                copy.blitImage(BLPoint(0.0, 0.0), *destination);
                copy.end();
            }
            // the later draws land in the group, which its flatten composites through the rounded mask (§6.2)
            m_group = group.get();
            m_groups.push_back(std::move(group));
            m_context = &m_groups.back()->context;
        }
    }

    void Blend2DRenderContext::flatten_clip_group() {
        std::unique_ptr<ClipGroup> group = std::move(m_groups.back());
        m_groups.pop_back();
        // the group raster is final, so its pixels and its backup are stable to read (§6.2)
        group->context.end();
        BLImageData source_data{};
        BLImageData backup_data{};
        group->target.getData(&source_data);
        group->backup.getData(&backup_data);
        const int width = source_data.size.w;
        const int height = source_data.size.h;
        // the rounded clips restrict the mask to their intersection, so a box no clip reaches is skipped (§6.2)
        bool restricted = false;
        DeviceBounds bounds{0, 0, width, height};
        for (const RoundedClip& clip : group->clips) {
            // a clip that is singular or has no radius covers fully, so it cannot narrow the box (§6.2)
            DeviceBounds clip_bounds;
            if (!rounded_clip_bounds(clip, m_device_scale, width, height, clip_bounds))
                continue;
            // the first bounded clip starts the intersection and later ones shrink it (§6.2)
            if (!restricted) {
                bounds = clip_bounds;
                restricted = true;
            }
            else {
                bounds.left = std::max(bounds.left, clip_bounds.left);
                bounds.top = std::max(bounds.top, clip_bounds.top);
                bounds.right = std::min(bounds.right, clip_bounds.right);
                bounds.bottom = std::min(bounds.bottom, clip_bounds.bottom);
            }
            // an empty intersection leaves every pixel at its backup, so no further clipping can add one (§6.2)
            if (bounds.empty())
                break;
        }
        // every pixel the shapes cover is the group, and every pixel outside keeps the backup (§6.2)
        for (int y = restricted ? bounds.top : 0; y < (restricted ? bounds.bottom : height); y++) {
            const auto* source_row = static_cast<const std::uint8_t*>(source_data.pixelData) + static_cast<std::intptr_t>(y) * source_data.stride;
            auto* result_row = static_cast<std::uint8_t*>(backup_data.pixelData) + static_cast<std::intptr_t>(y) * backup_data.stride;
            for (int x = restricted ? bounds.left : 0; x < (restricted ? bounds.right : width); x++) {
                // the clips intersect, so their coverages multiply at the pixel centre (§6.2)
                float coverage = 1.0f;
                for (const RoundedClip& clip : group->clips)
                    coverage *= rounded_coverage(clip, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, m_device_scale);
                // a pixel the mask leaves alone already holds the destination (§6.2)
                if (coverage <= 0.0f)
                    continue;
                const std::uint8_t* source = source_row + static_cast<std::size_t>(x) * 4;
                std::uint8_t* result = result_row + static_cast<std::size_t>(x) * 4;
                for (std::size_t channel = 0; channel < 4; channel++) {
                    // the premultiplied group blends over the destination by the mask's coverage (§6.2)
                    const float value = static_cast<float>(source[channel]) * coverage + static_cast<float>(result[channel]) * (1.0f - coverage);
                    result[channel] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
                }
            }
        }
        // the masked result replaces the destination in one source copy, which the parent context performs (§6.2)
        BLContext* parent = group->parent != nullptr ? group->parent : m_context;
        if (parent != nullptr && width > 0 && height > 0) {
            parent->save();
            parent->setGlobalAlpha(1.0);
            parent->setCompOp(BL_COMP_OP_SRC_COPY);
            parent->setTransform(BLMatrix2D::makeScaling(1.0 / m_device_scale, 1.0 / m_device_scale));
            parent->blitImage(BLRect(0.0, 0.0, static_cast<double>(width) / m_device_scale, static_cast<double>(height) / m_device_scale), group->backup);
            parent->restore();
        }
        // the parent context becomes current again, with the state the group began under (§6.2)
        m_context = parent;
        m_group = group->parent_group;
        m_transform = group->parent_state.transform;
        m_clip = group->parent_state.clip;
        m_opacity = group->parent_state.opacity;
    }

    BLRgba32 Blend2DRenderContext::to_blend_color(const Color& color) const {
        // with_alpha already scales the colour's own alpha, so passing the opacity alone scales it once (§6.1)
        const Color dimmed = color.with_alpha(m_opacity);
        // the albor colour is in the zero to one range, while blend2d packs one byte per channel (§6.3)
        const auto channel = [](float value) -> std::uint8_t {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(clamped * 255.0f);
        };
        // the channels are given in the red, green, blue and alpha order blend2d stores them in (§6.3)
        return BLRgba32(channel(dimmed.red), channel(dimmed.green), channel(dimmed.blue), channel(dimmed.alpha));
    }

    void Blend2DRenderContext::paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius) {
        // a context with no raster context draws nothing (§6.2)
        if (m_context == nullptr)
            return;
        // a shape with no area contributes nothing, so it is skipped rather than drawn degenerate (§6.2)
        if (rect.empty())
            return;
        // a rounded shape clamps its radius, because a radius larger than half the extent cannot be drawn (§6.2)
        const double effective_radius = rounded ? static_cast<double>(std::min(radius, std::min(rect.width, rect.height) * 0.5f)) : 0.0;
        m_context->setCompOp(to_blend_comp(paint.composition));
        // a transparent fill draws nothing, which is how a paint carries only a stroke (§6.2)
        if (paint.fills()) {
            m_context->setFillStyle(to_blend_color(paint.fill));
            // the fill of a plain and of a rounded rectangle differ only in the shape itself (§6.2)
            if (rounded)
                m_context->fillRoundRect(rect.x, rect.y, rect.width, rect.height, effective_radius);
            else
                m_context->fillRect(rect.x, rect.y, rect.width, rect.height);
        }
        // a transparent or a zero width stroke draws nothing (§6.2)
        if (paint.strokes()) {
            m_context->setStrokeStyle(to_blend_color(paint.stroke));
            m_context->setStrokeWidth(paint.stroke_width);
            // the default butt cap and miter join are restored here, because a polyline leaves round ones behind (§6.2)
            m_context->setStrokeCaps(BL_STROKE_CAP_BUTT);
            m_context->setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
            // the stroke of a plain and of a rounded rectangle differ only in the shape itself (§6.2)
            if (rounded)
                m_context->strokeRoundRect(rect.x, rect.y, rect.width, rect.height, effective_radius);
            else
                m_context->strokeRect(rect.x, rect.y, rect.width, rect.height);
        }
        m_context->setCompOp(BL_COMP_OP_SRC_OVER);
    }

    BLMatrix2D Blend2DRenderContext::to_blend_matrix(const Transform& transform) {
        // the matrix mirrors the albor transform with the same column order the scale, skew and translate follow (§6.3)
        return BLMatrix2D(transform.scale_x, transform.skew_y, transform.skew_x, transform.scale_y, transform.translate_x, transform.translate_y);
    }

    BLPath Blend2DRenderContext::build_path(std::span<const PathVerb> verbs, std::span<const Point> points) {
        // the path starts empty, because a malformed stream is refused before it reaches here (§6.4, AD-06)
        BLPath path;
        // the point index advances with every consuming verb (§6.4)
        std::size_t point_index = 0;
        for (const PathVerb verb : verbs) {
            // a move starts a new contour at the next point (§6.4)
            if (verb == PathVerb::MoveTo) {
                path.moveTo(points[point_index].x, points[point_index].y);
                point_index++;
            }
            // a line draws to the next point (§6.4)
            else if (verb == PathVerb::LineTo) {
                path.lineTo(points[point_index].x, points[point_index].y);
                point_index++;
            }
            // a quadratic curves through one control point to the end point (§6.4)
            else if (verb == PathVerb::QuadraticTo) {
                path.quadTo(points[point_index].x, points[point_index].y, points[point_index + 1].x, points[point_index + 1].y);
                point_index += 2;
            }
            // a cubic curves through two control points to the end point (§6.4)
            else if (verb == PathVerb::CubicTo) {
                path.cubicTo(points[point_index].x, points[point_index].y, points[point_index + 1].x, points[point_index + 1].y, points[point_index + 2].x, points[point_index + 2].y);
                point_index += 3;
            }
            // a close runs the contour back to its start and consumes no point (§6.4)
            else if (verb == PathVerb::Close) {
                path.close();
            }
        }
        return path;
    }

    BLFont Blend2DRenderContext::resolve_font(std::uint64_t font_hash, float font_size) {
        // the registered font is the only byte source here, because a handle borrows a foreign typeface (§9.2)
        const std::shared_ptr<const resources::EmbeddedFont> embedded = resources::FontRegistry::find_hash(font_hash);
        // a run whose font is not registered carries no bytes this backend can load, so it is skipped (§9.2, AD-34)
        if (embedded == nullptr)
            return BLFont();
        // the lookup and the cache share one lock, because a second render thread races otherwise (§9.2, AD-18)
        const std::lock_guard<std::mutex> lock(font_face_cache_mutex());
        // the cache is keyed by content hash, because a face is immutable and independent of size (§9.2, §15)
        std::unordered_map<std::uint64_t, CachedFontFace>& faces = font_face_cache();
        auto found = faces.find(font_hash);
        if (found == faces.end()) {
            // the entry owns a copy of the bytes, because the registry may clear while a face still draws (§9.2)
            CachedFontFace entry;
            entry.bytes.assign(embedded->bytes.begin(), embedded->bytes.end());
            // the data borrows the owned bytes, which outlive it inside the cache entry (§9.2, AD-34)
            if (entry.data.createFromData(entry.bytes.data(), entry.bytes.size()) != BL_SUCCESS)
                return BLFont();
            // the face is built from the data, which borrows the bytes the entry keeps (§9.2)
            if (entry.face.createFromData(entry.data, 0) != BL_SUCCESS)
                return BLFont();
            found = faces.emplace(font_hash, std::move(entry)).first;
        }
        // the font is built at the size the run was shaped at, which maps the outlines to logical pixels (§9.1)
        BLFont font;
        if (font.createFromFace(found->second.face, font_size) != BL_SUCCESS)
            return BLFont();
        return font;
    }

    void Blend2DRenderContext::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a context with no raster context draws nothing (§6.4)
        if (m_context == nullptr)
            return;
        // a batch with no segment draws nothing (§6.4, AD-06)
        if (!batch || batch->segment_count() == 0)
            return;
        // every consecutive point pair lifts to one segment, which the batched replay walks (§6.4)
        const std::span<const Point> points = batch->points();
        BLPath path;
        for (std::size_t index = 0; index < points.size(); index += 2) {
            path.moveTo(points[index].x, points[index].y);
            path.lineTo(points[index + 1].x, points[index + 1].y);
        }
        // the retained transform concats under the current one, so the segments land where the scene placed them (§6.4)
        m_context->save();
        m_context->applyTransform(to_blend_matrix(transform));
        m_context->setStrokeStyle(to_blend_color(stroke.color));
        m_context->setStrokeWidth(stroke.width);
        m_context->setCompOp(to_blend_comp(stroke.composition));
        // the butt cap and miter join match the skia reference, which sets neither for a batched stroke (§6.2, AD-26)
        m_context->setStrokeCaps(BL_STROKE_CAP_BUTT);
        m_context->setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
        m_context->strokePath(path);
        m_context->restore();
    }

    void Blend2DRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a context with no raster context draws nothing (§6.4)
        if (m_context == nullptr)
            return;
        // a path with no command draws nothing (§6.4, AD-06)
        if (!path || path->verbs().empty())
            return;
        // the contour is rebuilt in one pass, because the resource validates the command stream on creation (§6.4, AD-06)
        const BLPath contour = build_path(path->verbs(), path->points());
        // the retained transform concats under the current one, so the contour lands where the scene placed it (§6.4)
        m_context->save();
        m_context->applyTransform(to_blend_matrix(transform));
        m_context->setCompOp(to_blend_comp(paint.composition));
        m_context->setFillRule(paint.fill_rule == FillRule::EvenOdd ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
        // a transparent fill draws nothing, which is how the paint carries only a stroke (§6.4)
        if (paint.fills()) {
            m_context->setFillStyle(to_blend_color(paint.fill));
            m_context->fillPath(contour);
        }
        // a transparent or a zero width stroke draws nothing (§6.4)
        if (paint.strokes()) {
            m_context->setStrokeStyle(to_blend_color(paint.stroke));
            m_context->setStrokeWidth(paint.stroke_width);
            // the default butt cap and miter join are what a contour strokes with, like the skia reference (§6.2)
            m_context->setStrokeCaps(BL_STROKE_CAP_BUTT);
            m_context->setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
            m_context->strokePath(contour);
        }
        m_context->restore();
    }

    void Blend2DRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture with no command draws nothing (§6.4, AD-06)
        if (!picture || picture->empty())
            return;
        // the list nests under the transform the scene gave it, like any other retained draw (§6.4)
        if (m_context != nullptr) {
            m_context->save();
            m_context->applyTransform(to_blend_matrix(transform));
        }
        // the bookkeeping follows the context, so a command that reads the transform back sees the same one (§6.2)
        const Transform outer = m_transform;
        m_transform = transform.then(outer);
        // the recorded clip applies to the list, and a narrower one opens a switch of its own (§6.2)
        replay_commands(picture->commands(), *this, m_clip);
        // the transform the picture was drawn under is restored with the context (§6.4)
        m_transform = outer;
        // the list is closed, because a picture draws inside the scope it was given (§6.2)
        if (m_context != nullptr)
            m_context->restore();
    }

    void Blend2DRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        if (m_context == nullptr)
            return;
        if (!image || image->pixels().empty())
            return;
        if (destination.empty())
            return;
        // the extent is the pixel grid the raster and the drawable share, which a zero size cannot host (§6.4)
        const int width = static_cast<int>(image->width());
        const int height = static_cast<int>(image->height());
        if (width <= 0 || height <= 0)
            return;
        // the reference treats image bytes as premultiplied rgba8, a fill paints opaque to the alpha (§6.4, AD-26, §6.1)
        float paint_alpha = 1.0f;
        if (paint.fills())
            paint_alpha = paint.fill.alpha;
        else if (paint.strokes())
            paint_alpha = paint.stroke.alpha;
        paint_alpha = std::clamp(paint_alpha * m_opacity, 0.0f, 1.0f);
        const int modulation = static_cast<int>(std::lround(paint_alpha * 255.0f));
        // the modulation rounds to the nearest byte, because truncation darkens a translucent image (§6.4, AD-26)
        const auto modulated = [modulation](std::uint8_t value) -> std::uint8_t {
            // the half divisor rounds the byte before the cast (§6.4)
            return static_cast<std::uint8_t>((static_cast<int>(value) * modulation + 127) / 255);
        };
        BLImage raster;
        if (raster.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS)
            return;
        BLImageData data;
        if (raster.makeMutable(&data) != BL_SUCCESS)
            return;
        // resource bytes are premultiplied rgba8, while raster is premultiplied bgra (§6.4, AD-26)
        const std::span<const std::uint8_t> source_pixels = image->pixels();
        for (int y = 0; y < height; y++) {
            const std::uint8_t* source = source_pixels.data() + static_cast<std::size_t>(y) * image->row_bytes();
            std::uint8_t* target = static_cast<std::uint8_t*>(data.pixelData) + static_cast<std::intptr_t>(y) * data.stride;
            for (int x = 0; x < width; x++) {
                // red and blue swap into bgra order, and the shared alpha modulation is applied (§6.4, AD-26)
                target[0] = modulated(source[2]);
                target[1] = modulated(source[1]);
                target[2] = modulated(source[0]);
                target[3] = modulated(source[3]);
                source += 4;
                target += 4;
            }
        }
        m_context->setCompOp(to_blend_comp(paint.composition));
        m_context->blitImage(BLRect(destination.x, destination.y, destination.width, destination.height), raster);
        m_context->setCompOp(BL_COMP_OP_SRC_OVER);
    }
} // namespace albor::blend2d
