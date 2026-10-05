#pragma once

#include <cstddef>
#include <string>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/render_context.h"
#include "runtime/binding_engine.h"
#include "runtime/property.h"
#include "ui/ui_node.h"

namespace albor
{
    // the base of a composite UI component, which owns component scoped properties and an explicit id (§8.1, AD-11)
    class Component : public UiNode
    {
    public:
        // build a component and register it with the engine, which scopes its property identifiers (§8.1, §7.2)
        explicit Component(BindingEngine* engine, std::string explicit_id = {});

        // a component has no copy, because its properties own graph edges keyed by identity (§7.3)
        ~Component() override;

        // hand out the engine that settles the properties of this component (§7.2)
        [[nodiscard]] BindingEngine* engine() const;

        // hand out the base offset every property of this component shares (§7.2, AD-04b)
        [[nodiscard]] std::uint32_t property_base() const;

        // hand out how many properties the component registered (§7.2)
        [[nodiscard]] std::size_t property_count() const;

        // attach the drawable content of the component, which a concrete component overrides (§6.1)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the component and of its children (§8.2)
        void arrange(const Rect& bounds) override;

        // record the content of the component into the context (§6.1)
        void render(RenderContext& context) override;

        // report whether the point lands on the component content or on a descendant (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // draw the component background, which a concrete component fills in
        virtual void paint_background(RenderContext& context);

        // hand out the fill colour of the component background, which a concrete component overrides
        [[nodiscard]] virtual Color background() const;

        // hand out the size the component measured itself to (§8.2)
        [[nodiscard]] const Size& preferred_size() const;

        // attach the engine and a fresh component scoped identifier to a property member (§7.2, AD-04b)
        template <typename T>
        void register_property(Property<T>& property) {
            // a detached component has no engine, so the identifier stays invalid (§7.2)
            if (m_engine == nullptr)
                return;
            // the engine hands out the identifier, which keeps two components off the same slot (§7.2)
            property.set_id(m_engine->next_property_id());
            // the engine is attached last, because a property cannot bind without one (§7.2)
            property.set_engine(m_engine);
            // increment counter
            m_property_count++;
        }

    protected:
        // the engine that settles the properties of this component, which is null when detached (§7.2)
        BindingEngine* m_engine = nullptr;

        // the size the component prefers, which its parent queries in the measure phase (§8.2)
        Size m_preferred_size;

        // the base offset every property identifier of this component carries (§7.2, AD-04b)
        std::uint32_t m_property_base = 0;

        // how many properties the component registered, which the tests assert on (§7.2)
        std::size_t m_property_count = 0;
    };
} // namespace albor
