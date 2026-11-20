#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace albor::core
{
    // read an environment variable into an owned value, which avoids the unsafe getenv entry point on windows
    [[nodiscard]] std::optional<std::string> get_environment_variable(std::string_view name);
} // namespace albor::core
