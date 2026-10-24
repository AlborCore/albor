#include "resources/png_decoder.h"

#include <memory>
#include <utility>

#include <png.h>

namespace albor::resources
{
    namespace
    {
        // cap the decoded extent at 64 MiB, so a crafted header cannot force an unbounded allocation (§6.4)
        constexpr std::size_t MAX_DECODED_BYTES = 64 * 1024 * 1024;

        // release the reader's header state on every exit, including an allocation failure while decoding (§6.4)
        struct PngImageFree
        {
            void operator()(png_image* image) const noexcept { png_image_free(image); }
        };
    } // namespace

    std::optional<DecodedPixels> decode_png(std::span<const std::uint8_t> encoded) {
        // an empty buffer has no signature to read, so it is refused before libpng touches it (§6.4)
        if (encoded.empty())
            return std::nullopt;
        // the simplified reader keeps the chunk walk and the inflate inside libpng, which is the format owner (§6.4)
        png_image image{};
        // the version tells libpng which struct layout this build compiled against (§6.4)
        image.version = PNG_IMAGE_VERSION;
        // a signature or header libpng rejects has no pixels to hand back (§6.4)
        if (!png_image_begin_read_from_memory(&image, encoded.data(), encoded.size())) {
            // a failed begin may still have allocated state, which the release must return (§6.4)
            png_image_free(&image);
            // the refused header has no pixels to hand back (§6.4)
            return std::nullopt;
        }
        // the guard owns the reader as soon as the header exists, so every later exit frees it (§6.4)
        const std::unique_ptr<png_image, PngImageFree> reader(&image);
        // the output is eight bit rgba, so every palette, gray and sixteen bit source reaches the resource contract (§6.4)
        image.format = PNG_FORMAT_RGBA;
        // the extent is copied before the reader is freed, because the free releases its header state (§6.4)
        const std::size_t width = static_cast<std::size_t>(image.width);
        const std::size_t height = static_cast<std::size_t>(image.height);
        // an extent beyond the cap is refused before its byte count can overflow or allocate (§6.4)
        if (height == 0 || width > MAX_DECODED_BYTES / 4 / height)
            return std::nullopt;
        // the destination spans the whole extent, which the simplified reader fills row by row (§6.4)
        std::vector<std::uint8_t> rgba(PNG_IMAGE_SIZE(image));
        // a truncated or corrupt image stream fails here, after the header was accepted (§6.4)
        if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr))
            return std::nullopt;
        // the extent and pixels are handed back in straight alpha, which the registry premultiplies (§6.4)
        return DecodedPixels{.width = width, .height = height, .rgba = std::move(rgba)};
    }
} // namespace albor::resources
