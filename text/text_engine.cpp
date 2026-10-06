#include "text/text_engine.h"

namespace albor
{
    namespace
    {
        // the installed engine factory, which is empty until a backend installs one (§9.1)
        TextEngineFactory g_engine_factory;
    } // namespace

    void set_text_engine_factory(TextEngineFactory factory) {
        // the factory replaces the previous one, because the framework measures through exactly one engine (§9.1)
        g_engine_factory = std::move(factory);
    }

    const TextEngineFactory& text_engine_factory() {
        // an uninstalled backend is reported as an empty factory rather than a crash (§9.1)
        return g_engine_factory;
    }

    std::unique_ptr<TextEngine> create_text_engine() {
        // without an installed engine the framework has no way to measure text (§9.1)
        if (!g_engine_factory)
            return nullptr;
        // create instance
        return g_engine_factory();
    }
} // namespace albor
