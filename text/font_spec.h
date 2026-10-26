#pragma once

#include <string>
#include <vector>

#include "core/font_weight.h"

namespace albor
{
    // a selection a measure call carries, naming a family, a weight and a fallback chain (§9.1, AD-34)
    struct FontSpec
    {
        // the family an application asked for, which is a label the registry resolves and not a platform lookup (§9.1)
        std::string family{};

        // the weight that selects a face within the family, defaulting to the regular face (§9.1)
        FontWeight weight = FontWeight::Regular;

        // the families to try in order when the named one is absent, which must stay deterministic (§15, AD-34)
        std::vector<std::string> fallback{};
    };
} // namespace albor
