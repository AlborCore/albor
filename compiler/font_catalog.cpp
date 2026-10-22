#include "compiler/font_catalog.h"

#include <utility>

namespace albor::compiler
{
    void FontCatalog::add(std::string family, FontWeight weight) {
        // the face joins the catalog, because a selection may name it later (§9.1)
        m_faces.push_back(Face{.family = std::move(family), .weight = weight});
    }

    void FontCatalog::set_default_family(std::string family) {
        // the default replaces the previous one, because a build names exactly one default (§9.1)
        m_default_family = std::move(family);
    }

    bool FontCatalog::carries(std::string_view family) const {
        // any face of the family answers a bare family name, because the weight follows (§9.1)
        for (const Face& face : m_faces) {
            // the family decides, because a weight is not part of a bare selection (§9.1)
            if (face.family == family)
                return true;
        }
        // no face of that family is carried, which the caller reports (§9.1)
        return false;
    }

    bool FontCatalog::carries(std::string_view family, FontWeight weight) const {
        // the pairing is checked exactly, because substituting a weight would draw another face (§9.1, AD-34)
        for (const Face& face : m_faces) {
            // both fields decide, which is what makes a weighted selection exact (§9.1)
            if (face.family == family && face.weight == weight)
                return true;
        }
        // the pairing is not carried, which the caller reports (§9.1)
        return false;
    }

    const std::string& FontCatalog::default_family() const {
        // the default is handed out by reference, because it is the catalog's own member (§9.1)
        return m_default_family;
    }

    std::size_t FontCatalog::count() const {
        // the count is what a test asserts on, because an empty catalog skips validation (§11.1)
        return m_faces.size();
    }
} // namespace albor::compiler
