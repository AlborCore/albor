#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "text/text_engine.h"

namespace albor
{
    // the platform services the constructor accepts, declared here so the header needs no skia type (§9.1)
    struct TextServices;

    // the skia text engine, which shapes and measures through skparagraph and is the phase 3 backend of §9.1
    class SkiaTextEngine final : public TextEngine
    {
    public:
        // build the engine over the platform services, which is what an application uses (§9.1)
        SkiaTextEngine();

        // build the engine over services the caller supplies, which is how a test reaches the state an engine is in when the platform did not hand it a usable unicode (§9.1)
        explicit SkiaTextEngine(TextServices services);

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

        // how many shaping passes the engine has run (§9.1)
        [[nodiscard]] std::size_t shaped_run_count() const;

        // how many shaped runs the cache currently holds (§9.1)
        [[nodiscard]] std::size_t cache_size() const;

        // drop every cached run, which a memory pressure policy calls (§9.1)
        void clear_cache() const;

        // install this engine as the one the framework measures with, which a startup calls (§9.1)
        static void install();

    private:
        // the shaping state, which holds the font collection and the unicode behind an incomplete type (§9.1)
        struct State;

        // the owned shaping state, which holds the services as well and needs the complete type to destruct (§9.1)
        std::unique_ptr<State> m_state;
    };
} // namespace albor
