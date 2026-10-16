#include <memory>

#include <gtest/gtest.h>

#include "render/renderer_backend.h"

// the load time install mark the blend2d archive carries, which only a whole archive link brings in (§6.3, AD-26)
extern "C" int albor_renderer_marker;

using namespace albor;

TEST(Blend2DAutoInstallChecks, TheRendererBackendInstallsItselfAtLoad) {
    // assert the marker is the blend2d one, which proves the whole archive link ran the installer (§6.3, AD-26)
    EXPECT_EQ(albor_renderer_marker, 3);
    // act no test installs a factory here, so only the load time installer can have set one (§6.3)
    const std::unique_ptr<RendererBackend> backend = create_renderer_backend();
    // assert
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->ready());
}
