#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "render/frame_scene.h"

namespace albor
{
    // the text half of the framework, which every shaping backend implements and which names no shaping or platform type (§9.1, §3.5, AD-19)
    class TextEngine
    {
    public:
        virtual ~TextEngine() = default;

        // measure a run at the default font size, which is what a call site that does not care uses (§9.1)
        [[nodiscard]] virtual TextLayout measure(std::string_view text, float max_width) const = 0;

        // measure a run at an explicit font size, which the standard widgets use for their label (§9.1)
        [[nodiscard]] virtual TextLayout measure(std::string_view text, float max_width, float font_size) const = 0;

        // report whether the engine can measure at all, which a caller checks before it sizes a scene (§9.1)
        [[nodiscard]] virtual bool ready() const = 0;

        // the font size a call site gets when it does not name one, in logical pixels (§9.1)
        static constexpr float DEFAULT_FONT_SIZE = 16.0f;

        // the width an unbounded run is measured against, because a call site may ask for no ceiling (§8.2)
        static constexpr float UNBOUNDED_WIDTH = 1000000.0f;
    };

    // the signature a text engine implements to build itself (§9.1)
    using TextEngineFactory = std::function<std::unique_ptr<TextEngine>()>;

    // install the text engine the framework measures with, which a backend calls once at startup (§9.1)
    void set_text_engine_factory(TextEngineFactory factory);

    // hand out the installed factory, which is empty until a backend installs one (§9.1)
    [[nodiscard]] const TextEngineFactory& text_engine_factory();

    // build the installed engine, returning null when no backend is installed (§9.1)
    [[nodiscard]] std::unique_ptr<TextEngine> create_text_engine();
} // namespace albor
