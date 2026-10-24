#include "render/resource_factory.h"

#include <atomic>

namespace albor
{
    ResourceFactory::ResourceFactory() = default;

    std::shared_ptr<const LineBatch> ResourceFactory::create_line_batch(std::span<const Point> points) const {
        // a batch with no point cannot be drawn, and a dangling point is not a segment, so both are refused (§6.4, AD-06)
        if (points.empty() || points.size() % 2 != 0)
            return nullptr;
        // the identifier advances atomically, so concurrent workers never collide (§6.4, AD-06)
        return std::make_shared<const LineBatch>(next_id(), points);
    }

    std::shared_ptr<const PathResource> ResourceFactory::create_path(std::span<const PathVerb> verbs, std::span<const Point> points) const {
        // a path with no command cannot be drawn, so it is refused (§6.4, AD-06)
        if (verbs.empty())
            return nullptr;
        // a command stream whose point demand misses the point list is malformed, so it is refused (§6.4, AD-06)
        if (PathResource::point_demand(verbs) != points.size())
            return nullptr;
        // the identifier advances atomically, so concurrent workers never collide (§6.4, AD-06)
        return std::make_shared<const PathResource>(next_id(), verbs, points);
    }

    std::shared_ptr<const ImageResource> ResourceFactory::create_image(std::size_t width, std::size_t height, ImageFormat format, std::span<const std::uint8_t> pixels) const {
        // an empty extent cannot be drawn, so it is refused (§6.4, AD-06)
        if (width == 0 || height == 0)
            return nullptr;
        // a pixel buffer that misses the extent's byte count is malformed, so it is refused (§6.4, AD-06)
        if (pixels.size() != ImageResource::byte_count(width, height, format))
            return nullptr;
        // the identifier is process wide, so a registry image and a factory image share one backend cache (§6.4, AD-06)
        return std::make_shared<const ImageResource>(next_id(), width, height, format, pixels);
    }

    std::shared_ptr<const PictureResource> ResourceFactory::create_picture(const PictureRecorder& recorder) const {
        // a picture with no command draws nothing, so it is refused (§6.4, AD-06)
        if (recorder.empty())
            return nullptr;
        // the commands move in one piece, because the picture replays them in order (§6.4, AD-06)
        return std::make_shared<const PictureResource>(next_id(), recorder.commands());
    }

    std::uint64_t ResourceFactory::next_id() {
        // the source is one per process, because factories and the registry never repeat an identifier (§6.4, AD-06)
        static std::atomic<std::uint64_t> next{1};
        // the identifier advances atomically, so concurrent workers never collide (§6.4, AD-06)
        return next.fetch_add(1);
    }
} // namespace albor
