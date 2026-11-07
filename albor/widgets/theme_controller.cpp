#include "albor/widgets/theme_controller.h"

#include <utility>

#include "albor/widgets/widget.h"

namespace albor
{
    ThemeController::ThemeController(ThemePreset preset) :
        ThemeController(Accents{}, Accents{}, preset) {}

    ThemeController::ThemeController(Accents accents, ThemePreset preset) :
        ThemeController(accents, accents, preset) {}

    ThemeController::ThemeController(Accents light_accents, Accents dark_accents, ThemePreset preset) :
        m_preset(preset), m_light(build(light_accents, ColorScheme::Light)), m_dark(build(dark_accents, ColorScheme::Dark)), m_light_accents(light_accents), m_dark_accents(dark_accents) {
        // the resting scheme is live before an install, so theme() answers the preset immediately (§6.1)
        sync_theme();
    }

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
        // a new install replaces any previous root, so the old handler must become inert (§8.4)
        m_attached->generation++;
        // the live theme is stored where the attached-subtree handler can read it (§8.4)
        sync_theme();
        // a subtree built later is themed when it joins, which is what a conditional body or repeat row needs (§8.4)
        const std::uint64_t generation = m_attached->generation;
        root.set_attached_handler([state = std::weak_ptr<AttachedState>(m_attached), generation](UiNode* node) {
            // a null node names nothing, so there is no subtree to theme (§8.4)
            if (node == nullptr)
                return;
            // a controller that died leaves an expired state, so the handler does nothing (§8.4)
            const std::shared_ptr<AttachedState> locked = state.lock();
            // a handler left on an older root does nothing, because the controller manages one tree (§8.4)
            if (locked == nullptr || locked->generation != generation)
                return;
            // the new subtree is walked, because every widget in it needs the live theme (§8.4)
            apply_theme(*node, locked->theme);
        });
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
        // the live value is the one the attached-subtree handler also reads, so one object serves the whole tree (§8.4)
        return m_attached->theme;
    }

    void ThemeController::set_scheme_changed_handler(std::function<void(ColorScheme)> handler) {
        // the handler is installed on the controller, which the application owns alongside the tree (§6.1)
        m_scheme_changed = std::move(handler);
    }

    UiNode* ThemeController::root() const {
        // the subtree is the one the last install was given, which is null before the first (§6.1)
        return m_root;
    }

    void ThemeController::sync_theme() {
        // the scheme selects the built theme, which is the one object theme() and the handler both read (§8.4)
        m_attached->theme = m_scheme == ColorScheme::Light ? m_light : m_dark;
    }

    void ThemeController::reinstall() {
        // the live value is refreshed even without a root, because an accent change answers theme() before install (§6.1)
        sync_theme();
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
