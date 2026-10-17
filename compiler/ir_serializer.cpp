#include "compiler/ir_serializer.h"

#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace albor::compiler
{
    namespace
    {
        // parse one hex channel pair, reporting a pair that is not two hex digits (§11.1)
        std::optional<int> hex_channel(std::string_view pair) {
            // a channel is exactly two digits, because the literal is #RRGGBB (§11.1)
            if (pair.size() != 2)
                return std::nullopt;
            // the value is read in base sixteen, which is how a colour literal spells a channel (§11.1)
            int value = 0;
            // the parse must consume the whole pair, because a partial channel is not a colour (§11.1)
            const std::from_chars_result parsed = std::from_chars(pair.data(), pair.data() + pair.size(), value, 16);
            // a pair that did not parse has no channel (§11.1)
            if (parsed.ec != std::errc{} || parsed.ptr != pair.data() + pair.size())
                return std::nullopt;
            // the channel is the byte the literal wrote (§11.1)
            return value;
        }

        // turn an opaque colour literal into the comma separated channels the dev runtime reads (§12.1)
        std::optional<std::string> color_value(std::string_view literal) {
            // the hash is dropped, because the channels are what the reader parses (§11.1)
            if (!literal.empty() && literal.front() == '#')
                literal.remove_prefix(1);
            // the reader takes three channels and no alpha, so a translucent literal is refused (§12.1)
            if (literal.size() != 6)
                return std::nullopt;
            // each channel is read in order, because the position is the channel (§11.1)
            const std::optional<int> red = hex_channel(literal.substr(0, 2));
            // the green channel follows the red one (§11.1)
            const std::optional<int> green = hex_channel(literal.substr(2, 2));
            // the blue channel closes the triple (§11.1)
            const std::optional<int> blue = hex_channel(literal.substr(4, 2));
            // a literal with a bad channel is not a colour, which the caller drops (§11.1)
            if (!red || !green || !blue)
                return std::nullopt;
            // the channels are written in the unit range the reader expects, which is what a colour carries (§6.3)
            char buffer[64];
            // the three channels are formatted as the comma separated triple the reader parses (§12.1)
            std::snprintf(buffer, sizeof buffer, "%g, %g, %g", static_cast<float>(*red) / 255.0f, static_cast<float>(*green) / 255.0f, static_cast<float>(*blue) / 255.0f);
            // the formatted triple is the value the definition carries (§12.1)
            return std::string(buffer);
        }

        // turn one literal into the spelling the dev runtime's setters read, applying a written negation (§12.1)
        std::optional<std::string> literal_value(const LiteralExpression& literal, bool negative) {
            switch (literal.kind) {
            case LiteralExpression::Kind::Integer:
            case LiteralExpression::Kind::Float:
                // a number is read as written, and a written negation prefixes it (§12.1)
                return negative ? "-" + literal.text : literal.text;
            case LiteralExpression::Kind::Boolean:
                // a truth word is read as written, and a negation is not a value the reader takes (§12.1)
                return negative ? std::nullopt : std::optional<std::string>(literal.text);
            case LiteralExpression::Kind::String:
                // a decoded string cannot carry a newline through a line oriented format, so it is refused (§12.1)
                if (negative || literal.text.find('\n') != std::string::npos)
                    return std::nullopt;
                // the decoded text is the value the definition carries (§12.1)
                return literal.text;
            case LiteralExpression::Kind::Length: {
                // the unit is dropped, because the reader takes the logical number (§12.1, AD-27)
                std::string text = literal.text;
                // a physical length has no build time value, so it is refused rather than applied wrong (§12.1, AD-27)
                if (text.size() < 2 || text.compare(text.size() - 2, 2, "px") != 0)
                    return std::nullopt;
                // the two character unit is removed, which is what the reader parses (§12.1, AD-27)
                text.erase(text.size() - 2);
                // the logical number carries the written negation, because the reader parses a signed number (§12.1)
                return negative ? "-" + text : text;
            }
            case LiteralExpression::Kind::Color:
                // a colour has no negation, so a written one is not a value the reader takes (§12.1)
                return negative ? std::nullopt : color_value(literal.text);
            }
            // a kind the serializer does not know carries no value (§12.1)
            return std::nullopt;
        }

        // find the literal a value is written as, unwrapping a grouping and a numeric negation (§11.1)
        const LiteralExpression* find_literal(const Expression& expression, bool& negative) {
            // a literal is the value itself (§11.1)
            if (const auto* literal = dynamic_cast<const LiteralExpression*>(&expression))
                return literal;
            // a grouping is transparent, because the parentheses do not change the value (§11.1)
            if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
                return find_literal(*grouping->inner, negative);
            // a numeric negation is a written sign, which the reader parses as part of the number (§11.1)
            if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression); unary != nullptr && unary->op == UnaryOperator::Negate && unary->value != nullptr) {
                // the operand carries the value, and the sign is applied to it (§11.1)
                negative = true;
                return find_literal(*unary->value, negative);
            }
            // any other expression is a binding or a computation the IR cannot carry (§12.1)
            return nullptr;
        }

        // report whether every node of a tree is an element the IR can represent (§12.1)
        bool representable(const TreeNode& node) {
            // a repeat, a condition or a slot has no IR form, so a tree carrying one cannot be serialized (§12.1)
            if (node.kind != TreeNodeKind::Element || node.element_type.empty())
                return false;
            // every child must be representable too, because a partial tree must not be applied (§12.1)
            for (const std::unique_ptr<TreeNode>& child : node.children) {
                // a single unrepresentable child refuses the whole component (§12.1)
                if (!representable(*child))
                    return false;
            }
            // the node and its subtree are representable (§12.1)
            return true;
        }

        // serialize one element into the IR (§12.1)
        devrt::IrNode to_ir(const TreeNode& node) {
            // the node carries the element name, which is what the factory builds (§12.1)
            devrt::IrNode ir;
            // the element name is what the runtime builds (§12.1)
            ir.element = node.element_type;
            // the author assigned id is the identity a reload matches on (§12.1, AD-11)
            ir.id = node.explicit_id;
            // every static assignment becomes a property, and a binding is dropped, which the IR cannot carry (§12.1)
            for (const PropertyAssignment& assignment : node.assignments) {
                // the sign and the literal are read together, because a written negation is part of the value (§11.1)
                bool negative = false;
                // the literal is the value, when the assignment is a static one (§12.1)
                const LiteralExpression* literal = find_literal(*assignment.value, negative);
                // a binding or a computation is not representable in the IR (§12.1)
                if (literal == nullptr)
                    continue;
                // the literal is converted to the spelling the reader parses (§12.1)
                std::optional<std::string> value = literal_value(*literal, negative);
                // a value the reader cannot parse is dropped rather than written wrong (§12.1)
                if (!value)
                    continue;
                // the property joins the node in written order (§12.1)
                ir.properties.push_back(devrt::IrProperty{.name = assignment.name, .value = std::move(*value)});
            }
            // every child joins the node in written order, and each is an element because the tree was checked (§12.1)
            for (const std::unique_ptr<TreeNode>& child : node.children)
                ir.children.push_back(to_ir(*child));
            // the node is the serialized element (§12.1)
            return ir;
        }
    } // namespace

    std::optional<devrt::IrNode> serialize_component(const ComponentDeclaration& component) {
        // a component with no root element carries no tree, so it serializes to nothing (§11.1)
        if (component.root == nullptr)
            return std::nullopt;
        // a tree with a repeat, a condition or a slot cannot be half represented, so the component is refused (§12.1)
        if (!representable(*component.root))
            return std::nullopt;
        // the root is serialized through the same element walk as every child (§12.1)
        return to_ir(*component.root);
    }
} // namespace albor::compiler
