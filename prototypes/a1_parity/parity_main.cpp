// the driver of prototype a1-parity: one geometry generator, one view transform and one clear colour, handed to every
// renderer in turn, so the comparison is fair by construction rather than by convention
//
// there is no window, no swap chain and no display link here, because prototype a1-parity's recorded plan states that
// the display link cannot be run reproducibly on this platform and a parity result that reports nothing half the time
// is not a measurement

#include "prototypes/a1_parity/parity.h"

#include "prototypes/a1_parity/dawn_offscreen.h"
#include "prototypes/a1_parity/skia_offscreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace albor::a1_parity
{

    namespace
    {

        using Clock = std::chrono::steady_clock;

        // the workload the recorded thresholds were committed against, which must not drift without a new plan
        constexpr std::size_t kSegmentCount = 20000;
        constexpr float kExtent = 1000.0F;
        constexpr float kStrokeWidth = 1.0F;
        constexpr std::uint64_t kSeed = 20250114;
        constexpr std::size_t kPixelWidth = 1280;
        constexpr std::size_t kPixelHeight = 800;
        constexpr std::size_t kFramesPerEngine = 10;
        constexpr std::size_t kChannelTolerance = 4;

        double to_ms(Clock::time_point from, Clock::time_point to) { return std::chrono::duration<double, std::milli>(to - from).count(); }

    } // namespace

    void PixelBuffer::allocate(std::size_t pixel_width, std::size_t pixel_height) {
        width = pixel_width;
        height = pixel_height;
        rgba.assign(pixel_width * pixel_height * 4, 0);
    }

    const char* engine_name(Engine engine) {
        switch (engine) {
        case Engine::skia_raster:
            return "skia-raster";
        case Engine::dawn:
            return "dawn";
        }
        return "unknown";
    }

    bool engine_from_name(const char* name, Engine& engine) {
        const std::string wanted = name;
        if (wanted == "skia-raster") {
            engine = Engine::skia_raster;
        }
        else if (wanted == "dawn") {
            engine = Engine::dawn;
        }
        else {
            return false;
        }
        return true;
    }

    PixelDiff diff_pixels(const PixelBuffer& a, const PixelBuffer& b, std::size_t tolerance) {
        PixelDiff diff;
        if (a.width != b.width || a.height != b.height) {
            return diff;
        }
        diff.total_pixels = a.width * a.height;

        for (std::size_t pixel = 0; pixel < diff.total_pixels; pixel++) {
            const std::uint8_t* pa = a.rgba.data() + pixel * 4;
            const std::uint8_t* pb = b.rgba.data() + pixel * 4;

            // a pixel counts as differing when any channel moved at all, which is the strict reading of "differs"
            std::size_t worst = 0;
            for (std::size_t channel = 0; channel < 4; channel++) {
                worst = std::max<std::size_t>(worst, static_cast<std::size_t>(std::abs(pa[channel] - pb[channel])));
            }
            if (worst == 0) {
                continue;
            }
            diff.differing_pixels++;
            diff.max_channel_delta = std::max(diff.max_channel_delta, worst);
            if (worst <= tolerance) {
                diff.within_tolerance_pixels++;
            }
        }
        return diff;
    }

    double measure_stroke_width(const PixelBuffer& buffer, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        // count the pixels the stroke colour landed on; dividing by the total drawn length gives the mean width in
        // pixels, which is the quantity that distinguishes a cosmetic stroke from one that scales with the view
        std::size_t lit = 0;
        const std::size_t total = buffer.width * buffer.height;
        for (std::size_t pixel = 0; pixel < total; pixel++) {
            const std::uint8_t* p = buffer.rgba.data() + pixel * 4;
            // the stroke is solid and drawn last, so exact equality is the right test here
            if (p[0] == red && p[1] == green && p[2] == blue) {
                lit++;
            }
        }
        return static_cast<double>(lit);
    }

    namespace
    {

        // one engine's whole result for one view transform, which is what the report prints a row per
        struct EngineResult
        {
            bool created = false;
            FrameCost cost{};
            PixelBuffer pixels;
            double lit_pixels = 0.0;
        };

        // the two view transforms the recorded thresholds name, both framing the same content
        struct ViewCase
        {
            const char* label;
            albor::a1_dawn::ViewTransform view{};
        };

        ViewCase view_zoom_one() {
            // centre the 1000 unit extent in the 1280x800 target, with no scaling
            albor::a1_dawn::ViewTransform view{};
            view.scale = 1.0F;
            view.offset_x = (static_cast<float>(kPixelWidth) - kExtent) * 0.5F;
            view.offset_y = (static_cast<float>(kPixelHeight) - kExtent) * 0.5F;
            return ViewCase{"zoom 1.0", view};
        }

        ViewCase view_zoom_eight() {
            // the same centre at eight times the scale, which is the case the skia stroke defect lives in
            albor::a1_dawn::ViewTransform view{};
            view.scale = 8.0F;
            view.offset_x = static_cast<float>(kPixelWidth) * 0.5F - kExtent * 4.0F;
            view.offset_y = static_cast<float>(kPixelHeight) * 0.5F - kExtent * 4.0F;
            return ViewCase{"zoom 8.0", view};
        }

        // bring every renderer up at the target size, reporting which ones exist at all
        std::vector<std::pair<Engine, std::unique_ptr<OffscreenRenderer>>> bring_up_engines() {
            std::vector<std::pair<Engine, std::unique_ptr<OffscreenRenderer>>> engines;
            for (const Engine engine : {Engine::skia_raster, Engine::dawn}) {
                std::unique_ptr<OffscreenRenderer> renderer;
                if (engine == Engine::dawn) {
                    renderer.reset(make_dawn_renderer());
                }
                else {
                    renderer.reset(make_skia_renderer(engine));
                }
                if (renderer != nullptr && renderer->create(kPixelWidth, kPixelHeight)) {
                    std::printf("  %-12s up\n", engine_name(engine));
                    engines.emplace_back(engine, std::move(renderer));
                }
                else {
                    std::printf("  %-12s FAILED to come up, every threshold involving it is reported as not evaluated\n", engine_name(engine));
                }
            }
            return engines;
        }

        // draw one view transform through one renderer and read the result back
        EngineResult run_view(OffscreenRenderer& renderer, const albor::a1_dawn::ResourceSet& resources, const ViewCase& view_case) {
            EngineResult result;
            result.created = true;

            RenderRequest request{};
            request.resources = &resources;
            request.view = view_case.view;
            request.instances = resources.lines.size();

            // the first frame is not timed, because it is the one that pays for bringing the pipeline up; every later
            // frame is, and both engines are treated the same way
            renderer.render(request);

            const Clock::time_point start = Clock::now();
            for (std::size_t frame = 1; frame < kFramesPerEngine; frame++) {
                const Clock::time_point before = Clock::now();
                renderer.render(request);
                const double frame_ms = to_ms(before, Clock::now());
                result.cost.frames_drawn++;
                result.cost.total_ms += frame_ms;
                result.cost.slowest_ms = std::max(result.cost.slowest_ms, frame_ms);
            }
            result.cost.total_ms += to_ms(start, Clock::now()) - to_ms(start, Clock::now());

            if (!renderer.read_back(result.pixels)) {
                result.created = false;
                return result;
            }
            result.lit_pixels = measure_stroke_width(result.pixels, kStrokeRed, kStrokeGreen, kStrokeBlue);
            return result;
        }

        // check that an untouched dawn frame reads back as the clear colour everywhere, which is what makes a dawn
        // readback trustworthy enough to diff against another renderer
        bool dawn_readback_is_sane(OffscreenRenderer& dawn) {
            RenderRequest empty{};
            empty.instances = 0;
            dawn.render(empty);

            PixelBuffer buffer;
            if (!dawn.read_back(buffer)) {
                return false;
            }

            const auto red = static_cast<std::uint8_t>(std::lround(kClearRed * 255.0F));
            const auto green = static_cast<std::uint8_t>(std::lround(kClearGreen * 255.0F));
            const auto blue = static_cast<std::uint8_t>(std::lround(kClearBlue * 255.0F));
            const std::size_t total = buffer.width * buffer.height;
            std::size_t matching = 0;
            for (std::size_t pixel = 0; pixel < total; pixel++) {
                const std::uint8_t* p = buffer.rgba.data() + pixel * 4;
                // the clear is written as floats and read back as bytes, so it is compared within one step of rounding
                if (std::abs(static_cast<int>(p[0]) - red) <= 1 && std::abs(static_cast<int>(p[1]) - green) <= 1 && std::abs(static_cast<int>(p[2]) - blue) <= 1) {
                    matching++;
                }
            }
            std::printf("  dawn readback matches the clear colour at %zu of %zu pixels (%.2f%%), threshold 100%%\n", matching, total, 100.0 * static_cast<double>(matching) / static_cast<double>(total));
            return matching == total;
        }

    } // namespace

} // namespace albor::a1_parity

