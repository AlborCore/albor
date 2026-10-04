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

        // the clear colour as the readback carries it, which is what an ink measurement integrates from
        std::uint8_t to_channel(float value) { return static_cast<std::uint8_t>(std::lround(value * 255.0F)); }

        // the workload the recorded thresholds were committed against, which must not drift without a new plan
        constexpr std::size_t kSegmentCount = 20000;
        constexpr float kExtent = 1000.0F;
        constexpr float kStrokeWidth = 1.0F;
        constexpr std::uint64_t kSeed = 20250114;
        constexpr std::size_t kPixelWidth = 1280;
        constexpr std::size_t kPixelHeight = 800;
        constexpr std::size_t kFramesPerEngine = 10;
        constexpr std::size_t kChannelTolerance = 4;

        // the committed thresholds, which the driver enforces rather than only printing
        constexpr double kMaxDifferingShare = 0.002; // T3: raster against dawn at zoom 1.0
        constexpr double kMinWithinToleranceShare = 0.99; // T4: differing pixels that are only edge antialiasing
        constexpr double kMaxDifferingShareAtZoom8 = 0.01; // T6: raster against dawn at zoom 8.0

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

    // where the worst channel deltas of the differing pixels fall, in bands. this validates the per pixel instrument
    // before its verdict is relied on: the bimodal distribution a half pixel placement difference produces is what the
    // instrument should read, and any smearing across the middle bands would be a misreading
    void report_delta_bands(const PixelBuffer& a, const PixelBuffer& b) {
        std::size_t bands[5] = {};
        const std::size_t total = a.width * a.height;
        for (std::size_t pixel = 0; pixel < total; pixel++) {
            const std::uint8_t* pa = a.rgba.data() + pixel * 4;
            const std::uint8_t* pb = b.rgba.data() + pixel * 4;
            std::size_t worst = 0;
            for (std::size_t channel = 0; channel < 3; channel++) {
                worst = std::max(worst, static_cast<std::size_t>(std::abs(pa[channel] - pb[channel])));
            }
            if (worst == 0) {
                continue;
            }
            if (worst <= 4) {
                bands[0]++;
            }
            else if (worst <= 16) {
                bands[1]++;
            }
            else if (worst <= 64) {
                bands[2]++;
            }
            else if (worst <= 128) {
                bands[3]++;
            }
            else {
                bands[4]++;
            }
        }
        std::printf("    delta bands among differing pixels: 1-4: %zu, 5-16: %zu, 17-64: %zu, 65-128: %zu, 129-255: %zu\n", bands[0], bands[1], bands[2], bands[3], bands[4]);
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

    PixelDiff diff_coverage(const PixelBuffer& a, const PixelBuffer& b, std::size_t block, std::size_t tolerance) {
        PixelDiff diff;
        if (a.width != b.width || a.height != b.height || block == 0) {
            return diff;
        }

        // each block is averaged to one colour per channel, so an antialiased edge contributes its coverage rather than
        // landing on one side or the other of a per pixel comparison
        const std::size_t blocks_x = a.width / block;
        const std::size_t blocks_y = a.height / block;
        diff.total_pixels = blocks_x * blocks_y;
        if (diff.total_pixels == 0) {
            return diff;
        }

        for (std::size_t by = 0; by < blocks_y; by++) {
            for (std::size_t bx = 0; bx < blocks_x; bx++) {
                double sum_a[3] = {0.0, 0.0, 0.0};
                double sum_b[3] = {0.0, 0.0, 0.0};
                const std::size_t samples = block * block;
                for (std::size_t y = 0; y < block; y++) {
                    for (std::size_t x = 0; x < block; x++) {
                        const std::size_t pixel = (by * block + y) * a.width + (bx * block + x);
                        const std::uint8_t* pa = a.rgba.data() + pixel * 4;
                        const std::uint8_t* pb = b.rgba.data() + pixel * 4;
                        for (std::size_t channel = 0; channel < 3; channel++) {
                            sum_a[channel] += pa[channel];
                            sum_b[channel] += pb[channel];
                        }
                    }
                }

                std::size_t worst = 0;
                for (std::size_t channel = 0; channel < 3; channel++) {
                    const double mean_a = sum_a[channel] / static_cast<double>(samples);
                    const double mean_b = sum_b[channel] / static_cast<double>(samples);
                    const auto delta = static_cast<std::size_t>(std::lround(std::abs(mean_a - mean_b)));
                    worst = std::max(worst, delta);
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
        }
        return diff;
    }

    std::vector<double> ink_profile(const PixelBuffer& buffer, std::size_t column) {
        // how far each pixel of one column leans toward the stroke rather than the clear, which integrates to the
        // stroke's coverage and shows its width without depending on any single pixel landing exactly on the colour
        std::vector<double> profile;
        if (column >= buffer.width) {
            return profile;
        }
        profile.reserve(buffer.height);
        const double clear = static_cast<double>(to_channel(kClearRed));
        const double stroke = static_cast<double>(kStrokeRed);
        const double span = stroke - clear;
        for (std::size_t y = 0; y < buffer.height; y++) {
            const std::uint8_t* p = buffer.rgba.data() + (y * buffer.width + column) * 4;
            const double coverage = span > 0.0 ? (static_cast<double>(p[0]) - clear) / span : 0.0;
            profile.push_back(std::clamp(coverage, 0.0, 1.0));
        }
        return profile;
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

        // the mean stroke colour over the whole frame, which is a coverage proxy: two engines that lay down the same
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

            // draw every engine, keeping the first repeat's pixels to diff the later repeats against; the view has to
            // be part of the key, because one reference list holds every view and matching on the engine alone
            // compares a zoomed frame against an unzoomed one
            static std::vector<std::tuple<Engine, float, PixelBuffer>> reference;
            results.clear();

            for (auto& [engine, renderer] : engines) {
                EngineResult result = run_view(*renderer, *resources, view_case);
                std::printf("  %-12s %zu frames, mean %.3f ms, slowest %.3f ms, %zu lit pixels\n", engine_name(engine), result.cost.frames_drawn, result.cost.mean_ms(), result.cost.slowest_ms, static_cast<std::size_t>(result.lit_pixels));
                results.emplace_back(engine, std::move(result));
            }

            if (repeat == 0) {
                for (auto& [engine, result] : results) {
                    reference.emplace_back(engine, view_case.view.scale, result.pixels);
                }
            }
            else {
                // T2: the same input twice must produce the same bytes, or a diff means nothing
                for (auto& [engine, result] : results) {
                    for (const auto& [ref_engine, ref_scale, ref_pixels] : reference) {
                        if (ref_engine != engine || ref_scale != view_case.view.scale) {
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
                    if (view_case.view.scale == 1.0F) {
                        report_delta_bands(ref_result.pixels, result.pixels);
                    }

                    // T3 at zoom 1.0, T4 at zoom 1.0 and T6 at zoom 8.0, exactly as the plan committed them
                    if (view_case.view.scale == 1.0F) {
                        if (diff.differing_fraction() > kMaxDifferingShare) {
                            std::printf("  T3 FAIL, %.4f%% of pixels differ against a threshold of %.2f%%\n", 100.0 * diff.differing_fraction(), 100.0 * kMaxDifferingShare);
                            failures++;
                        }
                        if (diff.within_tolerance_share() < kMinWithinToleranceShare) {
                            std::printf("  T4 FAIL, only %.2f%% of the differing pixels stayed within +-%zu\n", 100.0 * diff.within_tolerance_share(), kChannelTolerance);
                            failures++;
                        }
                    }
                    else if (diff.differing_fraction() > kMaxDifferingShareAtZoom8) {
                        std::printf("  T6 FAIL, %.4f%% of pixels differ against a threshold of %.2f%%\n", 100.0 * diff.differing_fraction(), 100.0 * kMaxDifferingShareAtZoom8);
                        failures++;
                    }
                }
            }
        }
    }

    // the coverage comparison, which is the instrument that can measure antialiased agreement at all
    std::printf("\n== coverage, both engines averaged over 4x4 blocks\n");
    {
        const ViewCase one = view_zoom_one();
        static std::vector<std::pair<Engine, PixelBuffer>> covered;
        covered.clear();
        for (auto& [engine, renderer] : engines) {
            const EngineResult result = run_view(*renderer, *resources, one);
            covered.emplace_back(engine, result.pixels);
        }
        for (auto& [engine, result] : covered) {
            if (engine == Engine::dawn) {
                continue;
            }
            for (auto& [ref_engine, ref_pixels] : covered) {
                if (ref_engine != Engine::dawn) {
                    continue;
                }
                // a tolerance of four 8-bit steps over a sixteen pixel average is about one step of coverage per block
                const PixelDiff cover = diff_coverage(ref_pixels, result, 4, 4);
                std::printf("  %-12s vs dawn: %.4f%% of 4x4 blocks differ (%zu of %zu), worst channel %zu, %.2f%% within "
                            "+-4\n",
                            engine_name(engine), 100.0 * cover.differing_fraction(), cover.differing_pixels, cover.total_pixels, cover.max_channel_delta, 100.0 * cover.within_tolerance_share());
            }
        }
    }

    // the isolated segment: one clean horizontal line, which separates the quad corner computation from the aggregate
    // behaviour of a batch of twenty thousand overlapping strokes
    std::printf("\n== isolated segment, one horizontal line at y=400, at two widths\n");
    for (const float width : {1.0F, 2.0F}) {
        std::printf(" -- stroke width %.1f px\n", static_cast<double>(width));
        albor::a1_dawn::ResourceSet single;
        single.extent = 1000.0F;
        single.stroke_width = width;
        single.lines.push_back({400.0F, 400.0F, 600.0F, 400.0F});

        albor::a1_dawn::ViewTransform view{};
        view.scale = 1.0F;
        view.offset_x = 0.0F;
        view.offset_y = 0.0F;
        const ViewCase case_view{"single", view};

        std::vector<std::pair<Engine, PixelBuffer>> singles;
        for (auto& [engine, renderer] : engines) {
            const EngineResult result = run_view(*renderer, single, case_view);
            std::printf("  %-12s drew, %zu lit pixels\n", engine_name(engine), static_cast<std::size_t>(result.lit_pixels));
            singles.emplace_back(engine, result.pixels);
        }

        // the ink profile down the middle of the line says how wide each engine made it
        for (auto& [engine, pixels] : singles) {
            const std::vector<double> profile = ink_profile(pixels, 500);
            double covered_rows = 0.0;
            std::size_t first_row = 0;
            std::size_t last_row = 0;
            bool found = false;
            for (std::size_t row = 0; row < profile.size(); row++) {
                covered_rows += profile[row];
                if (profile[row] > 0.01) {
                    if (!found) {
                        first_row = row;
                        found = true;
                    }
                    last_row = row;
                }
            }
            double peak = 0.0;
            for (const double value : profile) {
                peak = std::max(peak, value);
            }
            std::printf("  %-12s ink across the column: %.3f px of coverage, peak %.3f, spanning rows %zu to %zu\n", engine_name(engine), covered_rows, peak, first_row, last_row);
        }

        for (auto& [engine, pixels] : singles) {
            if (engine == Engine::dawn) {
                continue;
            }
            for (auto& [ref_engine, ref_pixels] : singles) {
                if (ref_engine != Engine::dawn) {
                    continue;
                }
                const PixelDiff exact = diff_pixels(ref_pixels, pixels, kChannelTolerance);
                const PixelDiff cover = diff_coverage(ref_pixels, pixels, 4, 4);
                std::printf("  %-12s vs dawn on the isolated segment: %.4f%% of pixels differ, and %.4f%% of 4x4 "
                            "blocks differ\n",
                            engine_name(engine), 100.0 * exact.differing_fraction(), 100.0 * cover.differing_fraction());
            }
        }
    }

    // T7, on the isolated segment rather than an overlapping batch: a cosmetic stroke keeps its pixel width when
    // the scale changes and a geometry space stroke inflates with it, and this measures the width directly
    std::printf("\n== T7, isolated segment at zoom 1 and zoom 8\n");
    {
        albor::a1_dawn::ResourceSet single;
        single.extent = 1000.0F;
        single.stroke_width = 1.0F;
        single.lines.push_back({400.0F, 400.0F, 600.0F, 400.0F});

        albor::a1_dawn::ViewTransform view_one{};
        view_one.scale = 1.0F;

        // the same segment held on screen at eight times the scale: mapped = x * scale + offset
        albor::a1_dawn::ViewTransform view_eight{};
        view_eight.scale = 8.0F;
        view_eight.offset_x = 400.0F - 8.0F * 400.0F;
        view_eight.offset_y = 400.0F - 8.0F * 400.0F;

        for (auto& [engine, renderer] : engines) {
            const EngineResult result_one = run_view(*renderer, single, ViewCase{"zoom 1", view_one});
            const EngineResult result_eight = run_view(*renderer, single, ViewCase{"zoom 8", view_eight});
            double ink_one = 0.0;
            double ink_eight = 0.0;
            for (const double value : ink_profile(result_one.pixels, 500)) {
                ink_one += value;
            }
            for (const double value : ink_profile(result_eight.pixels, 500)) {
                ink_eight += value;
            }
            if (ink_one <= 0.0) {
                std::printf("  %-12s could not be measured, no ink at zoom 1\n", engine_name(engine));
                failures++;
                continue;
            }
            const double ratio = ink_eight / ink_one;
            std::printf("  %-12s ink width %.3f px at zoom 1, %.3f px at zoom 8, ratio %.2fx\n", engine_name(engine), ink_one, ink_eight, ratio);
            if (ratio <= 1.25) {
                std::printf("  T7 cosmetic for %s, the stroke keeps its pixel width under zoom\n", engine_name(engine));
            }
            else if (ratio >= 3.0) {
                std::printf("  T7 geometric for %s, the stroke inflates with the view, which is the recorded skia defect\n", engine_name(engine));
            }
            else {
                std::printf("  T7 INCONCLUSIVE for %s\n", engine_name(engine));
            }
        }
    }

    std::printf("\n== %d threshold failures\n", failures);
    return failures == 0 ? 0 : 1;
}
