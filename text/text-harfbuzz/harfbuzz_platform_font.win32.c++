#include "text/text-harfbuzz/harfbuzz_platform_font.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwrite.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace albor::harfbuzz
{
    namespace
    {
        // one big endian word the sfnt fields read as, which the view reads in place (§15)
        std::uint32_t be32(const std::uint8_t* at) { return (static_cast<std::uint32_t>(at[0]) << 24) | (static_cast<std::uint32_t>(at[1]) << 16) | (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]); }

        // one big endian half word the sfnt header reads as (§15)
        std::uint32_t be16(const std::uint8_t* at) { return (static_cast<std::uint32_t>(at[0]) << 8) | static_cast<std::uint32_t>(at[1]); }
    } // namespace

    std::vector<std::uint8_t> platform_font_bytes(const void* typeface) {
        // the handle is the dwrite font face the resolver issued, which only this half may name (§3.5, AD-19)
        auto* face = static_cast<IDWriteFontFace*>(const_cast<void*>(typeface));
        // a handle the platform did not resolve carries no bytes, which is a refusal rather than an empty font (§15)
        if (face == nullptr)
            return {};
        // the count of the files the face fronts is measured first, because the walk sizes from it (§15)
        UINT32 file_count = 0;
        // a face that names no file carries nothing, which a memory backed face is (§15)
        if (face->GetFiles(&file_count, nullptr) != S_OK || file_count == 0)
            return {};
        // the files are read into the measured span, which the face increments under com (§15)
        std::vector<IDWriteFontFile*> files(static_cast<std::size_t>(file_count), nullptr);
        if (face->GetFiles(&file_count, files.data()) != S_OK) {
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the first file is the one the sfnt directory is read from (§15)
        const void* key = nullptr;
        UINT32 key_size = 0;
        if (files[0]->GetReferenceKey(&key, &key_size) != S_OK || key == nullptr) {
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the loader is the loader that fronts this file, which the local one resolves to a path (§15)
        IDWriteFontFileLoader* loader = nullptr;
        if (files[0]->GetLoader(&loader) != S_OK || loader == nullptr) {
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the local file loader is the one a system face fronts, which the query interfaces to (§15)
        IDWriteLocalFontFileLoader* local = nullptr;
        if (loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), reinterpret_cast<void**>(&local)) != S_OK || local == nullptr) {
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the path length is measured first, because the path is converted into the measured span (§15)
        UINT32 path_length = 0;
        // a path that did not measure leaves the refusal (§15)
        if (local->GetFilePathLengthFromKey(key, key_size, &path_length) != S_OK || path_length == 0) {
            local->Release();
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the path is converted, which the terminator joins (§15)
        std::wstring path(static_cast<std::size_t>(path_length) + 1, L'\0');
        if (local->GetFilePathFromKey(key, key_size, path.data(), path_length + 1) != S_OK) {
            local->Release();
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the file is opened read only, which admits a rename but not the truncation a mapped file dies by (§15)
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            local->Release();
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the size is measured first, because the view covers exactly what the file carries (§15)
        LARGE_INTEGER size{};
        // a measure that failed carries nothing, which is the quiet refusal (§15)
        if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0) {
            CloseHandle(handle);
            local->Release();
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the file is mapped read only, because the tables are copied out of a mapping (§15)
        HANDLE mapping = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping == nullptr) {
            CloseHandle(handle);
            local->Release();
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the view covers the file, which is what the walk reads (§15)
        const auto* view = static_cast<const std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (view == nullptr) {
            CloseHandle(mapping);
            CloseHandle(handle);
            local->Release();
            loader->Release();
            for (IDWriteFontFile* file : files) {
                // a file that was never handed over releases nothing
                if (file != nullptr)
                    file->Release();
            }
            return {};
        }
        // the view's span bounds every read below, which the offsets are validated against (§15)
        const std::uint64_t view_size = static_cast<std::uint64_t>(size.QuadPart);
        // the member offset starts at zero, which is the whole of a single face file (§15)
        UINT64 member_offset = 0;
        // a collection file carries one header for the whole set, which the face index selects into (§15)
        if (view_size >= 12u && be32(view) == 0x74746366u) {
            // the collection names its members, and the face index selects one of them (§15)
            const std::uint32_t face_index = face->GetIndex();
            // the member count is a word, which is how the collection header carries it (§15)
            const std::uint32_t member_count = be32(view + 8u);
            // an index past the members resolves to the first member, which keeps the walk safe (§15)
            const std::uint64_t selected = face_index < member_count ? face_index : 0u;
            // the member offset stands in the offset array, which the bounds validate (§15)
            const std::uint64_t array_at = 12u + selected * 4u;
            // an array past the file leaves the single face offset, which is the safe answer (§15)
            if (array_at + 4u <= view_size)
                member_offset = be32(view + array_at);
        }
        // the sub font's header stands at the member offset, which a collection file carries (§15)
        std::vector<std::uint8_t> carried;
        if (member_offset + 12u <= view_size) {
            // the version tag opens the sub font, and the table count follows as a half word (§15)
            const std::uint32_t version = be32(view + member_offset);
            const std::uint32_t table_count = be16(view + member_offset + 4u);
            // the directory spans the table count, which the bounds validate (§15)
            if (member_offset + 12u + static_cast<std::uint64_t>(table_count) * 16u <= view_size) {
                // one directory entry carries its tag, its checksum, its offset and its length (§15)
                struct Table
                {
                    // the four character identifier the directory sorts on (§15)
                    std::uint32_t tag = 0;

                    // the table bytes themselves (§15)
                    std::vector<std::uint8_t> bytes;
                };
                std::vector<Table> tables;
                for (std::uint32_t at = 0; at < table_count; at++) {
                    // the entry stands at the directory's own offset, which the walk reads (§15)
                    const std::uint8_t* entry = view + member_offset + 12u + static_cast<std::size_t>(at) * 16u;
                    // the table's span is validated, because a corrupt face names spans past the file (§15)
                    const std::uint32_t tag = be32(entry);
                    const std::uint64_t table_offset = be32(entry + 8u);
                    const std::uint64_t length = be32(entry + 12u);
                    // a table whose span leaves the file is skipped, which keeps the walk safe (§15)
                    if (table_offset > view_size || length > view_size - table_offset)
                        continue;
                    // the bytes are copied out, because the view dies with the mapping (§15)
                    tables.push_back(Table{.tag = tag, .bytes = std::vector<std::uint8_t>(view + table_offset, view + table_offset + length)});
                }
                // a face the file carried no table for carries nothing, which is the refusal of §15
                if (!tables.empty()) {
                    // the directory must be sorted by tag, which is what an sfnt reader binary searches (§15)
                    std::sort(tables.begin(), tables.end(), [](const Table& left, const Table& right) { return left.tag < right.tag; });
                    // the selector is the power of two below the count, which is what the search fields fold from (§15)
                    std::uint32_t entry_selector = 0;
                    while ((std::size_t{1} << (entry_selector + 1)) <= tables.size())
                        entry_selector++;
                    // the search range is the whole span of that power of two, in directory bytes (§15)
                    const std::uint32_t search_range = static_cast<std::uint32_t>((1u << entry_selector) * 16u);
                    // the range shift is what remains, which a reader uses to reject a tag faster (§15)
                    const std::uint32_t range_shift = static_cast<std::uint32_t>(tables.size() * 16u - search_range);
                    // the offsets are laid out once, because the directory and the data share one buffer (§15)
                    std::size_t data_offset = 12u + tables.size() * 16u;
                    std::vector<std::size_t> offsets(tables.size());
                    std::vector<std::uint32_t> checksums(tables.size());
                    for (std::size_t index = 0; index < tables.size(); index++) {
                        offsets[index] = data_offset;
                        // the table data is padded to four bytes, which is what the offsets of an sfnt align on (§15)
                        data_offset += (tables[index].bytes.size() + 3u) & ~std::size_t{3u};
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
                    carried.resize(data_offset);
                    // the writer lays one big endian word, which every field of an sfnt reads as (§15)
                    const auto put32 = [&carried](std::size_t at, std::uint32_t value) {
                        carried[at + 0] = static_cast<std::uint8_t>(value >> 24);
                        carried[at + 1] = static_cast<std::uint8_t>(value >> 16);
                        carried[at + 2] = static_cast<std::uint8_t>(value >> 8);
                        carried[at + 3] = static_cast<std::uint8_t>(value);
                    };
                    // the writer lays one big endian half word, which the header fields read as (§15)
                    const auto put16 = [&carried](std::size_t at, std::uint32_t value) {
                        carried[at + 0] = static_cast<std::uint8_t>(value >> 8);
                        carried[at + 1] = static_cast<std::uint8_t>(value);
                    };
                    // the version opens the header, which is the face's own tag (§15)
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
                        std::memcpy(carried.data() + offsets[index], tables[index].bytes.data(), tables[index].bytes.size());
                    }
                }
            }
        }
        // the mapping and the handles go back, because the copy owns its own bytes now (§15)
        (void)UnmapViewOfFile(view);
        CloseHandle(mapping);
        CloseHandle(handle);
        local->Release();
        loader->Release();
        for (IDWriteFontFile* file : files) {
            // a file that was never handed over releases nothing
            if (file != nullptr)
                file->Release();
        }
        return carried;
    }
} // namespace albor::harfbuzz
