#pragma once

#include "compiler/ast.h"

namespace albor::compiler
{
    // find the embedded image a value is written as, unwrapping a transparent grouping (§6.4, §11.1)
    [[nodiscard]] const ImageExpression* static_image(const Expression& expression);
} // namespace albor::compiler
