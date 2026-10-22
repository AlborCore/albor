#include "platform/windows/text_encoding.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace albor::windows
{
    std::wstring wide_from_utf8(std::string_view text) {
        // an empty string converts to an empty one, which the size query would answer zero for (§5.3)
        if (text.empty())
            return std::wstring();
        // the length is measured first, because the converter writes exactly that many units (§5.3)
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        // a string that does not convert leaves the empty wide form, which no entry point reads (§5.3)
        if (length <= 0)
            return std::wstring();
        // the buffer is sized from the measure, because the converter fills what it asked for (§5.3)
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        // the units are written into the buffer, which is what the measure sized (§5.3)
        (void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    std::string utf8_from_units(const wchar_t* units, int count) {
        // the units are measured first, because the converter writes exactly that many bytes (§8.3)
        const int length = WideCharToMultiByte(CP_UTF8, 0, units, count, nullptr, 0, nullptr, nullptr);
        // units that do not convert leave the empty text, which a receiver reads as nothing (§8.3)
        if (length <= 0)
            return std::string();
        // the buffer is sized from the measure, because the converter fills what it asked for (§8.3)
        std::string text(static_cast<std::size_t>(length), '\0');
        // the bytes are written into the buffer, which is what the measure sized (§8.3)
        (void)WideCharToMultiByte(CP_UTF8, 0, units, count, text.data(), length, nullptr, nullptr);
        return text;
    }
} // namespace albor::windows
