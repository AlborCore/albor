#pragma once

#include <string>
#include <vector>

namespace albor
{
    // the weight of a face within a family, which picks one among the carried faces (§9.1, AD-34)
    enum class FontWeight
    {
        // the lightest stroke a family may carry (§9.1)
        Thin,
        // a stroke below the regular one (§9.1)
        Light,
        // the stroke an application gets when it names no weight (§9.1)
        Regular,
        // a stroke between regular and semibold (§9.1)
        Medium,
        // a stroke below bold (§9.1)
        Semibold,
        // the emphatic stroke a family usually carries (§9.1)
        Bold,
        // the heaviest stroke a family may carry (§9.1)
        Black,
    };

    // a selection a measure call carries, naming a family, a weight and a fallback chain (§9.1, AD-34)
    struct FontSpec
    {
        // the family an application asked for, which is a label the registry resolves and not a platform lookup (§9.1)
        std::string family;

        // the weight that selects a face within the family, defaulting to the regular face (§9.1)
        FontWeight weight = FontWeight::Regular;

        // the families to try in order when the named one is absent, which must stay deterministic (§15, AD-34)
        std::vector<std::string> fallback;
    };
} // namespace albor
