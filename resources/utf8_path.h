#pragma once

#include <filesystem>
#include <string_view>

namespace albor::resources
{
    // turn utf-8 bytes a ui file or an ir carries into a native path, which a windows code page does not do (§6.4)
    [[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);
} // namespace albor::resources
