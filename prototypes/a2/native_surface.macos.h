#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace albor::a2
{

    // the native presentation surface of a window, defined only for the objective c++ platform files
    // the public interface keeps it opaque so that no platform type reaches a header (§3.5, AD-05)
    struct NativeSurface
    {
        CAMetalLayer* layer = nil;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
    };

} // namespace albor::a2
