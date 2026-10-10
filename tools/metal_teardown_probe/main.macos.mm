#import <Metal/Metal.h>

#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlBackendContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlDirectContext.h>

#include <cstdio>
#include <cstring>

// the smallest program that reproduces the metal teardown defect with no albor code; DEFECT-METAL-BACKEND-TEARDOWN.md
namespace
{
    id<MTLDevice> pick_device(const char* wanted) {
        // a name is matched so the same repro can run against every gpu of a multi gpu host (§6.3)
        for (id<MTLDevice> device in MTLCopyAllDevices())
            if (wanted != nullptr && std::strcmp(device.name.UTF8String, wanted) == 0)
                return device;
        return MTLCreateSystemDefaultDevice();
    }
} // namespace

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "base";
    const char* wanted = argc > 2 ? argv[2] : nullptr;

    id<MTLDevice> device = pick_device(wanted);
    // a host with no metal device cannot run the repro, which is a different answer than a trap (§6.3)
    if (device == nil) {
        std::fprintf(stderr, "no metal device\n");
        return 2;
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];

    // skia retains both through sk_cfp, so ganesh is a second owner and not a borrower (§6.3)
    GrMtlBackendContext backend;
    backend.fDevice = sk_cfp<GrMTLHandle>((__bridge const void*)device);
    backend.fQueue = sk_cfp<GrMTLHandle>((__bridge const void*)queue);
    sk_sp<GrDirectContext> context = GrDirectContexts::MakeMetal(backend);

    std::fprintf(stderr, "device=%s mode=%s context=%p\n", device.name.UTF8String, mode, (const void*)context.get());
    if (context == nullptr)
        return 3;

    // each mode is one teardown sequence, so the exit code says which the driver tolerates (§6.3)
    if (std::strcmp(mode, "abandon") == 0)
        context->abandonContext();
    if (std::strcmp(mode, "release") == 0)
        context->releaseResourcesAndAbandonContext();
    if (std::strcmp(mode, "finish") == 0)
        context->flushAndSubmit(GrSyncCpu::kYes);
    if (std::strcmp(mode, "finish-then-delete") == 0)
        context->flushAndSubmit(GrSyncCpu::kYes), context.reset();
    if (std::strcmp(mode, "hold-queue") == 0)
        // the one sequence that survives: a reference the process never drops, so the queue never deallocates (§6.3)
        (void)CFRetain((__bridge CFTypeRef)queue);
    if (std::strcmp(mode, "leak-context") == 0)
        (void)context.release();

    return 0;
}
