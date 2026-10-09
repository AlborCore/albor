#pragma once

#include <memory>
#include <utility>

#include "semantics/semantic_provider.h"
#include "style/theme.h"
#include "ui/component.h"

namespace albor
{
    // hand back the mutable owner a const provider reads through, which an invoked action needs because it changes the control as a press does (§10.1)
    template <typename T>
    [[nodiscard]] T* mutable_owner(const T* provider) {
        return const_cast<T*>(provider);
    }

    // the provider that announces one fixed node and no children, which is what a child without semantics becomes (§10.1, AD-09)
    class SemanticNodeProvider final : public SemanticProvider
    {
    public:
        // build a provider over the node it announces (§10.1)
        explicit SemanticNodeProvider(SemanticNode node) :
            m_node(std::move(node)) {}

        // override: hand out the announced node (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: hand out zero, because the announced node is a leaf (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: report that the leaf exposes no child (§10.1)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

    private:
        // the node the provider announces (§10.1)
        SemanticNode m_node;
    };

    // the provider that forwards to another provider without owning it, which keeps a semantic walk off the ui tree lifetime (§10.1, AD-09)
    class SemanticProviderView final : public SemanticProvider
    {
    public:
        // build a view over a provider the caller keeps alive (§10.1)
        explicit SemanticProviderView(const SemanticProvider* provider) :
            m_provider(provider) {}

        // override: forward to the provider the view watches (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: forward the child count of the provider the view watches (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: forward the child at an index, rewrapping it so the view stays non owning (§10.1, AD-09)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

    private:
        // the provider the view forwards to, which the ui tree owns (§10.1, AD-09)
        const SemanticProvider* m_provider;
    };

    // the base every standard widget derives from, which is a component for layout and a provider for semantics (§8, §10.1)
    class Widget : public Component, public SemanticProvider
    {
    public:
        // build a widget over the engine that settles its properties (§8.1, §7.2)
        explicit Widget(BindingEngine* engine, std::string explicit_id = {});

        // override: hand out the plate radius, which a theme sets and an unthemed widget leaves hard (§6.2)
        [[nodiscard]] float corner_radius() const override;

        // override: hand out how many children the ui tree holds, which is what the semantic walk walks (§10.1, AD-09)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: wrap the child at an index, returning null when the index is past the end (§10.1, AD-09)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // install the theme the widget resolves its paint and sizing through, which the caller owns (§6.1)
        void set_theme(const Theme* theme);

        // hand out the installed theme, which is null while no theme is installed (§6.1)
        [[nodiscard]] const Theme* theme() const;

        // hand out the state the widget is in, which a pointer transition or a press changes (§8.3)
        [[nodiscard]] ControlState state() const;

        // report whether the pointer is inside the widget, which is what the hover paint reads (§8.3)
        [[nodiscard]] bool hovered() const;

        // report whether a press is held on the widget, which is what the pressed paint reads (§8.3)
        [[nodiscard]] bool pressed() const;

    protected:
        // run the pointer transitions through the state flags, which every control calls before its own handling (§8.3)
        void handle_hover_transitions(const Event& event);

        // resolve a role in the current state, falling back to the widget's own value when no theme is installed (§6.1)
        [[nodiscard]] Color resolve(const Color* theme_colors, Color own) const;

        // the fallback metrics, which are what a widget measures from while no theme is installed (§8.2)
        [[nodiscard]] static const ControlMetrics& default_metrics();

    private:
        // the installed theme, which is null while the widget uses its own values (§6.1)
        const Theme* m_theme = nullptr;

        // whether the pointer is inside the widget, which the enter and leave events set (§8.3)
        bool m_hovered = false;

        // whether a press is held on the widget, which the down and up events set (§8.3)
        bool m_pressed = false;
    };
} // namespace albor
