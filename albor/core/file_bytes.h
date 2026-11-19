#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace albor::core
{
    // read a regular file whole, which refuses a directory because libstdc++ throws when one is read (§6.4, #246)
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_regular_file(const std::filesystem::path& path);
} // namespace albor::core
