#include "resources/utf8_path.h"

#include <string>

namespace albor::resources
{
    std::filesystem::path path_from_utf8(std::string_view text) {
        // an empty value names no file, so there are no bytes to convert (§6.4)
        if (text.empty())
            return {};
        // the u8 spelling is what makes the conversion locale independent (§6.4)
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }
} // namespace albor::resources
