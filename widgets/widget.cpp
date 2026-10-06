#include "widgets/widget.h"

namespace albor
{
    SemanticNode SemanticNodeProvider::semantic_node() const {
        // the announced node is handed back by value, because a consumer owns what it reads (§10.1)
        return m_node;
    }

    std::size_t SemanticNodeProvider::semantic_child_count() const {
        // the announced node is a leaf, so the walk stops here (§10.1)
        return 0;
    }

    std::unique_ptr<SemanticProvider> SemanticNodeProvider::semantic_child(std::size_t index) const {
        // a leaf exposes no child at any index, which is what the lazy walk expects (§10.1, AD-09)
        (void)index;
        return nullptr;
    }

    SemanticNode SemanticProviderView::semantic_node() const {
        // a view without a provider announces nothing, so a walk over it does not dereference null (§10.1)
        if (m_provider == nullptr)
            return SemanticNode{SemanticRole::None};
        // the watched provider answers for itself, because a view carries no state of its own (§10.1)
        return m_provider->semantic_node();
    }

    std::size_t SemanticProviderView::semantic_child_count() const {
        // a view without a provider exposes no child, so a walk stops immediately (§10.1)
        if (m_provider == nullptr)
            return 0;
        // the watched provider owns the count, because a view does not walk the ui tree itself (§10.1, AD-09)
        return m_provider->semantic_child_count();
    }

    std::unique_ptr<SemanticProvider> SemanticProviderView::semantic_child(std::size_t index) const {
        // a view without a provider exposes no child, so a walk stops immediately (§10.1)
        if (m_provider == nullptr)
            return nullptr;
        // the child the watched provider built is owned by that provider, so it is wrapped rather than taken (§10.1, AD-09)
        std::unique_ptr<SemanticProvider> child = m_provider->semantic_child(index);
        if (!child)
            return nullptr;
        // the built child is handed over as it is, because the wrapper owns it for the walk (§10.1, AD-09)
        return child;
    }

    Widget::Widget(BindingEngine* engine, std::string explicit_id) :
        Component(engine, std::move(explicit_id)) {}

    std::size_t Widget::semantic_child_count() const {
        // the semantic walk visits the same children the layout walks, so the two counts agree (§10.1, AD-09)
        return child_count();
    }

    std::unique_ptr<SemanticProvider> Widget::semantic_child(std::size_t index) const {
        // an index past the end names no child, which is what the lazy walk expects (§10.1, AD-09)
        UiNode* child = child_at(index);
        if (child == nullptr)
            return nullptr;
        // a child that exposes semantics is watched rather than copied, so the ui tree stays the only owner (§10.1, AD-09)
        if (const auto* provider = dynamic_cast<const SemanticProvider*>(child))
            return std::make_unique<SemanticProviderView>(provider);
        // a child with no semantics is announced as a node that carries no meaning, so the walk still has a shape (§10.1, AD-09)
        return std::make_unique<SemanticNodeProvider>(SemanticNode{SemanticRole::None});
    }
} // namespace albor
