#include "widgets/theme_controller.h"

#include <utility>

#include "widgets/widget.h"

namespace albor
{
    ThemeController::ThemeController(ThemePreset preset) :
        ThemeController(Accents{}, Accents{}, preset) {}

    ThemeController::ThemeController(Accents accents, ThemePreset preset) :
        ThemeController(accents, accents, preset) {}

    ThemeController::ThemeController(Accents light_accents, Accents dark_accents, ThemePreset preset) :
        m_preset(preset), m_light(build(light_accents, ColorScheme::Light)), m_dark(build(dark_accents, ColorScheme::Dark)), m_light_accents(light_accents), m_dark_accents(dark_accents) {}

    Theme ThemeController::build(const Accents& accents, ColorScheme scheme) const {
        // the derived preset is the portable baseline, which the builder answers (§6.1)
        if (m_preset == ThemePreset::Derived)
            return scheme == ColorScheme::Dark ? ThemeBuilder::dark(accents) : ThemeBuilder::light(accents);
        // a macos preset reads as the macos control set (§8.4)
        if (m_preset == ThemePreset::Macos)
            return Theme::macos(accents, scheme);
        // a fluent preset reads as the windows control set (§8.4)
        if (m_preset == ThemePreset::Fluent)
            return Theme::fluent(accents, scheme);
        // a pro dark preset reads as a professional tool surface (§8.4)
        return Theme::pro_dark(accents);
    }

    void ThemeController::set_accents(Accents light_accents, Accents dark_accents) {
        // the accents are stored first, so a later scheme flip rebuilds from the same source (§8.4)
        m_light_accents = light_accents;
        m_dark_accents = dark_accents;
        // both schemes are rebuilt, because every colour in the palette derives from the accents (§6.1)
        m_light = build(m_light_accents, ColorScheme::Light);
        m_dark = build(m_dark_accents, ColorScheme::Dark);
        // the rebuilt theme is put over the tree, which is where the accent change reaches the screen (§8.4)
        reinstall();
    }

    void ThemeController::install(UiNode& root, ColorScheme scheme) {
        // the subtree is remembered first, because a later flip has nothing to reinstall without it (§6.1)
        m_root = &root;
        // the scheme is stored first, so the handler below reads the value the tree was given (§6.1)
        m_scheme = scheme;
        // the whole subtree is walked, because a nested widget is as unthemed as the root (§6.1)
        apply_theme(root, theme());
        // the application is told, which is where the preference would be persisted (§6.1)
        if (m_scheme_changed)
            m_scheme_changed(m_scheme);
    }

    void ThemeController::set_scheme(ColorScheme scheme) {
        // an unchanged scheme installs nothing, because the tree already carries that theme (§6.1)
        if (m_scheme == scheme)
            return;
        // the scheme is stored first, so the reinstall below draws from the new one (§6.1)
        m_scheme = scheme;
        // the live theme is put over the tree, which is what makes the flip reach the screen (§6.1)
        reinstall();
    }

    void ThemeController::toggle() {
        // the dark scheme flips to the light one and back, which is the two a preference can hold (§6.1)
        set_scheme(m_scheme == ColorScheme::Dark ? ColorScheme::Light : ColorScheme::Dark);
    }

    ColorScheme ThemeController::scheme() const {
        // the scheme is what the tree was last installed with (§6.1)
        return m_scheme;
    }

    const Theme& ThemeController::theme() const {
        // the dark theme is the resting one, because the accents a caller builds default to a dark surface (§6.1)
        if (m_scheme == ColorScheme::Light)
            return m_light;
        // the dark theme answers every other scheme, because there are only two (§6.1)
        return m_dark;
    }

    void ThemeController::set_scheme_changed_handler(std::function<void(ColorScheme)> handler) {
        // the handler is installed on the controller, which the application owns alongside the tree (§6.1)
        m_scheme_changed = std::move(handler);
    }

    UiNode* ThemeController::root() const {
        // the subtree is the one the last install was given, which is null before the first (§6.1)
        return m_root;
    }

    void ThemeController::reinstall() {
        // a controller with no subtree has nothing to reinstall, because it was never installed (§6.1)
        if (m_root == nullptr)
            return;
        // the tree is walked again, because every widget resolves its paint through the theme (§6.1)
        apply_theme(*m_root, theme());
        // the application is told, which is where the preference would be persisted (§6.1)
        if (m_scheme_changed)
            m_scheme_changed(m_scheme);
    }
} // namespace albor
