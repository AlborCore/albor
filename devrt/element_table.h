#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace albor
{
    class BindingEngine;
    class TextEngine;
    class UiNode;
} // namespace albor

namespace albor::devrt
{
    // what one property assignment does to a node, which reports whether the value was one the property holds (§12.1)
    using PropertySetter = std::function<bool(UiNode& node, std::string_view value)>;

    // what one element name builds over a window's engines, which is what a reload creates its nodes with (§12.1)
    using NodeFactory = std::function<std::unique_ptr<UiNode>(std::string_view element, BindingEngine& engine, TextEngine& text)>;

    // build the node an element name names, returning null when the name is not part of the closed builtin set (§8)
    [[nodiscard]] std::unique_ptr<UiNode> create_node(std::string_view element, BindingEngine& engine, TextEngine& text);

    // report whether the closed builtin set has an element by that name (§8)
    [[nodiscard]] bool known_element(std::string_view element);

    // apply one property assignment to a node, reporting whether it landed, because a name and a value are refused (§12.1)
    bool apply_property(UiNode& node, std::string_view element, std::string_view property, std::string_view value);

    // name the element a live node was built from, which is empty for a node the closed set does not describe (§12.1)
    [[nodiscard]] std::string_view element_of(const UiNode& node);
} // namespace albor::devrt
