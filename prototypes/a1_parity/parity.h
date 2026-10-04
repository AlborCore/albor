#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "prototypes/a1_dawn/resources.h"

namespace albor::a1_parity
{

    // the two renderers this slice compares: skia raster is the deterministic reference for what the geometry should
    // look like, and dawn is the candidate ad-28 proposes
    enum class Engine
    {
        skia_raster,
        dawn,
    };

    // the readable name of an engine, which the report prints and the results file quotes
    [[nodiscard]] const char* engine_name(Engine engine);

    // parse an engine name back, so one driver can drive every renderer without a branch per call site
    [[nodiscard]] bool engine_from_name(const char* name, Engine& engine);

    // one readback buffer of 8-bit channels, which is the only thing two renderers can be compared through
    struct PixelBuffer
    {
        std::size_t width = 0;
        std::size_t height = 0;
        std::vector<std::uint8_t> rgba; // four bytes per pixel, in r, g, b, a order

        // the number of bytes one row of pixels occupies, which readback has to respect
        [[nodiscard]] std::size_t stride() const { return width * 4; }

        void allocate(std::size_t pixel_width, std::size_t pixel_height);
    };

    // what one renderer is asked to draw: the retained geometry, the view transform to draw it through, and how many
    // of its segments to draw, which is what makes the zoom levels comparable
    struct RenderRequest
    {
        const albor::a1_dawn::ResourceSet* resources = nullptr;
        albor::a1_dawn::ViewTransform view{};
        std::size_t instances = 0;
    };

    // what one engine cost to draw one frame offscreen, which is reported but never used as a parity threshold
    struct FrameCost
    {
        std::size_t frames_drawn = 0;
        double total_ms = 0.0;
        double slowest_ms = 0.0;

        [[nodiscard]] double mean_ms() const { return frames_drawn == 0 ? 0.0 : total_ms / static_cast<double>(frames_drawn); }
    };

    // one offscreen renderer, which is the whole contract both engines implement
    class OffscreenRenderer
    {
    public:
        virtual ~OffscreenRenderer() = default;

        // the engine this renderer is, which the report labels the buffer with
        [[nodiscard]] virtual Engine engine() const = 0;

        // bring the renderer up at a pixel size, without a window, a window server or a display link
        [[nodiscard]] virtual bool create(std::size_t pixel_width, std::size_t pixel_height) = 0;

        // draw one frame into the offscreen target, timed by the caller so both engines are timed the same way
        virtual void render(const RenderRequest& request) = 0;

        // read the target back into cpu memory, which is what a comparison can be made on
        [[nodiscard]] virtual bool read_back(PixelBuffer& out) const = 0;
    };

    // how two readback buffers differ, which is the measurement this prototype exists to make
    struct PixelDiff
    {
        std::size_t differing_pixels = 0;
        std::size_t max_channel_delta = 0;
        std::size_t within_tolerance_pixels = 0; // differing pixels whose worst channel stayed within tolerance
        std::size_t total_pixels = 0;

        [[nodiscard]] double differing_fraction() const { return total_pixels == 0 ? 0.0 : static_cast<double>(differing_pixels) / static_cast<double>(total_pixels); }

        // the share of differing pixels that are only edge antialiasing rather than a different image
        [[nodiscard]] double within_tolerance_share() const { return differing_pixels == 0 ? 1.0 : static_cast<double>(within_tolerance_pixels) / static_cast<double>(differing_pixels); }
    };

    // compare two buffers channel by channel, counting a pixel as differing when any channel moved at all
    [[nodiscard]] PixelDiff diff_pixels(const PixelBuffer& a, const PixelBuffer& b, std::size_t tolerance);

    // compare two buffers after averaging each block of pixels down to one colour, which is the instrument that can
    // actually measure agreement between two antialiasing rasterisers: a partially covered edge is a legitimate
    // mid-tone, so comparing pixels one at a time cannot separate a different edge from a different image
    [[nodiscard]] PixelDiff diff_coverage(const PixelBuffer& a, const PixelBuffer& b, std::size_t block, std::size_t tolerance);

    // the ink profile down one column, which is what says how wide a stroke a renderer actually laid down
    [[nodiscard]] std::vector<double> ink_profile(const PixelBuffer& buffer, std::size_t column);

    // the measured width in pixels of the stroke a renderer laid down, which is how the cosmetic versus geometry
    // space question is settled by measurement instead of by assertion
    [[nodiscard]] double measure_stroke_width(const PixelBuffer& buffer, std::uint8_t red, std::uint8_t green, std::uint8_t blue);

    // the background a frame starts from, which both renderers clear to so a readback can be sanity checked
    inline constexpr float kClearRed = 0.05F;
    inline constexpr float kClearGreen = 0.06F;
    inline constexpr float kClearBlue = 0.08F;

    // the stroke colour both renderers are asked to lay down, in the 8-bit channels the readback carries
    inline constexpr std::uint8_t kStrokeRed = 255;
    inline constexpr std::uint8_t kStrokeGreen = 64;
    inline constexpr std::uint8_t kStrokeBlue = 32;

} // namespace albor::a1_parity
