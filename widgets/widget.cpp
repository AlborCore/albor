#include "widgets/widget.h"

namespace albor
{
    void apply_theme(UiNode& root, const Theme& theme) {
        // the whole subtree is walked, because a nested widget is as unthemed as the root (§6.1)
        root.visit([&theme](UiNode& node) {
            // a node that is not a widget has nothing to theme, so it is passed over (§6.1)
            if (auto* widget = dynamic_cast<Widget*>(&node))
                widget->set_theme(&theme);
        });
    }

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

    float Widget::corner_radius() const {
        // without a theme the plate is a hard rectangle, because nothing asked for a radius (§6.2)
        if (m_theme == nullptr)
            return 0.0f;
        // the theme owns the radius, because a design system draws its controls to one shape (§6.2)
        return m_theme->metrics().corner_radius;
    }

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

    void Widget::set_theme(const Theme* theme) {
        // the theme is borrowed, because the application owns it and outlives every widget (§6.1)
        m_theme = theme;
        // a theme changes the paint, so the widget asks for a frame (§6.1)
        invalidate();
    }

    const Theme* Widget::theme() const {
        // the installed theme is handed back as borrowed, because the widget does not own it (§6.1)
        return m_theme;
    }

    ControlState Widget::state() const {
        // a disabled control reports disabled whatever the pointer does, because it refuses input (§8.3)
        if (disabled())
            return ControlState::Disabled;
        // a held press wins over the hover, because a press is the later and stronger signal (§8.3)
        if (m_pressed)
            return ControlState::Pressed;
        // the pointer inside the control is the next strongest signal (§8.3)
        if (m_hovered)
            return ControlState::Hovered;
        // nothing is happening to the control, which is the resting paint (§8.3)
        return ControlState::Normal;
    }

    bool Widget::hovered() const {
        // the flag is what the pointer transitions set, and the tests read (§8.3)
        return m_hovered;
    }

    bool Widget::pressed() const {
        // the flag is what the press set, and the tests read (§8.3)
        return m_pressed;
    }

    Color Widget::resolve(const Color* theme_colors, Color own) const {
        // without a theme the widget keeps its own value, so no existing test changes behaviour (§6.1)
        if (m_theme == nullptr)
            return own;
        // with a theme the role wins, because the theme is the surface an application configures (§6.1)
        return theme_colors[state_index(state())];
    }

    const ControlMetrics& Widget::default_metrics() {
        // a theme with no overrides holds the same numbers the widgets used as literals, so both paths agree (§8.2)
        static const ControlMetrics metrics;
        // the fallback is shared, because nothing writes to it (§8.2)
        return metrics;
    }

    void Widget::handle_hover_transitions(const Event& event) {
        // the enter is the hover flag, so a move across a boundary flips it exactly once (§8.3)
        if (event.kind == EventKind::PointerEnter) {
            // an already hovered widget is not repainted, because nothing about it changed (§6.1)
            if (m_hovered)
                return;
            m_hovered = true;
            invalidate();
            return;
        }
        // a leave clears the hover and any press with it, because a press held outside is not a press here (§8.3)
        if (event.kind == EventKind::PointerLeave) {
            // a widget that was neither hovered nor pressed changes nothing, so it is not repainted (§6.1)
            if (!m_hovered && !m_pressed)
                return;
            m_hovered = false;
            m_pressed = false;
            invalidate();
            return;
        }
        // a press sets the pressed flag, which the state reads (§8.3)
        if (event.kind == EventKind::PointerDown) {
            // a second press while one is held changes nothing, so the widget is not repainted (§8.3)
            if (m_pressed)
                return;
            m_pressed = true;
            invalidate();
            return;
        }
        // a release clears the press, while a pointer capture keeps the moves coming (§8.3)
        if (event.kind == EventKind::PointerUp) {
            // a widget that held no press changes nothing, so it is not repainted (§6.1)
            if (!m_pressed)
                return;
            m_pressed = false;
            invalidate();
            return;
        }
        // a move that arrives outside the bounds clears the press, so dragging away releases it (§8.3)
        if (event.kind == EventKind::PointerMove && m_pressed) {
            // a press held inside the widget survives the move, because the drag belongs to it (§8.3)
            if (hit_test(event.position))
                return;
            // the pointer left while a press was held, so the press is released like a hover (§8.3)
            m_pressed = false;
            invalidate();
        }
    }
} // namespace albor
