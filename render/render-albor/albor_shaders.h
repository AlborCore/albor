#pragma once

#include <string_view>

namespace albor::render
{
    // hand out the metal shading language source the device compiles once at startup (AD-16)
    [[nodiscard]] std::string_view shader_source();
} // namespace albor::render
