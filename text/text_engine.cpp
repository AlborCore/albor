#include "text/text_engine.h"

#include <utility>

namespace albor
{
    namespace
    {
        // the installed state, which a null pointer leaves empty without running any initializer (§9.1)
        TextEngineFactoryState* g_state = nullptr;

        // hand out the state, building it the first time it is asked for, which is the only writer (§9.1)
        [[nodiscard]] TextEngineFactoryState& state() {
            // the first caller creates the state, and every later one reads the same object (§9.1)
            if (g_state == nullptr)
                g_state = new TextEngineFactoryState();
            return *g_state;
        }
    } // namespace

    void set_text_engine_factory(TextEngineFactory factory) {
        // the factory replaces the previous one, because the framework measures through exactly one engine (§9.1)
        state().factory = std::move(factory);
    }

    const TextEngineFactory& text_engine_factory() {
        // a never installed factory is the empty one, reported rather than crashed on (§9.1)
        static const TextEngineFactory empty;
        return g_state ? g_state->factory : empty;
    }

    std::unique_ptr<TextEngine> create_text_engine() {
        // without an installed engine the framework has no way to measure text (§9.1)
        const TextEngineFactory& factory = text_engine_factory();
        if (!factory)
            return nullptr;
        return factory();
    }

    void set_installed_font_family(std::string family) {
        // the family replaces the previous one, because the framework measures through one engine (§9.1)
        state().font_family = std::move(family);
    }

    std::string installed_font_family() {
        // a family never named and a state never built both read as the default, which codegen registers under (#43)
        if (g_state == nullptr || g_state->font_family.empty())
            return std::string(TextEngine::DEFAULT_FONT_FAMILY);
        // the named family is what a factory builds the engine over (§9.1)
        return g_state->font_family;
    }
} // namespace albor
