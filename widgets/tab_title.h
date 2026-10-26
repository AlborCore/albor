#pragma once

#include <string>

#include "widgets/widget.h"

namespace albor
{
    // the content one tab draws in the strip, which lets a title carry an icon or a composed run (§8)
    class TabTitle final : public Widget
    {
    public:
        // build an empty title over the engine that settles its children (§8)
        explicit TabTitle(BindingEngine* engine, std::string explicit_id = {});

        // override: a title is decorative, so it never takes a pointer of its own (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: a title carries no semantic meaning of its own, because the page label names the tab (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: a title is decorative, so it is not a focus stop (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: a title's children are decorative too, so the focus walk does not enter them (§8.3)
        [[nodiscard]] bool focusable_children() const override;

        // override: a title takes no input, because the tab around it is the target (§8.3)
        void dispatch_event(Event& event) override;
    };
} // namespace albor
