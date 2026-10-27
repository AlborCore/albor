#pragma once

namespace albor::tools
{
    // put the process streams in binary mode, because a json-rpc frame is a byte sequence (§12.2)
    [[nodiscard]] bool set_binary_stdio();
} // namespace albor::tools
