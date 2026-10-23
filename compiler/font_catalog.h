#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/font_weight.h"

namespace albor::compiler
{
    // the faces a target carries, which the compiler checks a declarative selection against (§11.1, AD-34)
    class FontCatalog
    {
    public:
        // record one face the target carries, which a ui file may name by family and weight (§9.1)
        void add(std::string family, FontWeight weight);

        // name the family a run without a family resolves, which the build declared (§9.1)
        void set_default_family(std::string family);

        // report whether any face of the family is carried, which a bare family name needs (§9.1)
        [[nodiscard]] bool carries(std::string_view family) const;

        // report whether the exact family and weight are carried, which a weighted selection needs (§9.1, AD-34)
        [[nodiscard]] bool carries(std::string_view family, FontWeight weight) const;

        // hand out the family a bare selection resolves, which is empty when the build named none (§9.1)
        [[nodiscard]] const std::string& default_family() const;

        // report how many faces the target carries, which an empty catalog has none of (§11.1)
        [[nodiscard]] std::size_t count() const;

    private:
        // one face the build carries, which is the pairing a selection resolves (§9.1)
        struct Face
        {
            // the family the face is registered under (§9.1)
            std::string family;

            // the weight the face carries inside that family (§9.1, AD-34)
            FontWeight weight = FontWeight::Regular;
        };

        // the faces the target carries, in declaration order (§11.3)
        std::vector<Face> m_faces;

        // the family a bare selection resolves, which is empty when the build named none (§9.1)
        std::string m_default_family;
    };
} // namespace albor::compiler
