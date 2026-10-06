#pragma once

#include <memory>
#include <utility>

#include "semantics/semantic_provider.h"
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

        // override: hand out how many children the ui tree holds, which is what the semantic walk walks (§10.1, AD-09)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: wrap the child at an index, returning null when the index is past the end (§10.1, AD-09)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;
    };
} // namespace albor
