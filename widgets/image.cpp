#include "widgets/image.h"

#include <algorithm>
#include <utility>

#include "render/image_resource.h"
#include "render/render_context.h"

namespace albor
{
    Image::Image(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    void Image::measure(const SizeConstraints& constraints, Size& out_size) {
        // the source extent, or the explicit dimensions, is what an image offers (§8.2)
        Size content = natural_size();
        // the children are measured too, because a widget is a component and they are part of its content (§8.1, §8.2)
        Size children{};
        UiNode::measure(constraints, children);
        // a widget is at least as large as its own content and as its children (§8.2)
        content.width = std::max(content.width, children.width);
        content.height = std::max(content.height, children.height);
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(content);
        // the preferred size follows the measured one, so a parent can query it without a second measure (§8.2)
        m_preferred_size = m_measured_size;
        out_size = m_measured_size;
    }

    void Image::arrange(const Rect& bounds) {
        // the component arranges its own bounds and its children, because a widget is a component (§8.2)
        Component::arrange(bounds);
    }

    void Image::paint_content(RenderContext& context) {
        // an image with no pixels or no area draws nothing, because there is nothing to map (§6.4)
        if (!m_source || m_source->pixels().empty() || bounds().empty())
            return;
        // white leaves the sampled texel unchanged, while the alpha still modulates it (§6.4)
        context.draw_image(m_source, bounds(), Paint{.fill = Color::rgb(1.0f, 1.0f, 1.0f)});
    }

    bool Image::accepts_focus() const {
        // a static image performs no action, so it is not a keyboard stop (§8.3)
        return false;
    }

    SemanticNode Image::semantic_node() const {
        // the role is an image, because the user reads a picture rather than edits one (§10.1)
        SemanticNode node{SemanticRole::Image};
        // the author assigned id travels with the node, because automation selects by it and by nothing else (AD-11)
        node.set_explicit_id(identity().explicit_id);
        // the description names the image, while an empty description falls back to the author identity (§10.1)
        node.set_name(m_description.empty() ? identity().explicit_id : m_description);
        // the bounds travel with the node, because an adapter places the announcement where it was drawn (§10.1)
        node.set_bounds(bounds());
        // an image is not checkable, so the check state stays not checkable (§10.1)
        node.set_check(CheckState::NotCheckable);
        // an image with no pixels is announced as disabled, because there is nothing to describe (§10.1)
        node.set_disabled(!m_source || m_source->pixels().empty());
        return node;
    }

    void Image::set_source(std::shared_ptr<const ImageResource> source) {
        // an unchanged handle redraws nothing, because the picture is the same (§6.4)
        if (m_source == source)
            return;
        // the old extent is read first, because only a different one changes the layout (§6.4, §8.2)
        const Size before = natural_size();
        // the handle is stored first, so the invalidation walks an image that already carries it (§6.4)
        m_source = std::move(source);
        // the new extent is read once, because both axes decide whether a measure pass is needed (§6.4, §8.2)
        const Size after = natural_size();
        // a same extent only changes the pixels, so the next frame redraws without a measure pass (§6.4, §8.2)
        if (after.width == before.width && after.height == before.height) {
            invalidate();
            return;
        }
        // a new extent changes what the image measures, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    const std::shared_ptr<const ImageResource>& Image::source() const {
        // the retained handle is what the paint records (§6.4)
        return m_source;
    }

    void Image::set_description(std::string description) {
        // an unchanged description republishes nothing, because the accessible name is the same (§10.1)
        if (m_description == description)
            return;
        // the description is stored first, so the frame that republishes semantics reads it (§10.1)
        m_description = std::move(description);
        // a new accessible name changes what a reader announces, so a frame is requested (§10.1)
        invalidate();
    }

    const std::string& Image::description() const {
        // the description is the accessible name the semantic node carries (§10.1)
        return m_description;
    }

    void Image::set_width(float width) {
        // a negative width is no size at all, so it resolves to the natural extent like zero (§8.2)
        const float clamped = std::max(0.0f, width);
        // an unchanged width remeasures nothing, because the layout did not change (§8.2)
        if (m_width == clamped)
            return;
        // the width is stored first, so the invalidation walks an image that already carries it (§8.2)
        m_width = clamped;
        // a new width changes what the image measures, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    float Image::width() const {
        // zero means the natural pixel width, which is what measure resolves (§8.2)
        return m_width;
    }

    void Image::set_height(float height) {
        // a negative height is no size at all, so it resolves to the natural extent like zero (§8.2)
        const float clamped = std::max(0.0f, height);
        // an unchanged height remeasures nothing, because the layout did not change (§8.2)
        if (m_height == clamped)
            return;
        // the height is stored first, so the invalidation walks an image that already carries it (§8.2)
        m_height = clamped;
        // a new height changes what the image measures, so the layout passes must run again (§8.2)
        invalidate_layout();
    }

    float Image::height() const {
        // zero means the natural pixel height, which is what measure resolves (§8.2)
        return m_height;
    }

    Size Image::natural_size() const {
        // a source carries its pixel extent, which is the default logical size of the image (§6.4, §8.2)
        const Size source_size{.width = m_source != nullptr ? static_cast<float>(m_source->width()) : 0.0f, .height = m_source != nullptr ? static_cast<float>(m_source->height()) : 0.0f};
        // an explicit dimension replaces the natural one, while zero keeps the source extent (§8.2)
        return Size{.width = m_width > 0.0f ? m_width : source_size.width, .height = m_height > 0.0f ? m_height : source_size.height};
    }
} // namespace albor
