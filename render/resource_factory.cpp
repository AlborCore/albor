#include "render/resource_factory.h"

namespace albor
{
    ResourceFactory::ResourceFactory() = default;

    std::shared_ptr<const LineBatch> ResourceFactory::create_line_batch(std::span<const Point> points) const {
        // a batch with no point cannot be drawn, and a dangling point is not a segment, so both are refused (§6.4, AD-06)
        if (points.empty() || points.size() % 2 != 0)
            return nullptr;
        // the identifier advances atomically, so concurrent workers never collide (§6.4, AD-06)
        return std::make_shared<const LineBatch>(m_next_id.fetch_add(1), points);
    }

    std::shared_ptr<const PathResource> ResourceFactory::create_path(std::span<const PathVerb> verbs, std::span<const Point> points) const {
        // a path with no command cannot be drawn, so it is refused (§6.4, AD-06)
        if (verbs.empty())
            return nullptr;
        // a command stream whose point demand misses the point list is malformed, so it is refused (§6.4, AD-06)
        if (PathResource::point_demand(verbs) != points.size())
            return nullptr;
        // the identifier advances atomically, so concurrent workers never collide (§6.4, AD-06)
        return std::make_shared<const PathResource>(m_next_id.fetch_add(1), verbs, points);
    }

    std::shared_ptr<const ImageResource> ResourceFactory::create_image(std::size_t width, std::size_t height, ImageFormat format, std::span<const std::uint8_t> pixels) const {
        // an empty extent cannot be drawn, so it is refused (§6.4, AD-06)
        if (width == 0 || height == 0)
            return nullptr;
        // a pixel buffer that misses the extent's byte count is malformed, so it is refused (§6.4, AD-06)
        if (pixels.size() != ImageResource::byte_count(width, height, format))
            return nullptr;
        // the identifier advances atomically, so concurrent workers never collide (§6.4, AD-06)
        return std::make_shared<const ImageResource>(m_next_id.fetch_add(1), width, height, format, pixels);
    }

    std::shared_ptr<const PictureResource> ResourceFactory::create_picture(const PictureRecorder& recorder) const {
        // a picture with no command draws nothing, so it is refused (§6.4, AD-06)
        if (recorder.empty())
            return nullptr;
        // the commands move in one piece, because the picture replays them in order (§6.4, AD-06)
        return std::make_shared<const PictureResource>(m_next_id.fetch_add(1), recorder.commands());
    }
} // namespace albor
