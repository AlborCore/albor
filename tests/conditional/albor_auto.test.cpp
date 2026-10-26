#include <memory>

#include <gtest/gtest.h>

#include "render/renderer_backend.h"

// the load time install mark the albor archive carries, which only a whole archive link brings in (§6.3, AD-26)
extern "C" int albor_renderer_marker;

using namespace albor;

TEST(AlborAutoInstallChecks, TheRendererBackendInstallsItselfAtLoad) {
    // assert the marker is the in-repo one, which proves the whole archive link ran the installer (§6.3, AD-26)
    EXPECT_EQ(albor_renderer_marker, 4);
    // act no test installs a factory here, so only the load time installer can have set one (§6.3)
    const std::unique_ptr<RendererBackend> backend = create_renderer_backend();
    // assert
    ASSERT_NE(backend, nullptr);
    // assert the installed backend is ready, because a host without a device cannot verify the install it proves (§6.2)
    ASSERT_TRUE(backend->ready()) << "no render backend device on this host";
}
