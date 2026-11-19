#include "albor/core/file_bytes.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace albor::core
{
    std::optional<std::vector<std::uint8_t>> read_regular_file(const std::filesystem::path& path) {
        // a path that names no regular file is refused before the open, because a directory read throws on libstdc++ (§6.4)
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return std::nullopt;
        // the file is opened in binary, because an asset is a byte sequence and not text (§6.4)
        std::ifstream file(path, std::ios::binary);
        // a regular file that cannot be opened carries no bytes, which is the same refusal a missing file gets (§6.4)
        if (!file.is_open())
            return std::nullopt;
        // the bytes are read whole, because every caller carries a complete asset (§6.4)
        std::vector<std::uint8_t> bytes;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return bytes;
    }
} // namespace albor::core
