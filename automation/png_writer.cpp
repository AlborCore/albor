#include "automation/png_writer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace albor::automation
{
    namespace
    {
        // the eight bytes every png opens with, which a decoder sniffs before it reads a chunk (ISO 15948)
        constexpr std::array<std::uint8_t, 8> PNG_SIGNATURE{0x89u, 'P', 'N', 'G', '\r', '\n', 0x1Au, '\n'};

        // how many compressed bytes one idat chunk carries, which keeps every chunk far inside the limit (ISO 15948)
        constexpr std::size_t IDAT_CHUNK = 1024u * 1024u;

        // append one big endian word, which every png field spells that way (ISO 15948)
        void append_word(std::string& out, std::uint32_t word) {
            // the four bytes go most significant first, which is the order every png field documents (ISO 15948)
            for (int shift = 24; shift >= 0; shift -= 8)
                out.push_back(static_cast<char>((word >> shift) & 0xFFu));
        }

        // append one chunk with its crc, which a decoder verifies before it reads the data (ISO 15948)
        void append_chunk(std::string& out, std::string_view type, std::string_view data) {
            // the length describes the data alone, which is what the chunk header promises (ISO 15948)
            append_word(out, static_cast<std::uint32_t>(data.size()));
            // the type names the chunk, which is what a decoder switches on (ISO 15948)
            out.append(type);
            // the data follows the type, which is the payload the chunk carries (ISO 15948)
            out.append(data);
            // the crc covers the type and the data, which is what every png reader verifies (ISO 15948)
            uLong crc = crc32(0L, Z_NULL, 0);
            crc = crc32(crc, reinterpret_cast<const Bytef*>(type.data()), static_cast<uInt>(type.size()));
            // an empty chunk still folds in its type, which is why data joins the crc only when present (ISO 15948)
            if (!data.empty())
                crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
            // the crc is a big endian word like every other field (ISO 15948)
            append_word(out, static_cast<std::uint32_t>(crc));
        }
    } // namespace

    std::string encode_png(int width, int height, std::span<const std::uint8_t> pixels) {
        // a frame without an extent or without a whole row of bytes cannot be a picture (ISO 15948)
        if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
            return {};
        // every row is filtered against the row above, which turns a flat interface into runs of zero bytes (ISO 15948)
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
        const std::size_t stride = row_bytes + 1u;
        std::vector<std::uint8_t> raw(stride * static_cast<std::size_t>(height));
        // the row above is kept unfiltered, because the up filter subtracts reconstructed bytes (ISO 15948)
        std::vector<std::uint8_t> above(row_bytes, 0);
        std::vector<std::uint8_t> line(row_bytes);
        for (int row = 0; row < height; row++) {
            // the first row has nothing above it, which the none filter states (ISO 15948)
            const std::size_t at = static_cast<std::size_t>(row) * stride;
            raw[at] = row == 0 ? 0 : 2;
            // the four channels are written straight, because png names the alpha rather than folding it (ISO 15948)
            for (std::size_t column = 0; column < row_bytes; column += 4u) {
                const std::size_t source = static_cast<std::size_t>(row) * row_bytes + column;
                // the alpha is what the colour was scaled by, which the file divides back out (ISO 15948)
                const std::uint8_t alpha = pixels[source + 3u];
                for (std::size_t channel = 0; channel < 3u; channel++) {
                    // a transparent pixel carries no colour, which is the only defined value at zero alpha (ISO 15948)
                    const std::uint32_t value = alpha == 0 ? 0u : (static_cast<std::uint32_t>(pixels[source + channel]) * 255u + alpha / 2u) / alpha;
                    line[column + channel] = static_cast<std::uint8_t>(std::min<std::uint32_t>(value, 255u));
                }
                // the alpha itself travels unchanged, because the file states it (ISO 15948)
                line[column + 3u] = alpha;
            }
            // the up filter subtracts the byte above, which a decoder adds back in the same order (ISO 15948)
            for (std::size_t column = 0; column < row_bytes; column++)
                raw[at + 1u + column] = static_cast<std::uint8_t>(line[column] - above[column]);
            // the row just written is what the next one subtracts (ISO 15948)
            above = line;
        }
        // a frame past half the deflate api's range is refused, which leaves the bound its own room (ISO 15948)
        if (raw.size() > std::numeric_limits<uLong>::max() / 2u)
            return {};
        // the deflate stream is the one png fixes, which zlib produces at the speed a capture can pay (ISO 15948)
        uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
        std::vector<std::uint8_t> compressed(compressed_size);
        if (compress2(compressed.data(), &compressed_size, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK)
            return {};
        compressed.resize(compressed_size);
        // the file is assembled from its signature through its last chunk, which is what a decoder walks (ISO 15948)
        std::string out;
        out.reserve(PNG_SIGNATURE.size() + raw.size() / 2u + 128u);
        out.append(reinterpret_cast<const char*>(PNG_SIGNATURE.data()), PNG_SIGNATURE.size());
        // the header names the extent, the eight bit depth and the rgba colour type (ISO 15948)
        std::string header;
        append_word(header, static_cast<std::uint32_t>(width));
        append_word(header, static_cast<std::uint32_t>(height));
        // the depth is one byte per channel, which is what every backend reads (ISO 15948)
        header.push_back(8);
        // the colour type is truecolour with alpha, which is the only layout a capture carries (ISO 15948)
        header.push_back(6);
        // the compression, filter and interlace methods are the only ones the format defines at this depth (ISO 15948)
        header.push_back(0);
        header.push_back(0);
        header.push_back(0);
        append_chunk(out, "IHDR", header);
        // the compressed stream is split into chunks, because one chunk cannot exceed the format's length word (ISO 15948)
        for (std::size_t at = 0; at < compressed.size(); at += IDAT_CHUNK) {
            const std::size_t length = std::min(IDAT_CHUNK, compressed.size() - at);
            append_chunk(out, "IDAT", std::string_view(reinterpret_cast<const char*>(compressed.data() + at), length));
        }
        // the end chunk closes the file, which is the last thing a decoder expects (ISO 15948)
        append_chunk(out, "IEND", {});
        return out;
    }
} // namespace albor::automation
