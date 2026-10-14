#pragma once

#include <functional>

#include "style/theme_builder.h"
#include "ui/ui_node.h"

namespace albor
{
    // the object that owns both schemes and decides which one is installed (§6.1)
    class ThemeController
    {
    public:
        // build both schemes from one set of accents, which is enough when only the states differ (§6.1)
        explicit ThemeController(Accents accents);

        // build both schemes from one accent set each, because a surface is what a scheme is drawn on (§6.1)
        ThemeController(Accents light_accents, Accents dark_accents);

        // install a scheme over a subtree, which is the one call an application makes at start up (§6.1)
        void install(UiNode& root, ColorScheme scheme);

        // move to a scheme and reinstall it over the subtree it was given, which is what a flip is (§6.1)
        void set_scheme(ColorScheme scheme);

        // swap between the two schemes over the subtree, which is what a toggle control calls (§6.1)
        void toggle();

        // hand out the installed scheme, which a toggle reads to show what it would move to (§6.1)
        [[nodiscard]] ColorScheme scheme() const;

        // hand out the installed theme, which an application reads its surface colour from (§6.1)
        [[nodiscard]] const Theme& theme() const;

        // install the handler a scheme change reaches, which is where an application persists the preference (§6.1)
        void set_scheme_changed_handler(std::function<void(ColorScheme)> handler);

        // hand out the subtree the controller installed over, which is null until one is (§6.1)
        [[nodiscard]] UiNode* root() const;

    private:
        // reinstall the current scheme over the subtree, which a change of scheme runs through (§6.1)
        void reinstall();

        // the light theme, which a flip away from the dark scheme lands on (§6.1)
        Theme m_light;

        // the dark theme, which a flip away from the light scheme lands on (§6.1)
        Theme m_dark;

        // the scheme currently installed, which decides which of the two themes is live (§6.1)
        ColorScheme m_scheme = ColorScheme::Dark;

        // the subtree the themes were installed over, which a later change reapplies (§6.1)
        UiNode* m_root = nullptr;

        // the handler a scheme change reaches, which an application installs to persist the preference (§6.1)
        std::function<void(ColorScheme)> m_scheme_changed;
    };
} // namespace albor
