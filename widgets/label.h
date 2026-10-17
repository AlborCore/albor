#pragma once

#include <string>
#include <vector>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // the widget that draws a run of text, which is what a caption or a value readout is (§8)
    class Label final : public Widget
    {
    public:
        // build a label over the engines it reads, with the default white foreground (§8) the text engine is borrowed rather than owned, because a scene measures every label through the one engine the application installed, and a second one would shape differently (§9.1)
        Label(BindingEngine* engine, TextEngine* text_engine, std::string text = {}, std::string explicit_id = {});

        // measure through the text engine, because a label takes the extent of the run it draws (§8.2, §9.1)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // assign the bounds of the label, which the parent does after measuring (§8.2)
        void arrange(const Rect& bounds) override;

        // record the run at the baseline of the assigned bounds, which is what stage one replays (§6.1)
        void paint_content(RenderContext& context) override;

        // override: answer the pointer transitions, so a label gains hover and focus visuals (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce the run as a label whose accessible name is the text it draws (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // replace the drawn text, which remeasures because the run changed (§9.1)
        void set_text(std::string text);

        // hand out the drawn text, which is also the accessible name (§10.1)
        [[nodiscard]] const std::string& text() const;

        // replace the fill colour of the run, which redraws without remeasuring (§6.1)
        void set_foreground(Color color);

        // hand out the fill colour of the run (§6.1)
        [[nodiscard]] Color foreground() const;

        // set the role the label resolves its size and its colour through, which a theme owns (§8.4)
        void set_role(TextRole role);

        // hand out the role the label is set to (§8.4)
        [[nodiscard]] TextRole role() const;

        // replace the font size the run is measured at, which remeasures because the metrics changed (§9.1)
        void set_font_size(float font_size);

        // hand out the font size the run is measured at (§9.1)
        [[nodiscard]] float font_size() const;

        // replace the family the run is measured with, which remeasures because the face may change (§9.1, AD-34)
        void set_font_family(std::string family);

        // replace the weight the run is measured with, which remeasures because the face may change (§9.1, AD-34)
        void set_font_weight(FontWeight weight);

        // replace the fallback families the run resolves when the named one is absent (§9.1, AD-34)
        void set_font_fallback(std::vector<std::string> fallback);

        // hand out the measured run, which the render pass draws and the tests read (§9.1)
        [[nodiscard]] const TextLayout& layout() const;

    private:
        // the foreground colours the label resolves through, which are the palette when a theme is installed (§6.1)
        [[nodiscard]] const Color* foreground_colors() const;

        // the size the run resolves from the hierarchy, or the explicit value otherwise (§8.4)
        [[nodiscard]] float font_size_used() const;

        // the colour the run draws with, which a secondary role dims through the theme's opacity token (§8.4)
        [[nodiscard]] Color foreground_color() const;

        // the text engine that measures the run, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the run the engine produced for the current text and font size (§9.1)
        TextLayout m_layout;

        // the drawn text, which is also the accessible name (§10.1)
        std::string m_text;

        // the fill colour of the run (§6.1)
        Color m_foreground{1.0f, 1.0f, 1.0f, 1.0f};

        // the size the run is measured at, which the explicit value selects until a role does (§9.1)
        float m_font_size = TextEngine::DEFAULT_FONT_SIZE;

        // the role the run resolves, which is none until an author sets one (§8.4)
        TextRole m_role = TextRole::None;

        // the font selection the run is measured with, which the declarative properties fill (§9.1, AD-34)
        FontSpec m_font_spec;
    };
} // namespace albor
