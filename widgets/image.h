#pragma once

#include <memory>
#include <string>

#include "widgets/widget.h"

namespace albor
{
    // the decoded pixels an image widget draws, declared ahead because the header carries only the handle (§6.4)
    class ImageResource;

    // the widget that draws a retained image into its arranged bounds (§6.4, §8)
    class Image final : public Widget
    {
    public:
        // build an empty image over the engine that settles its properties (§8)
        explicit Image(BindingEngine* engine, std::string explicit_id = {});

        // measure the image extent, or the explicit size the author declared (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the image, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the source into the bounds, which is what the widget shows (§6.4)
        void paint_content(RenderContext& context) override;

        // override: a static image is not a keyboard stop, because it performs no action (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: announce the image with its description and bounds (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the retained pixels, which remeasures when the new image has another extent (§6.4)
        void set_source(std::shared_ptr<const ImageResource> source);

        // hand out the retained pixels the paint records (§6.4)
        [[nodiscard]] const std::shared_ptr<const ImageResource>& source() const;

        // replace the accessible description, which names the image for assistive technology (§10.1)
        void set_description(std::string description);

        // hand out the accessible description (§10.1)
        [[nodiscard]] const std::string& description() const;

        // replace the explicit width, where zero asks for the natural pixel width (§8.2)
        void set_width(float width);

        // hand out the explicit width, where zero means the natural width (§8.2)
        [[nodiscard]] float width() const;

        // replace the explicit height, where zero asks for the natural pixel height (§8.2)
        void set_height(float height);

        // hand out the explicit height, where zero means the natural height (§8.2)
        [[nodiscard]] float height() const;

    private:
        // hand out the extent the source and the explicit dimensions resolve to (§8.2)
        [[nodiscard]] Size natural_size() const;

        // the retained image the paint records, which is empty until a source is assigned (§6.4)
        std::shared_ptr<const ImageResource> m_source;

        // the accessible description, which falls back to the author identity when it is empty (§10.1)
        std::string m_description;

        // the explicit width, where zero keeps the natural pixel width (§8.2)
        float m_width = 0.0f;

        // the explicit height, where zero keeps the natural pixel height (§8.2)
        float m_height = 0.0f;
    };
} // namespace albor
