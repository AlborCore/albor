#include "albor/text/text-harfbuzz/harfbuzz_platform_font.h"

#if defined(ALBOR_HAVE_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#include <hb.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "albor/core/file_bytes.h"

namespace albor::harfbuzz
{
#if defined(ALBOR_HAVE_FONTCONFIG)
    namespace
    {
        // one table the sfnt directory carries, which is the tag and the bytes the face holds (§15)
        struct Table
        {
            // the four character identifier the directory sorts on (§15)
            std::uint32_t tag = 0;

            // the table bytes themselves (§15)
            std::vector<std::uint8_t> bytes;
        };

        // read a font file into memory, because the face is rebuilt from its tables and not a path (§15, AD-34)
        std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
            // a path that names no readable file carries no font, so no table is rebuilt from it (§9.1)
            return core::read_regular_file(path).value_or(std::vector<std::uint8_t>());
        }
    } // namespace

    std::vector<std::uint8_t> platform_font_bytes(const void* typeface) {
        // the handle is the fontconfig pattern the resolver issued, which only this half may name (§3.5, AD-19)
        const auto* pattern = static_cast<const FcPattern*>(typeface);
        // a handle the platform did not resolve carries no bytes, which is a refusal rather than an empty font (§15)
        if (pattern == nullptr)
            return {};
        // the file the face fronts is what the tables are read from, which fontconfig records (§15)
        FcChar8* file_path = nullptr;
        // a pattern with no file carries nothing, because there is no face to rebuild (§15)
        if (FcPatternGetString(pattern, FC_FILE, 0, &file_path) != FcResultMatch || file_path == nullptr)
            return {};
        // the index selects the member of a collection, which a single face leaves at zero (§15)
        int packed_index = 0;
        // a pattern with no index names the first member, which fontconfig reports as zero (§15)
        if (FcPatternGetInteger(pattern, FC_INDEX, 0, &packed_index) != FcResultMatch)
            packed_index = 0;
        // fontconfig packs the member low and the instance high, and a static rebuild hands over the base member (§15)
        const unsigned int face_index = static_cast<unsigned int>(packed_index) & 0xFFFFu;
        // the file is read whole, because the face is rebuilt from its tables and not from a path (§15, AD-34)
        const std::vector<std::uint8_t> file = read_file_bytes(reinterpret_cast<const char*>(file_path));
        // a file that could not be read carries nothing, which the registry must not key on (§15)
        if (file.empty())
            return {};
        // the blob borrows the file bytes, so the face reads them without a second copy (§15)
        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(file.data()), static_cast<unsigned int>(file.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        // a blob that did not come up leaves no face to read (§15)
        if (blob == nullptr)
            return {};
        // the face is read at the member index, which is how a collection face is selected (§15)
        hb_face_t* face = hb_face_create(blob, face_index);
        // the blob is released after the face takes its own reference to it (§15)
        hb_blob_destroy(blob);
        // a face with no glyph cannot carry tables, so it is refused rather than rebuilt empty (§15)
        if (face == nullptr || hb_face_get_glyph_count(face) == 0) {
            // a face that exists despite the refusal is released, because nothing reads it (§15)
            if (face != nullptr)
                hb_face_destroy(face);
            return {};
        }
        // the tag count is the return value a null list reports, which is how harfbuzz states the total (§15)
        const unsigned int tag_count = hb_face_get_table_tags(face, 0, nullptr, nullptr);
        // the tags are read into the measured span, which the walk then reads (§15)
        std::vector<hb_tag_t> tags(static_cast<std::size_t>(tag_count));
        // the second ask fills the span, and its output is the number of tags written (§15)
        unsigned int written = tag_count;
        hb_face_get_table_tags(face, 0, &written, tags.data());
        // a span that came back shorter is trimmed, so the walk never reads an unset tag (§15)
        tags.resize(static_cast<std::size_t>(written));
        // the tables are copied out, because the face goes away with the walk (§15)
        std::vector<Table> tables;
        for (hb_tag_t tag : tags) {
            // the table the tag names is what the face carries, and a tag with no data is skipped (§15)
            hb_blob_t* table = hb_face_reference_table(face, tag);
            // a table that came up is copied into the rebuild list (§15)
            if (table != nullptr) {
                // the length is the table's own count, which sizes the copy (§15)
                const unsigned int length = hb_blob_get_length(table);
                // the pointer is the table's own bytes, which are copied before the blob is released (§15)
                const char* data = hb_blob_get_data(table, nullptr);
                // the bytes join the rebuild list, because the directory needs all of them at once (§15)
                tables.push_back(Table{.tag = static_cast<std::uint32_t>(tag), .bytes = std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data), reinterpret_cast<const std::uint8_t*>(data) + length)});
                // the table blob is released once its bytes are copied (§15)
                hb_blob_destroy(table);
            }
        }
        // the face is released once its tables are copied out (§15)
        hb_face_destroy(face);
        // a face the platform handed no table for carries nothing, which is a refusal rather than an empty font (§15)
        if (tables.empty())
            return {};
        // the directory must be sorted by tag, which is what an sfnt reader binary searches (§15)
        std::sort(tables.begin(), tables.end(), [](const Table& left, const Table& right) { return left.tag < right.tag; });
        // the sfnt version is truetype for glyf outlines and opentype for cff ones (§15)
        const bool has_cff = std::any_of(tables.begin(), tables.end(), [](const Table& table) { return table.tag == 0x43464620u; });
        // the opentype version is what a cff face opens with (§15)
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
                // the word is assembled from up to four big endian lanes (§15)
                std::uint32_t word = 0;
                for (std::size_t lane = 0; lane < 4u; lane++) {
                    word <<= 8;
                    // a lane past the table's end contributes a zero byte (§15)
                    if (byte + lane < tables[index].bytes.size())
                        word |= tables[index].bytes[byte + lane];
                }
                checksum += word;
            }
            checksums[index] = checksum;
        }
        // the file is the header, the directory and the padded tables in one buffer (§15, AD-34)
        std::vector<std::uint8_t> output(offset);
        // the writer lays one big endian word, which every field of an sfnt reads as (§15)
        const auto put32 = [&output](std::size_t position, std::uint32_t value) {
            output[position + 0] = static_cast<std::uint8_t>(value >> 24);
            output[position + 1] = static_cast<std::uint8_t>(value >> 16);
            output[position + 2] = static_cast<std::uint8_t>(value >> 8);
            output[position + 3] = static_cast<std::uint8_t>(value);
        };
        // the writer lays one big endian half word, which the header fields read as (§15)
        const auto put16 = [&output](std::size_t position, std::uint32_t value) {
            output[position + 0] = static_cast<std::uint8_t>(value >> 8);
            output[position + 1] = static_cast<std::uint8_t>(value);
        };
        // the version opens the header, and the four search fields follow as half words (§15)
        put32(0u, version);
        put16(4u, static_cast<std::uint32_t>(tables.size()));
        put16(6u, search_range);
        put16(8u, entry_selector);
        put16(10u, range_shift);
        // every entry carries its tag, its checksum, its offset and its length (§15)
        for (std::size_t index = 0; index < tables.size(); index++) {
            // the entry starts after the header and the entries before it (§15)
            const std::size_t entry = 12u + index * 16u;
            put32(entry + 0u, tables[index].tag);
            put32(entry + 4u, checksums[index]);
            put32(entry + 8u, static_cast<std::uint32_t>(offsets[index]));
            put32(entry + 12u, static_cast<std::uint32_t>(tables[index].bytes.size()));
        }
        // the tables land past the directory, each one padded to its four byte alignment (§15)
        for (std::size_t index = 0; index < tables.size(); index++)
            std::memcpy(output.data() + offsets[index], tables[index].bytes.data(), tables[index].bytes.size());
        return output;
    }
#else
    std::vector<std::uint8_t> platform_font_bytes(const void*) {
        // a build without fontconfig has no platform face, so no bytes are handed over (§9.1, AD-34)
        return {};
    }
#endif
} // namespace albor::harfbuzz
