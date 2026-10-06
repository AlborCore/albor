#pragma once

#include "prototypes/a1_parity/parity.h"

namespace albor::a1_parity
{

    // build an offscreen skia renderer, either the deterministic cpu raster backend or ganesh on metal
    // the returned pointer owns a context and is deleted through the interface
    [[nodiscard]] OffscreenRenderer* make_skia_renderer(Engine engine);

} // namespace albor::a1_parity
