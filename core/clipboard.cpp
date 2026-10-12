#include "core/clipboard.h"

#include <utility>

namespace albor
{
    namespace
    {
        // the installed state, which a null pointer leaves empty without running any initializer (§3.5)
        ClipboardFactoryState* g_state = nullptr;

        // hand out the state, building it the first time it is asked for, which is the only writer (§3.5)
        [[nodiscard]] ClipboardFactoryState& state() {
            // the first caller creates the state, and every later one reads the same object (§3.5)
            if (g_state == nullptr)
                g_state = new ClipboardFactoryState();
            return *g_state;
        }
    } // namespace

    void set_clipboard_factory(ClipboardFactory factory) {
        // the factory replaces the previous one, because the framework copies through exactly one clipboard (§3.5)
        state().factory = std::move(factory);
    }

    const ClipboardFactory& clipboard_factory() {
        // a never installed factory is the empty one, reported rather than crashed on (§3.5)
        static const ClipboardFactory empty;
        return g_state ? g_state->factory : empty;
    }

    std::unique_ptr<Clipboard> create_clipboard() {
        // without an installed clipboard the framework has no way to copy anything (§3.5)
        const ClipboardFactory& factory = clipboard_factory();
        if (!factory)
            return nullptr;
        return factory();
    }
} // namespace albor
