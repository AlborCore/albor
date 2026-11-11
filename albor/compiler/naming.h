#pragma once

#include <string>
#include <string_view>

namespace albor::compiler
{
    // render an identifier in snake case, which is how a generated unit is named (§11.3, §14.1)
    [[nodiscard]] std::string snake_case(std::string_view name);
} // namespace albor::compiler
