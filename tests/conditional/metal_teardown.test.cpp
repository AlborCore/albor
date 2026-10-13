#include <memory>

#include <gtest/gtest.h>

#include "render/render-skia/skia_renderer_backend.h"

using namespace albor;

TEST(SkiaRendererBackendLifecycle, ConstructingAndDestroyingABackendReturns) {
    // act the backend is constructed and destroyed in a scope, which is what an application does (§6.3)
    {
        SkiaRendererBackend backend;
        // assert it came up, because a backend that did not is a different failure than the one guarded here (§6.3)
        EXPECT_TRUE(backend.ready());
    }
    // assert reaching this line is the claim: the driver traps in the queue dealloc unless the queue is held (§6.3)
    SUCCEED();
}

TEST(SkiaRendererBackendLifecycle, DestroyingABackendAfterItRecordedNothingStillReturns) {
    // arrange a second backend in the same process, because the first test already exercised one queue (§6.3)
    std::unique_ptr<SkiaRendererBackend> backend = std::make_unique<SkiaRendererBackend>();
    // assert it came up, so the destruction below is not skipped by a null (§6.3)
    ASSERT_TRUE(backend->ready());
    // act
    backend.reset();
    // assert returning at all is the claim, because the teardown order is what the driver rejects (§6.3)
    SUCCEED();
}
