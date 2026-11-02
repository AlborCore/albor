#pragma once

#include <string>
#include <string_view>

namespace albor::windows
{
    // convert utf8 text to the wide form the win32 entry points take (§5.3)
    [[nodiscard]] std::wstring wide_from_utf8(std::string_view text);

    // convert a run of utf16 units to the utf8 text a normalized event carries (§8.3)
    [[nodiscard]] std::string utf8_from_units(const wchar_t* units, int count);
} // namespace albor::windows
