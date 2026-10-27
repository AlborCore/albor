#include "compiler/static_expression.h"

namespace albor::compiler
{
    const ImageExpression* static_image(const Expression& expression) {
        // an image is the value itself (§6.4)
        if (const auto* image = dynamic_cast<const ImageExpression*>(&expression))
            return image;
        // a grouping is transparent, because the parentheses do not change the value (§11.1)
        if (const auto* grouping = dynamic_cast<const GroupingExpression*>(&expression))
            return static_image(*grouping->inner);
        // any other expression is a binding or a computation, which the static form cannot carry (§12.1)
        return nullptr;
    }
} // namespace albor::compiler
