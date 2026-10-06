#pragma once

#include <memory>
#include <string_view>

#include "text/text_engine.h"

namespace albor
{
    // the skia text engine, which shapes and measures through skparagraph and is the phase 3 backend of §9.1
    class SkiaTextEngine final : public TextEngine
    {
    public:
        // build the engine and resolve its font collection, which is what makes it usable (§9.1)
        SkiaTextEngine();

        ~SkiaTextEngine() override;

        SkiaTextEngine(const SkiaTextEngine&) = delete;
        SkiaTextEngine& operator=(const SkiaTextEngine&) = delete;
        SkiaTextEngine(SkiaTextEngine&&) = delete;
        SkiaTextEngine& operator=(SkiaTextEngine&&) = delete;

        // override: measure a run at the default font size (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override;

        // override: measure a run at an explicit font size (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override;

        // override: report whether the collection and the unicode both resolved (§9.1)
        [[nodiscard]] bool ready() const override;

        // install this engine as the one the framework measures with, which a startup calls (§9.1)
        static void install();

    private:
        // the shaping state, which holds the font collection and the unicode behind an incomplete type (§9.1)
        struct State;

        // the owned shaping state, which the destructor needs the complete type for (§9.1)
        std::unique_ptr<State> m_state;
    };
} // namespace albor
