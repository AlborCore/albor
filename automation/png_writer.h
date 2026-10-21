#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace albor::automation
{
    // encode premultiplied rgba8 as a png, whose straight alpha divides a partial pixel back out (ISO 15948)
    [[nodiscard]] std::string encode_png(int width, int height, std::span<const std::uint8_t> pixels);
} // namespace albor::automation
