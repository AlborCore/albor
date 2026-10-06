#pragma once

#include <functional>

#include "widgets/widget.h"

namespace albor
{
    // the widget that hands its drawing context to a callback, which is how custom content is drawn (§6.4, §8)
    class Canvas final : public Widget
    {
    public:
        // build an empty canvas over the engine that settles its properties (§8)
        explicit Canvas(BindingEngine* engine, std::string explicit_id = {});

        // measure the extent the caller declares, because a canvas draws whatever the callback draws (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the canvas, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record whatever the callback draws, which is the whole point of a canvas (§6.1, §6.4)
        void render(RenderContext& context) override;

        // override: announce the canvas as a drawing surface an adapter may interact with (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the work that draws into the context, which redraws because the content changed (§6.4)
        void set_draw(std::function<void(RenderContext&)> draw);

        // report whether a callback is installed, because an empty canvas draws nothing (§6.4)
        [[nodiscard]] bool draws() const;

        // replace the declared extent, which remeasures because the canvas size changed (§8.2)
        void set_intrinsic_size(Size size);

        // hand out the declared extent, which is what the measure pass uses (§8.2)
        [[nodiscard]] Size intrinsic_size() const;

        // hand out how many times the canvas recorded into a context, which the tests assert on (§6.4)
        [[nodiscard]] std::uint64_t draw_count() const;

    private:
        // the work that draws into the context (§6.4)
        std::function<void(RenderContext&)> m_draw;

        // the extent the canvas declares, because a canvas has no intrinsic content to measure (§8.2)
        Size m_intrinsic_size{0.0f, 0.0f};

        // how many times the canvas recorded into a context, which the tests assert on (§6.4)
        std::uint64_t m_draw_count = 0;
    };
} // namespace albor