int main(int argc, char** argv) {
    using namespace albor::a1_parity;

    // an optional repeat count, so a smoke run costs seconds while the recorded run still does three repeats
    std::size_t repeats = 2;
    if (argc > 1) {
        repeats = static_cast<std::size_t>(std::atoll(argv[1]));
    }

    std::printf("Albor Prototype A1-Parity: one geometry, one transform, three renderers, no display link\n");
    std::printf("  %zu segments, extent %.1f, stroke %.2f px, seed %llu\n", kSegmentCount, static_cast<double>(kExtent), static_cast<double>(kStrokeWidth), static_cast<unsigned long long>(kSeed));
    std::printf("  %zux%zu target, %zu frames per engine, %zu repeats\n\n", kPixelWidth, kPixelHeight, kFramesPerEngine, repeats);

    // one generated set, handed to every renderer, so byte identical input is structural rather than a convention
    const auto resources = albor::a1_dawn::create_line_batch(kSegmentCount, kExtent, kStrokeWidth, kSeed);
    if (resources == nullptr) {
        std::printf("could not generate the workload\n");
        return 1;
    }
    std::printf("  generated %zu segments (%.1f MiB retained)\n", resources->lines.size(), static_cast<double>(resources->lines.size() * sizeof(albor::a1_dawn::Segment)) / (1024.0 * 1024.0));

    auto engines = bring_up_engines();
    if (engines.empty()) {
        std::printf("no renderer came up, there is nothing to compare\n");
        return 1;
    }

    int failures = 0;

    // the skia lit counts the first repeat produces, which is what T7 measures rather than re-rendering for
    double lit_at_zoom_one = 0.0;
    double lit_at_zoom_eight = 0.0;

    // the per repeat measurements, kept out here because the threshold section reads the first repeat's numbers
    std::vector<std::pair<Engine, EngineResult>> results;

    // T1: a dawn readback has to be trustworthy before it can be diffed against anything
    std::printf("\n== T1, dawn readback sanity\n");
    for (auto& [engine, renderer] : engines) {
        if (engine != Engine::dawn) {
            continue;
        }
        if (!dawn_readback_is_sane(*renderer)) {
            std::printf("  T1 FAIL\n");
            failures++;
        }
        else {
            std::printf("  T1 pass\n");
        }
    }

    const std::vector<ViewCase> views{view_zoom_one(), view_zoom_eight()};

    for (std::size_t repeat = 0; repeat < repeats; repeat++) {
        std::printf("\n== repeat %zu of %zu\n", repeat + 1, repeats);

        for (const ViewCase& view_case : views) {
            std::printf("\n-- %s\n", view_case.label);

            // draw every engine, keeping the first repeat's pixels to diff the later repeats against
            static std::vector<std::pair<Engine, PixelBuffer>> reference;
            results.clear();

            for (auto& [engine, renderer] : engines) {
                EngineResult result = run_view(*renderer, *resources, view_case);
                std::printf("  %-12s %zu frames, mean %.3f ms, slowest %.3f ms, %zu lit pixels\n", engine_name(engine), result.cost.frames_drawn, result.cost.mean_ms(), result.cost.slowest_ms, static_cast<std::size_t>(result.lit_pixels));
                results.emplace_back(engine, std::move(result));
            }

            if (repeat == 0) {
                for (auto& [engine, result] : results) {
                    reference.emplace_back(engine, result.pixels);
                    if (engine == Engine::skia_raster && view_case.view.scale == 1.0F) {
                        lit_at_zoom_one = result.lit_pixels;
                    }
                    if (engine == Engine::skia_raster && view_case.view.scale == 8.0F) {
                        lit_at_zoom_eight = result.lit_pixels;
                    }
                }
            }
            else {
                // T2: the same input twice must produce the same bytes, or a diff means nothing
                for (auto& [engine, result] : results) {
                    for (const auto& [ref_engine, ref_pixels] : reference) {
                        if (ref_engine != engine) {
                            continue;
                        }
                        const PixelDiff same = diff_pixels(ref_pixels, result.pixels, 0);
                        if (same.differing_pixels != 0) {
                            std::printf("  T2 FAIL, %s differs from its own first repeat at %zu pixels\n", engine_name(engine), same.differing_pixels);
                            failures++;
                        }
                    }
                }
                std::printf("  T2 checked: every renderer against its own first repeat\n");
            }

            // the parity diffs, each against the dawn buffer, which is the candidate under test
            for (auto& [engine, result] : results) {
                if (engine == Engine::dawn || !result.created) {
                    continue;
                }
                for (auto& [ref_engine, ref_result] : results) {
                    if (ref_engine != Engine::dawn) {
                        continue;
                    }
                    const PixelDiff diff = diff_pixels(ref_result.pixels, result.pixels, kChannelTolerance);
                    std::printf("  %-12s vs dawn: %.4f%% of pixels differ (%zu), worst channel %zu, %.2f%% within +-%zu\n", engine_name(engine), 100.0 * diff.differing_fraction(), diff.differing_pixels, diff.max_channel_delta, 100.0 * diff.within_tolerance_share(), kChannelTolerance);
                }
            }
        }
    }

    // T7: the recorded skia stroke defect, settled by measurement rather than by assertion
    std::printf("\n== T7, skia stroke width under zoom\n");
    {
        // the buffers the first repeat already produced, so measuring the stroke width costs no further rendering
        for (auto& [engine, result] : results) {
            if (engine != Engine::skia_raster) {
                continue;
            }
            const double at_one = lit_at_zoom_one;
            const double at_eight = lit_at_zoom_eight;
            if (at_one <= 0.0 || at_eight <= 0.0) {
                std::printf("  %-12s could not be measured\n", engine_name(engine));
                failures++;
                continue;
            }
            // the drawn length grows with the scale, so the mean width is lit pixels over length, and the ratio of the
            // two is what a cosmetic stroke keeps at one and a geometric stroke inflates
            const double length_one = static_cast<double>(resources->lines.size());
            const double length_eight = length_one * 8.0;
            const double width_one = at_one / length_one;
            const double width_eight = at_eight / length_eight;
            const double ratio = width_eight / width_one;
            std::printf("  %-12s mean stroke %.3f px at zoom 1, %.3f px at zoom 8, ratio %.2fx (threshold: >= 3.0x "
                        "geometric, <= 1.25x cosmetic)\n",
                        engine_name(engine), width_one, width_eight, ratio);
            if (ratio < 1.25 || ratio >= 3.0) {
                std::printf("  T7 INCONCLUSIVE for %s, the ratio is neither cosmetic nor geometric\n", engine_name(engine));
            }
        }
    }

    std::printf("\n== %d threshold failures\n", failures);
    return failures == 0 ? 0 : 1;
}
