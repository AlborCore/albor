#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include "automation/png_writer.h"

using namespace albor::automation;

TEST(PngWriter, AFrameRoundTripsThroughItsChunks) {
    // arrange a two by two opaque frame whose four pixels differ, which is what a reader compares (§6.3)
    const std::array<std::uint8_t, 16> pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    // act
    const std::string png = encode_png(2, 2, pixels);
    // assert the signature opens the file, which every decoder sniffs before a chunk (§6.3)
    ASSERT_GT(png.size(), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[0]), 0x89U);
    EXPECT_EQ(png.substr(1, 3), "PNG");
    // assert the header names the extent, the eight bit depth and the rgba colour type (§6.3)
    EXPECT_EQ(png.substr(12, 4), "IHDR");
    EXPECT_EQ(static_cast<std::uint8_t>(png[19]), 2U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[23]), 2U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[24]), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[25]), 6U);
    // act the chunks are walked, which is what a decoder does before it inflates (§6.3)
    std::string compressed;
    std::size_t at = 8;
    while (at + 12 <= png.size()) {
        // the length is a big endian word, which the chunk header spells (§6.3)
        const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
        const std::string type = png.substr(at + 4, 4);
        if (type == "IDAT")
            compressed.append(png, at + 8, length);
        at += 12 + length;
        if (type == "IEND")
            break;
    }
    ASSERT_FALSE(compressed.empty());
    // act the stream is inflated, which is the inverse of what the encoder wrote (§6.3)
    std::vector<std::uint8_t> raw((2U * 4U + 1U) * 2U);
    uLongf raw_size = raw.size();
    ASSERT_EQ(uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size())), Z_OK);
    // assert the first row carries none and the second carries up, which is what the encoder writes (ISO 15948)
    ASSERT_EQ(raw_size, raw.size());
    EXPECT_EQ(raw[0], 0U);
    EXPECT_EQ(raw[9], 2U);
    // assert the first row is the frame's own, because an opaque pixel needs no conversion (§6.3)
    for (std::size_t index = 0; index < 8; index++)
        EXPECT_EQ(raw[1U + index], pixels[index]);
    // assert the second row adds the row above back, which is what the up filter subtracts (ISO 15948)
    for (std::size_t index = 0; index < 8; index++)
        EXPECT_EQ(static_cast<std::uint8_t>(raw[10U + index] + raw[1U + index]), pixels[8U + index]);
}

TEST(PngWriter, ATranslucentPixelIsUnpremultiplied) {
    // arrange a pixel whose colour is premultiplied by half its alpha, which png cannot carry as it stands (§6.3)
    const std::array<std::uint8_t, 4> pixels = {50, 25, 0, 128};
    // act
    const std::string png = encode_png(1, 1, pixels);
    ASSERT_FALSE(png.empty());
    std::string compressed;
    std::size_t at = 8;
    while (at + 12 <= png.size()) {
        // the length is a big endian word, which the chunk header spells (§6.3)
        const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
        if (png.substr(at + 4, 4) == "IDAT")
            compressed.append(png, at + 8, length);
        at += 12 + length;
    }
    std::vector<std::uint8_t> raw(5);
    uLongf raw_size = raw.size();
    ASSERT_EQ(uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size())), Z_OK);
    // assert each channel is divided back by the alpha, which is the lossy step raw exists for (§6.3)
    EXPECT_EQ(raw[0], 0U);
    EXPECT_EQ(raw[1], 100U);
    EXPECT_EQ(raw[2], 50U);
    EXPECT_EQ(raw[3], 0U);
    EXPECT_EQ(raw[4], 128U);
}

TEST(PngWriter, ATransparentPixelCarriesNoColour) {
    // arrange a fully transparent pixel, whose premultiplied colour is zero (§6.3)
    const std::array<std::uint8_t, 4> pixels = {0, 0, 0, 0};
    // act
    const std::string png = encode_png(1, 1, pixels);
    ASSERT_FALSE(png.empty());
    std::string compressed;
    std::size_t at = 8;
    while (at + 12 <= png.size()) {
        // the length is a big endian word, which the chunk header spells (§6.3)
        const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
        if (png.substr(at + 4, 4) == "IDAT")
            compressed.append(png, at + 8, length);
        at += 12 + length;
    }
    std::vector<std::uint8_t> raw(5);
    uLongf raw_size = raw.size();
    ASSERT_EQ(uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size())), Z_OK);
    // assert the zero alpha is not divided by, which would leave an undefined colour (§6.3)
    EXPECT_EQ(raw[1], 0U);
    EXPECT_EQ(raw[2], 0U);
    EXPECT_EQ(raw[3], 0U);
    EXPECT_EQ(raw[4], 0U);
}

TEST(PngWriter, AFrameThatDoesNotFillItsExtentIsRefused) {
    // arrange the three shapes a broken frame can take, which no png can describe (§6.3)
    const std::array<std::uint8_t, 4> one_pixel = {1, 2, 3, 4};
    // act and assert each is refused, because the header would describe another picture (§6.3)
    EXPECT_TRUE(encode_png(0, 1, one_pixel).empty());
    EXPECT_TRUE(encode_png(1, 0, one_pixel).empty());
    EXPECT_TRUE(encode_png(2, 2, one_pixel).empty());
}

