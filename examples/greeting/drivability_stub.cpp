#include "drivability.h"

namespace albor::example
{
    // the implementation a release never builds any of, which exists only so the header slot can be destroyed (§2.3)
    class DrivableImpl
    {
    };

    // the drivability surface with nothing behind it, which is what a release links (§2.3, AD-04, AD-10)

    Drivable::Drivable(UiNode& root, const void* native_window) {
        // the tree is left empty, because a release publishes nothing (§10.3, AD-10)
        (void)root;
        // the native window is named but unused, because a release attaches no bridge (§2.3, AD-10)
        (void)native_window;
    }

    Drivable::~Drivable() = default;

    void Drivable::refresh() {
        // there is nothing to publish and no client to serve, which is the whole of a release (§10.3, AD-10)
    }

    const std::string& Drivable::endpoint() const {
        // no endpoint exists in a release, which the empty path states (§10.3, AD-10)
        static const std::string none;
        // the empty path is returned, because nothing is listening (§10.3)
        return none;
    }
} // namespace albor::example
