#pragma once

#include <cstdint>

namespace albor::prototype_d
{

    // identify a property by a dense index into its component store (§13.3)
    using PropertyId = std::uint32_t;

    // identify a binding by a dense index into the binding table
    using BindingId = std::uint32_t;

    constexpr PropertyId INVALID_PROPERTY = static_cast<PropertyId>(-1);
    constexpr BindingId INVALID_BINDING = static_cast<BindingId>(-1);

} // namespace albor::prototype_d
