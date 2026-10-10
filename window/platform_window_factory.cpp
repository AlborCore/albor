#include "window/platform_window_factory.h"

namespace albor
{
    namespace
    {
        // the installed state, which a null pointer leaves empty without running any initializer (§5.3)
        PlatformWindowFactoryState* g_state = nullptr;

        // hand out the state, building it the first time it is asked for, which is the only writer (§5.3)
        [[nodiscard]] PlatformWindowFactoryState& state() {
            // the first caller creates the state, and every later one reads the same object (§5.3)
            if (g_state == nullptr)
                g_state = new PlatformWindowFactoryState();
            return *g_state;
        }
    } // namespace

    void set_platform_window_factory(PlatformWindowFactory factory) {
        // the factory replaces the previous one, because a process is served by exactly one backend (§5.3)
        state().factory = std::move(factory);
    }

    const PlatformWindowFactory& platform_window_factory() {
        // a never installed factory is the empty one, reported rather than crashed on (§5.3)
        static const PlatformWindowFactory empty;
        return g_state ? g_state->factory : empty;
    }
} // namespace albor
