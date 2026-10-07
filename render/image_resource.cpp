#include "render/image_resource.h"

#include <limits>

namespace albor
{
    ImageResource::ImageResource(std::uint64_t id, std::size_t width, std::size_t height, ImageFormat format, std::span<const std::uint8_t> pixels) :
        m_id(id), m_width(width), m_height(height), m_format(format), m_pixels(pixels.begin(), pixels.end()) {}

    std::uint64_t ImageResource::id() const {
        // the identifier is what the factory stamped at creation (§6.4, AD-06)
        return m_id;
    }

    std::size_t ImageResource::width() const {
        // the width is the extent the factory validated the bytes against (§6.4)
        return m_width;
    }

    std::size_t ImageResource::height() const {
        // the height is the extent the factory validated the bytes against (§6.4)
        return m_height;
    }

    ImageFormat ImageResource::format() const {
        // the format is what the byte count was checked against (§6.4)
        return m_format;
    }

    std::size_t ImageResource::row_bytes() const {
        // one row spans one pixel per column at the format's cost (§6.4)
        return m_width * bytes_per_pixel(m_format);
    }

    std::span<const std::uint8_t> ImageResource::pixels() const {
        // the bytes are handed back in row order, which is what an upload consumes (§6.4)
        return m_pixels;
    }

    std::size_t ImageResource::bytes_per_pixel(ImageFormat format) {
        // every format the framework knows today costs four bytes a pixel (§6.4)
        (void)format;
        return 4;
    }

    std::size_t ImageResource::byte_count(std::size_t width, std::size_t height, ImageFormat format) {
        // an empty extent needs no bytes, which the largest size_t marks as a refusal (§6.4)
        if (width == 0 || height == 0)
            return std::numeric_limits<std::size_t>::max();
        // the whole image is one pixel per cell, which the product counts (§6.4)
        return width * height * bytes_per_pixel(format);
    }
} // namespace albor
