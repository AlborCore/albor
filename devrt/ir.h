#pragma once

#include <optional>
#include <string>
#include <vector>

namespace albor::devrt
{
    // one property the ir assigns, which a reload may apply (§12.1)
    struct IrProperty
    {
        // the property name as written, which is what a widget surface is looked up by (§12.1)
        std::string name;

        // the value as written, which a setter takes as text (§12.1)
        std::string value;
    };

    // one node of a serialized component definition, which a reload reconciles against (§12.1)
    struct IrNode
    {
        // the element name, which is what the factory builds (§12.1)
        std::string element;

        // the author assigned id, which is empty when the author named nothing (§12.1, AD-11)
        std::string id;

        // the properties the definition assigns (§12.1)
        std::vector<IrProperty> properties;

        // the children in order, which is what a diff walks (§12.1)
        std::vector<IrNode> children;
    };

    // read a serialized definition, which is line oriented because only this extension ever reads it (§12.1, AD-02)
    [[nodiscard]] std::optional<IrNode> parse_ir(const std::string& text);

    // write a component definition back into the format, which is what a compiler driver emits (§12.1)
    [[nodiscard]] std::string write_ir(const IrNode& root);
} // namespace albor::devrt
