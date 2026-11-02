#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace albor
{
    // how the bytes of an image resource are laid out, which the backend translates into a texture (§6.4, AD-26)
    enum class ImageFormat
    {
        // four bytes per pixel in premultiplied rgb(a), matching the skia reference (§6.4, AD-26)
        RGBA8,
    };

    // the immutable pixel data a domain view retains off the ui thread, which the backend uploads once (§6.4, AD-26)
    class ImageResource
    {
    public:
        // build an image of the given extent and pixels under a factory identifier (§6.4)
        ImageResource(std::uint64_t id, std::size_t width, std::size_t height, ImageFormat format, std::span<const std::uint8_t> pixels);

        ImageResource(const ImageResource&) = delete;
        ImageResource& operator=(const ImageResource&) = delete;
        ImageResource(ImageResource&&) = delete;
        ImageResource& operator=(ImageResource&&) = delete;
        ~ImageResource() = default;

        // hand out the identifier the factory stamped, which the backend keys its gpu cache on (§6.4, AD-26)
        [[nodiscard]] std::uint64_t id() const;

        // hand out the width in pixels (§6.4)
        [[nodiscard]] std::size_t width() const;

        // hand out the height in pixels (§6.4)
        [[nodiscard]] std::size_t height() const;

        // hand out the pixel layout the bytes follow (§6.4)
        [[nodiscard]] ImageFormat format() const;

        // hand out the byte count of one pixel row (§6.4)
        [[nodiscard]] std::size_t row_bytes() const;

        // hand out the premultiplied raw pixel bytes the backend uploads (§6.4, AD-26)
        [[nodiscard]] std::span<const std::uint8_t> pixels() const;

        // report how many bytes a format consumes per pixel (§6.4)
        [[nodiscard]] static std::size_t bytes_per_pixel(ImageFormat format);

        // report how many bytes an extent needs for a format, or the largest size_t when the extent is empty (§6.4)
        [[nodiscard]] static std::size_t byte_count(std::size_t width, std::size_t height, ImageFormat format);

    private:
        // the identifier the factory assigned, which never repeats (§6.4, AD-06)
        std::uint64_t m_id;

        // the width in pixels (§6.4)
        std::size_t m_width;

        // the height in pixels (§6.4)
        std::size_t m_height;

        // the pixel layout the bytes follow (§6.4)
        ImageFormat m_format;

        // the raw pixel bytes in row order (§6.4)
        std::vector<std::uint8_t> m_pixels;
    };
} // namespace albor
