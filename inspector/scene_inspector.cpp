#include "inspector/scene_inspector.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace albor::inspector
{
    namespace
    {
        // how many kinds a report counts, which is the enumeration's size (§6.2)
        constexpr std::size_t KIND_COUNT = 9;

        // report whether a bound carries a number that is not finite, which no rasterizer can honour (§6.2)
        bool not_finite(const Rect& rect) {
            // every edge is checked, because one non finite value poisons the whole rectangle (§6.2)
            return !std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height);
        }

        // the surface a frame presents at, which is what an off screen command is measured against (§6.2)
        Rect surface_rect(const FrameScene& scene) {
            // the metrics carry the size, which is the whole of the surface (§6.1)
            return Rect{.x = 0.0f, .y = 0.0f, .width = scene.metrics().logical_size.width, .height = scene.metrics().logical_size.height};
        }

        // report whether a bound lies wholly outside the surface, which is how an off screen command is found (§6.2)
        bool outside(const Rect& rect, const Rect& surface) {
            // the right edge is compared, because a command wholly left of the surface never lands (§6.2)
            const float right = rect.x + rect.width;
            // the bottom edge is compared, because a command wholly above never lands (§6.2)
            const float bottom = rect.y + rect.height;
            // the command lands only if it overlaps on at least one axis, so both must miss (§6.2)
            return right <= surface.x || rect.x >= surface.x + surface.width || bottom <= surface.y || rect.y >= surface.y + surface.height;
        }
    } // namespace

    SceneReport inspect(const FrameScene& scene) {
        // the report starts empty, because a frame is described rather than judged (§12.2)
        SceneReport report;
        // the per kind counts start at zero, which is what an empty scene reports (§6.2)
        report.per_kind.assign(KIND_COUNT, 0U);
        // the covered area starts empty, because no command has been seen (§6.2)
        report.covered = Rect{};
        // the commands are walked in order, because a debugger reports the order it recorded (§6.1)
        for (const DrawCommand& command : scene.commands()) {
            // the total follows the frame (§6.1)
            report.commands++;
            // the kind is counted, which is what shows at a glance (§12.2)
            const std::size_t kind = static_cast<std::size_t>(command.kind());
            // a kind outside the count would overrun, so it is skipped rather than written (§12.2)
            if (kind < report.per_kind.size())
                report.per_kind[kind]++;
            // the bound is what the command could touch, which is what the overlay draws (§6.2)
            const Rect bound = command.bounds();
            // a command with no area contributes nothing to the covered extent (§6.2)
            if (bound.width <= 0.0f && bound.height <= 0.0f)
                continue;
            // the first real bound becomes the extent, because an empty one would swallow it (§6.2)
            if (report.covered.width == 0.0f && report.covered.height == 0.0f) {
                // the extent takes the bound, which is what the frame covers (§6.2)
                report.covered = bound;
                // the command is consumed (§12.2)
                continue;
            }
            // the extent grows to cover this bound, which is what makes it the whole frame (§6.2)
            const float left = std::min(report.covered.x, bound.x);
            // the top edge follows the higher of the two (§6.2)
            const float top = std::min(report.covered.y, bound.y);
            // the right edge follows the wider of the two (§6.2)
            const float right = std::max(report.covered.x + report.covered.width, bound.x + bound.width);
            // the bottom edge follows the taller of the two (§6.2)
            const float bottom = std::max(report.covered.y + report.covered.height, bound.y + bound.height);
            // the extent is rewritten whole, because rect has no grow operation (§6.2)
            report.covered = Rect{.x = left, .y = top, .width = right - left, .height = bottom - top};
        }
        // a frame that draws only its background is legal but usually a mistake, so it is reported (§6.1)
        if (report.commands == 0) {
            // the finding names what was seen, which is what a developer needs (§12.2)
            report.findings.push_back(Finding{.code = "I0001", .message = "the frame records no command, so it draws only its background"});
        }
        // every command is checked, because the rules are about individual commands (§6.2)
        for (const DrawCommand& command : scene.commands()) {
            // the bound is read once, because both rules are about it (§6.2)
            const Rect bound = command.bounds();
            // a bound that cannot be honoured is reported, because a rasterizer would draw something odd (§6.2)
            if (not_finite(bound)) {
                // the finding names the kind, so the developer can find the command (§12.2)
                report.findings.push_back(Finding{.code = "I0002", .message = "a command carries a bound that is not finite"});
                // one finding per command is enough to act on (§12.2)
                continue;
            }
            // a command wholly outside the surface is reported, because it can never land (§6.2)
            if (outside(bound, surface_rect(scene)))
                // the finding names the kind, so the developer can find the command (§12.2)
                report.findings.push_back(Finding{.code = "I0003", .message = "a command lies wholly outside the surface"});
        }
        // the report is returned whole (§12.2)
        return report;
    }

    std::shared_ptr<const FrameScene> bounds_overlay(const FrameScene& scene, Color tint) {
        // the edges accumulate as plain rectangles, which the primitive factory already builds (§6.1)
        std::vector<Rect> edges;
        // every command contributes its bound, because the point is to see where each landed (§12.2)
        for (const DrawCommand& command : scene.commands()) {
            // the bound is read, which is what the overlay outlines (§6.2)
            const Rect bound = command.bounds();
            // a command with no area has nothing to outline (§6.2)
            if (bound.width <= 0.0f || bound.height <= 0.0f)
                continue;
            // the leading vertical edge, which is one logical pixel wide so it survives any scale (§12.2)
            edges.push_back(Rect{.x = bound.x, .y = bound.y, .width = 1.0f, .height = bound.height});
            // the trailing vertical edge, which completes the left and right of the outline (§12.2)
            edges.push_back(Rect{.x = bound.x + bound.width - 1.0f, .y = bound.y, .width = 1.0f, .height = bound.height});
            // the leading horizontal edge, which completes the top (§12.2)
            edges.push_back(Rect{.x = bound.x, .y = bound.y, .width = bound.width, .height = 1.0f});
            // the trailing horizontal edge completes the bottom (§12.2)
            edges.push_back(Rect{.x = bound.x, .y = bound.y + bound.height - 1.0f, .width = bound.width, .height = 1.0f});
        }
        // the overlay is built through the primitive factory, so it renders by the ordinary path (§6.1)
        FrameScene overlay = FrameScene::from_primitives(scene.metrics(), tint, std::move(edges));
        // the overlay is returned shared, which is what a render call takes (§6.1)
        return std::make_shared<const FrameScene>(std::move(overlay));
    }
} // namespace albor::inspector