TEST(PngWriter, EveryChunkCarriesItsOwnCrc) {
    // arrange a two by two frame, which produces the three chunks every png carries (ISO 15948)
    const std::array<std::uint8_t, 16> pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    // act
    const std::string png = encode_png(2, 2, pixels);
    ASSERT_FALSE(png.empty());
    // assert every chunk's crc is the one its type and data produce, which every decoder verifies (ISO 15948)
    std::size_t at = 8;
    std::size_t chunks = 0;
    while (at + 12 <= png.size()) {
        // the length is a big endian word, which the chunk header spells (ISO 15948)
        const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
        const std::string type = png.substr(at + 4, 4);
        const std::string data = png.substr(at + 8, length);
        const std::uint32_t stored = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 8 + length])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 9 + length])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 10 + length])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 11 + length]));
        uLong crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(type.data()), static_cast<uInt>(type.size()));
        crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
        EXPECT_EQ(stored, static_cast<std::uint32_t>(crc));
        chunks++;
        at += 12 + length;
        if (type == "IEND")
            break;
    }
    // assert the walk ended at the end chunk and consumed the file, which a trailing chunk would break (ISO 15948)
    EXPECT_EQ(chunks, 3U);
    EXPECT_EQ(png.substr(at - 8, 4), "IEND");
    EXPECT_EQ(at, png.size());
}

TEST(PngWriter, TheHeaderNamesItsOwnExtent) {
    // arrange a three by two frame, whose width and height differ (§6.3)
    const std::vector<std::uint8_t> pixels(3U * 2U * 4U, 128U);
    // act
    const std::string png = encode_png(3, 2, pixels);
    ASSERT_GT(png.size(), 29U);
    // assert the header carries the width then the height as big endian words (ISO 15948)
    EXPECT_EQ(png.substr(16, 4), std::string("\0\0\0\x03", 4));
    EXPECT_EQ(png.substr(20, 4), std::string("\0\0\0\x02", 4));
    // assert the depth, the rgba colour type and the three methods, which a decoder reads before the pixels (ISO 15948)
    EXPECT_EQ(static_cast<std::uint8_t>(png[24]), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[25]), 6U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[26]), 0U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[27]), 0U);
    EXPECT_EQ(static_cast<std::uint8_t>(png[28]), 0U);
    // assert the signature carries its carriage return, line feed and end of file bytes, which a decoder sniffs (ISO 15948)
    EXPECT_EQ(static_cast<std::uint8_t>(png[4]), '\r');
    EXPECT_EQ(static_cast<std::uint8_t>(png[5]), '\n');
    EXPECT_EQ(static_cast<std::uint8_t>(png[6]), 0x1AU);
    EXPECT_EQ(static_cast<std::uint8_t>(png[7]), '\n');
}

TEST(PngWriter, ALargeFrameSplitsItsStreamWithoutDroppingTheTail) {
    // arrange a noisy frame whose deflate stream exceeds one chunk, which a flat frame would never reach (ISO 15948)
    const int width = 600;
    const int height = 600;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    std::uint32_t state = 0x12345678U;
    for (std::uint8_t& byte : pixels) {
        // a linear congruential step gives bytes no deflate pass flattens, so the stream passes one chunk (§6.3)
        state = state * 1664525U + 1013904223U;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    // act
    const std::string png = encode_png(width, height, pixels);
    ASSERT_FALSE(png.empty());
    // act the chunks are walked, which is how a decoder collects the stream (ISO 15948)
    std::string compressed;
    std::size_t at = 8;
    std::size_t idats = 0;
    while (at + 12 <= png.size()) {
        // the length is a big endian word, which the chunk header spells (ISO 15948)
        const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
        const std::string type = png.substr(at + 4, 4);
        if (type == "IDAT") {
            compressed.append(png, at + 8, length);
            idats++;
        }
        at += 12 + length;
        if (type == "IEND")
            break;
    }
    // assert the stream arrived in more than one chunk, because the split is what a large frame exercises (ISO 15948)
    EXPECT_GT(idats, 1U);
    // assert the chunks form one zlib stream that inflates to the whole frame, which a lost tail would fail (ISO 15948)
    std::vector<std::uint8_t> raw((static_cast<std::size_t>(width) * 4U + 1U) * static_cast<std::size_t>(height));
    uLongf raw_size = raw.size();
    ASSERT_EQ(uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size())), Z_OK);
    EXPECT_EQ(raw_size, raw.size());
}

TEST(PngWriter, TheAlphaConversionsRoundAndClamp) {
    // arrange three pixels at the edges of the conversion, which a rounding change would break (§6.3)
    const std::array<std::uint8_t, 12> pixels = {200, 0, 0, 100, 1, 0, 0, 1, 253, 0, 0, 254};
    // act
    const std::string png = encode_png(3, 1, pixels);
    ASSERT_FALSE(png.empty());
    std::string compressed;
    std::size_t at = 8;
    while (at + 12 <= png.size()) {
        // the length is a big endian word, which the chunk header spells (ISO 15948)
        const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at])) << 24) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 1])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(png[at + 3]));
        if (png.substr(at + 4, 4) == "IDAT")
            compressed.append(png, at + 8, length);
        at += 12 + length;
    }
    std::vector<std::uint8_t> raw(13);
    uLongf raw_size = raw.size();
    ASSERT_EQ(uncompress(raw.data(), &raw_size, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressed.size())), Z_OK);
    // assert a channel above its alpha clamps, and the two rounding edges land on 255 and 254 (§6.3)
    EXPECT_EQ(raw[1], 255U);
    EXPECT_EQ(raw[5], 255U);
    EXPECT_EQ(raw[9], 254U);
}
