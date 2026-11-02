#pragma once

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
} // namespace albor
