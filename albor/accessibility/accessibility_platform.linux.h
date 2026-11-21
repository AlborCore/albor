#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "albor/accessibility/accessibility_adapter.h"

namespace albor::accessibility::platform
{
    // the object path the registry embeds, which the at-spi protocol fixes for an application root (§10.2)
    inline constexpr std::string_view ATSPI_ROOT_PATH = "/org/a11y/atspi/accessible/root";

    // the object path a published node answers on, which the index in the publication names (§10.2)
    [[nodiscard]] std::string atspi_object_path(std::size_t index);

    // the unique bus name the bridge announced, which a client addresses it by (§10.2)
    [[nodiscard]] std::string atspi_bus_name_for(AccessibilityAdapter& adapter);
} // namespace albor::accessibility::platform
