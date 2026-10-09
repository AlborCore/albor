#pragma once

#include <memory>
#include <string>

#include "text/text_engine.h"

namespace albor::harfbuzz
{
    // the text engine that shapes with a vendored harfbuzz instead of a renderer bundled shaper (§9.1)
    class HarfbuzzTextEngine final : public TextEngine
    {
    public:
        // build over a font file, reporting a refusal by returning null because shaping needs a face (§9.1)
        [[nodiscard]] static std::unique_ptr<HarfbuzzTextEngine> create(const std::string& font_path);

        ~HarfbuzzTextEngine() override;

        HarfbuzzTextEngine(const HarfbuzzTextEngine&) = delete;
        HarfbuzzTextEngine& operator=(const HarfbuzzTextEngine&) = delete;
        HarfbuzzTextEngine(HarfbuzzTextEngine&&) = delete;
        HarfbuzzTextEngine& operator=(HarfbuzzTextEngine&&) = delete;

        // override: measure at the default font size (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width) const override;

        // override: measure at an explicit font size (§9.1)
        [[nodiscard]] TextLayout measure(std::string_view text, float max_width, float font_size) const override;

        // override: report whether a face was loaded, because an engine with no face shapes nothing (§9.1)
        [[nodiscard]] bool ready() const override;

        // report whether a run from this engine can be drawn, which is false until a typeface can be issued (§9.1)
        [[nodiscard]] bool drawable() const;

    private:
        // load the face and the shaper over it, which is the whole of construction (§9.1)
        explicit HarfbuzzTextEngine(const std::string& font_path);

        // the opaque freetype, harfbuzz and typeface state, so that no shaping type leaves this header (§3.5, AD-19)
        struct HarfbuzzEngineState;

        // the loaded state, which the engine owns for its whole life (§9.1)
        std::unique_ptr<HarfbuzzEngineState> m_state;
    };

    // install the vendored engine as the default, which is what phase 3 of the text plan does (§9.1)
    void install_harfbuzz_text_engine();
} // namespace albor::harfbuzz
