#include "widgets/widget.h"

#include "commands/command.h"

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

    void Widget::paint_background(RenderContext& context) {
        // the plate goes down first, because the ring stands outside it rather than over it (§6.1)
        Component::paint_background(context);
        // the ring follows the plate, which is the order the eye reads a focused control in (§6.1)
        paint_focus_ring(context);
    }

    Rect Widget::focus_ring_rect() const {
        // the ring surrounds the arranged bounds by default, which is where a plate control draws (§8.4)
        return bounds();
    }

    void Widget::paint_focus_ring(RenderContext& context) const {
        // without a theme there is no ring, because nothing configured one (§6.1)
        if (m_theme == nullptr)
            return;
        // a hidden control draws no ring, because there is nothing on screen to point at (§6.1)
        if (!visible())
            return;
        // a control with no area draws no ring, because a ring around nothing is nothing (§6.2)
        if (bounds().empty())
            return;
        // the ring marks the keyboard focus alone, because a hover says nothing about taking keys (§6.1)
        if (!m_focused)
            return;
        // the ring resolves by state, so a hover and a press change it with the plate it surrounds (§6.1)
        const Color ring = resolve(m_theme->palette().focus_ring, Color::rgba(0.0f, 0.0f, 0.0f, 0.0f));
        // a ring with no alpha draws nothing, which is how the normal state carries no signal (§6.1)
        if (ring.alpha <= 0.0f)
            return;
        // the ring metrics come from the theme, because the ring is a sizing decision like any plate (§6.1)
        const ControlMetrics& metrics = m_theme->metrics();
        // the ring surrounds the rect the control names, which a sub rect control overrides (§8.4)
        const Rect ringed = focus_ring_rect();
        // the ring stands off the plate, because a ring on the edge reads as a border rather than as focus (§6.1)
        const Rect outer = Rect::from_origin_size(Point{.x = ringed.x - metrics.focus_ring_offset, .y = ringed.y - metrics.focus_ring_offset}, Size{.width = ringed.width + metrics.focus_ring_offset * 2.0f, .height = ringed.height + metrics.focus_ring_offset * 2.0f});
        // the halo resolves by state too, so the soft layer follows the ring it underlies (§8.4)
        const Color halo = resolve(m_theme->palette().focus_ring_outer, Color::rgba(0.0f, 0.0f, 0.0f, 0.0f));
        // the halo rides one step further out than the ring, which is what a soft second layer reads as (§8.4)
        const Rect halo_rect = Rect::from_origin_size(Point{.x = ringed.x - metrics.focus_ring_offset - 1.0f, .y = ringed.y - metrics.focus_ring_offset - 1.0f}, Size{.width = ringed.width + (metrics.focus_ring_offset + 1.0f) * 2.0f, .height = ringed.height + (metrics.focus_ring_offset + 1.0f) * 2.0f});
        // the halo is stroked rather than filled, like the ring it underlies (§8.4)
        if (halo.alpha > 0.0f)
            context.draw_rounded_rect(halo_rect, metrics.focus_ring_radius + 1.0f, {.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = halo, .stroke_width = metrics.focus_ring_width});
        // the ring is stroked rather than filled, because a filled ring would cover the plate it surrounds (§6.1)
        context.draw_rounded_rect(outer, metrics.focus_ring_radius, {.fill = Color::rgba(0.0f, 0.0f, 0.0f, 0.0f), .stroke = ring, .stroke_width = metrics.focus_ring_width});
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
        // the keyboard focus outranks the resting paint, because a ring is what says the control takes keys (§8.3)
        if (m_focused)
            return ControlState::Focused;
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

    bool Widget::focused() const {
        // the flag is what the focus manager set, and the ring and the tests read it (§8.3)
        return m_focused;
    }

    void Widget::set_focused(bool focused) {
        // an unchanged focus changes nothing, because the ring is already in that state (§8.3)
        if (m_focused == focused)
            return;
        // the state is stored first, because the ring resolves through it (§8.3)
        m_focused = focused;
        // the ring appears and disappears with the focus, so the frame must be redrawn (§6.1)
        invalidate();
    }

    void Widget::set_command(Command* command) {
        // a widget bound to a different command releases that one first, so it stops receiving (§18.1)
        if (m_command != nullptr && m_command_observer != 0)
            (void)m_command->unbind(m_command_observer);
        // the binding handle is cleared with the command, so a later unbind finds nothing to remove (§18.1)
        m_command_observer = 0;
        // the new command replaces the old one, which the widget reads from here on (§18.1)
        m_command = command;
        // a widget with no command owns its own state again, which is what unbinding restores (§18.7)
        if (m_command == nullptr)
            return;
        // the widget subscribes, and binding hands it the current state at once so it never starts stale (§18.1)
        m_command_observer = m_command->bind_static(&Widget::command_observer, this);
    }

    Command* Widget::command() const { return m_command; }

    void Widget::set_visible(bool visible) {
        // a bound command owns applicability, so an imperative write does not apply while it is bound (§18.7)
        if (m_command != nullptr)
            return;
        // an unbound widget hides itself, which is what the base node does (§6.1)
        UiNode::set_visible(visible);
    }

    void Widget::apply_command_state(const CommandState& state) {
        // the base widget presents no label and no disabled state of its own, so it only follows applicability (§18.7)
        UiNode::set_visible(state.visible);
    }

    void Widget::command_observer(void* context, const CommandState& state) {
        // the context is the widget that bound, so the publication reaches exactly the consumer that asked (§18.1)
        auto* self = static_cast<Widget*>(context);
        // the control shapes the state to what it can present, which is where the two published states land (§18.7)
        self->apply_command_state(state);
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
