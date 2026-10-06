#pragma once

#include "prototypes/a1_parity/parity.h"

namespace albor::a1_parity
{

    // build the dawn offscreen renderer, which allocates its own device and target and needs no window
    // the returned pointer owns a context and is deleted through the interface
    [[nodiscard]] OffscreenRenderer* make_dawn_renderer();

} // namespace albor::a1_parity
