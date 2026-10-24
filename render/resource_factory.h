#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "core/geometry.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"

namespace albor
{
    // the id source and validator for every retained resource, which never repeats an identifier across threads (§6.4, AD-06)
    class ResourceFactory
    {
    public:
        // build a factory that stamps resources from the process wide source (§6.4, AD-06)
        ResourceFactory();

        ResourceFactory(const ResourceFactory&) = delete;
        ResourceFactory& operator=(const ResourceFactory&) = delete;
        ResourceFactory(ResourceFactory&&) = delete;
        ResourceFactory& operator=(ResourceFactory&&) = delete;
        ~ResourceFactory() = default;

        // build a validated immutable line batch, or nothing for an empty or dangling segment list, which a worker thread calls (§6.4, AD-06)
        [[nodiscard]] std::shared_ptr<const LineBatch> create_line_batch(std::span<const Point> points) const;

        // build a validated immutable path, or nothing for an empty or point-mismatched command stream (§6.4, AD-06)
        [[nodiscard]] std::shared_ptr<const PathResource> create_path(std::span<const PathVerb> verbs, std::span<const Point> points) const;

        // build a validated immutable image, or nothing for a bad extent or pixel buffer (§6.4, AD-06)
        [[nodiscard]] std::shared_ptr<const ImageResource> create_image(std::size_t width, std::size_t height, ImageFormat format, std::span<const std::uint8_t> pixels) const;

        // freeze a recorder's commands into an immutable picture, or nothing for an empty recording (§6.4, AD-06)
        [[nodiscard]] std::shared_ptr<const PictureResource> create_picture(const PictureRecorder& recorder) const;

    private:
        // hand out the next process wide identifier, which every factory and the registry share (§6.4, AD-06)
        [[nodiscard]] static std::uint64_t next_id();
    };
} // namespace albor
