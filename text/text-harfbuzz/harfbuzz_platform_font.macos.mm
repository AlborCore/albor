#include "text/text-harfbuzz/harfbuzz_platform_font.h"

#include <CoreText/CoreText.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <skia/core/SkTypeface.h>
#include <skia/ports/SkTypeface_mac.h>

namespace albor::harfbuzz
{
    std::vector<std::uint8_t> platform_font_bytes(const void* typeface_object) {
        // the object is the skia typeface the shaping issued, which only this half may name (§3.5, AD-19)
        const SkTypeface* typeface = static_cast<const SkTypeface*>(typeface_object);
        // the core text face is what the platform backs the typeface with, and a stream backed one carries none (§9.1)
        CTFontRef ct_font = SkTypeface_GetCTFontRef(typeface);
        // the graphics font is the sfnt the core text face fronts, which a face with no backing does not carry (§15)
        CGFontRef cg_font = ct_font != nullptr ? CTFontCopyGraphicsFont(ct_font, nullptr) : nullptr;
        // the tags name the tables the face carries, which rebuild into one face file (§15, AD-34)
        CFArrayRef tags = cg_font != nullptr ? CGFontCopyTableTags(cg_font) : nullptr;
        // the reported count bounds the walk, and a face with an absurd or empty tag list carries none (§15)
        const CFIndex reported = tags != nullptr ? CFArrayGetCount(tags) : 0;
        const CFIndex tag_count = reported > 0 && reported <= 512 ? reported : 0;
        // one table entry carries its tag and its bytes, which the directory orders by tag (§15)
        struct Table
        {
            // the four character identifier the directory sorts on (§15)
            std::uint32_t tag = 0;

            // the table bytes themselves (§15)
            std::vector<std::uint8_t> bytes;
        };
        std::vector<Table> tables;
        for (CFIndex index = 0; index < tag_count; index++) {
            // the array carries the raw tag values, which a cast hands over directly (§15)
            const std::uint32_t raw_tag = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(CFArrayGetValueAtIndex(tags, index)));
            // the table for that tag is what the face carries, and a tag with no data is skipped (§15)
            CFDataRef table = CGFontCopyTableForTag(cg_font, raw_tag);
            // the bytes are copied out, because the data goes away with its release (§15)
            if (table != nullptr) {
                const std::uint8_t* start = reinterpret_cast<const std::uint8_t*>(CFDataGetBytePtr(table));
                tables.push_back(Table{.tag = raw_tag, .bytes = std::vector<std::uint8_t>(start, start + CFDataGetLength(table))});
                CFRelease(table);
            }
        }
        // the platform objects go back, because the tables were copied out above (§15)
        if (tags != nullptr)
            CFRelease(tags);
        if (cg_font != nullptr)
            CFRelease(cg_font);
        // a face the platform handed no table for carries nothing, which is a refusal rather than an empty font (§15)
        if (tables.empty())
            return {};
        // the directory must be sorted by tag, which is what an sfnt reader binary searches (§15)
        std::sort(tables.begin(), tables.end(), [](const Table& left, const Table& right) { return left.tag < right.tag; });
        // the sfnt version is truetype for glyf outlines and opentype for cff ones (§15)
        const bool has_cff = std::any_of(tables.begin(), tables.end(), [](const Table& table) { return table.tag == 0x43464620u; });
        const std::uint32_t version = has_cff ? 0x4f54544fu : 0x00010000u;
        // the selector is the power of two below the table count, which is what the search fields fold from (§15)
        std::uint32_t entry_selector = 0;
        while ((1u << (entry_selector + 1)) <= tables.size())
            entry_selector++;
        // the search range is the whole span of that power of two, in directory bytes (§15)
        const std::uint32_t search_range = static_cast<std::uint32_t>((1u << entry_selector) * 16u);
        // the range shift is what remains, which a reader uses to reject a tag faster (§15)
        const std::uint32_t range_shift = static_cast<std::uint32_t>(tables.size() * 16u - search_range);
        // the offsets are laid out once, because the directory and the data share one buffer (§15)
        std::size_t offset = 12u + tables.size() * 16u;
        std::vector<std::size_t> offsets(tables.size());
        std::vector<std::uint32_t> checksums(tables.size());
        for (std::size_t index = 0; index < tables.size(); index++) {
            offsets[index] = offset;
            // the table data is padded to four bytes, which is what the offsets of an sfnt align on (§15)
            offset += (tables[index].bytes.size() + 3u) & ~std::size_t{3u};
            // the checksum is the sum of the big endian words the padded table holds (§15)
            std::uint32_t checksum = 0;
            for (std::size_t byte = 0; byte < tables[index].bytes.size(); byte += 4) {
                std::uint32_t word = 0;
                for (std::size_t lane = 0; lane < 4u; lane++) {
                    word <<= 8;
                    if (byte + lane < tables[index].bytes.size())
                        word |= tables[index].bytes[byte + lane];
                }
                checksum += word;
            }
            checksums[index] = checksum;
        }
        // the file is the header, the directory and the padded tables in one buffer (§15, AD-34)
        std::vector<std::uint8_t> file(offset);
        // the writer lays one big endian word, which every field of an sfnt reads as (§15)
        const auto put32 = [&file](std::size_t at, std::uint32_t value) {
            file[at + 0] = static_cast<std::uint8_t>(value >> 24);
            file[at + 1] = static_cast<std::uint8_t>(value >> 16);
            file[at + 2] = static_cast<std::uint8_t>(value >> 8);
            file[at + 3] = static_cast<std::uint8_t>(value);
        };
        // the writer lays one big endian half word, which the header fields read as (§15)
        const auto put16 = [&file](std::size_t at, std::uint32_t value) {
            file[at + 0] = static_cast<std::uint8_t>(value >> 8);
            file[at + 1] = static_cast<std::uint8_t>(value);
        };
        // the version opens the header, and the four search fields follow as half words (§15)
        put32(0u, version);
        put16(4u, static_cast<std::uint32_t>(tables.size()));
        put16(6u, search_range);
        put16(8u, entry_selector);
        put16(10u, range_shift);
        // every entry carries its tag, its checksum, its offset and its length (§15)
        for (std::size_t index = 0; index < tables.size(); index++) {
            const std::size_t at = 12u + index * 16u;
            put32(at + 0u, tables[index].tag);
            put32(at + 4u, checksums[index]);
            put32(at + 8u, static_cast<std::uint32_t>(offsets[index]));
            put32(at + 12u, static_cast<std::uint32_t>(tables[index].bytes.size()));
        }
        // the tables land past the directory, each one padded to its four byte alignment (§15)
        for (std::size_t index = 0; index < tables.size(); index++) {
            std::memcpy(file.data() + offsets[index], tables[index].bytes.data(), tables[index].bytes.size());
        }
        return file;
    }
} // namespace albor::harfbuzz
