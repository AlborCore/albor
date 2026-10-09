#include "ui/component.h"

namespace albor
{
    Component::Component(BindingEngine* engine, std::string explicit_id) :
        m_engine(engine) {
        // a detached component has no engine, so its properties never receive an identifier (§7.2)
        if (m_engine != nullptr)
            m_property_base = m_engine->begin_component();
        // the author assigned id is stored as written, because it is the authoritative identity (AD-11)
        set_explicit_id(std::move(explicit_id));
    }

    Component::~Component() = default;

    BindingEngine* Component::engine() const {
        // the engine settles the properties of this component (§7.2)
        return m_engine;
    }

    std::uint32_t Component::property_base() const {
        // the base offset is what every property identifier of this component carries (§7.2, AD-04b)
        return m_property_base;
    }

    std::size_t Component::property_count() const {
        // the count is what a component registers, which a test asserts on (§7.2)
        return m_property_count;
    }

    void Component::measure(const SizeConstraints& constraints, Size& out_size) {
        // the component measures as the largest of its children, which the node contract already implements (§8.2)
        UiNode::measure(constraints, out_size);
        // the preferred size is kept so a parent can query it without a second measure (§8.2)
        m_preferred_size = out_size;
    }

    void Component::arrange(const Rect& bounds) {
        // the component passes the same bounds down, which keeps a leaf component simple (§8.2)
        UiNode::arrange(bounds);
        // the arranged extent becomes the preferred one, because the parent has now decided (§8.2)
        m_preferred_size = Size{.width = bounds.width, .height = bounds.height};
    }

    void Component::render(RenderContext& context) {
        // the background is drawn first, so the content lands on top of it (§6.1)
        paint_background(context);
        // the content is drawn over the background, because that is the order a composite paints in (§6.1)
        paint_content(context);
        // the children draw over both, in insertion order, which is what a composite tree needs (§6.1)
        UiNode::render(context);
    }

    void Component::paint_content(RenderContext& context) {
        // a component with no content of its own still draws its background and its children (§6.1)
        (void)context;
    }

    bool Component::hit_test(Point logical_point) const {
        // the component body itself is a hit only where it has area (§8.3)
        if (visible() && !bounds().empty() && logical_point.x >= bounds().x && logical_point.x < bounds().x + bounds().width && logical_point.y >= bounds().y && logical_point.y < bounds().y + bounds().height)
            return true;
        // a child under the point is also a hit, which is what a composite component needs (§8.3)
        return UiNode::hit_test(logical_point);
    }

    void Component::paint_background(RenderContext& context) {
        // a component with no area draws nothing, which keeps an unarranged component invisible (§6.1)
        if (bounds().empty())
            return;
        // the fill is the only paint a background needs, so the stroke stays empty (§6.2)
        const Color fill = background();
        // a radius means a rounded plate, which is what a themed control draws (§6.2)
        const float radius = corner_radius();
        if (radius > 0.0f) {
            // the plate is rounded, so the corners take the radius rather than the fill (§6.2)
            context.draw_rounded_rect(bounds(), radius, {.fill = fill});
            return;
        }
        // without a radius the background is a hard rectangle, which is what an unthemed component drew (§6.2)
        context.draw_rect(bounds(), {.fill = fill});
    }

    Color Component::background() const {
        // the default component is transparent, so a derived component decides whether it paints (§6.2)
        return Color::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float Component::corner_radius() const {
        // the default component is a hard rectangle, because nothing asked for a radius (§6.2)
        return 0.0f;
    }

    const Size& Component::preferred_size() const {
        // the preferred size is the last measure result, which a parent queries (§8.2)
        return m_preferred_size;
    }
} // namespace albor
