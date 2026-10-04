#pragma once

#include <cstdint>

namespace albor_proto_e
{

    // component-local property identifier (§13.3)
    struct PropertyId
    {
        uint32_t base;
        uint16_t local_offset;

        // return the globally unique property index by summing base and local offset
        [[nodiscard]] uint32_t to_global() const noexcept { return base + local_offset; }
    };

} // namespace albor_proto_e
