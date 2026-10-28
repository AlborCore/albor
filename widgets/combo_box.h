#pragma once

#include <functional>
#include <string>
#include <vector>

#include "text/text_engine.h"
#include "widgets/widget.h"

namespace albor
{
    // the top-level layer the popup draws through, declared here so the widget can own its lifetime (§8.3)
    class ComboBoxPopup;

    // the compact choice control that shows one value and opens a popup list of the rest (§8)
    class ComboBox final : public Widget
    {
    public:
        // build an empty combo box over the engines it reads (§8)
        ComboBox(BindingEngine* engine, TextEngine* text_engine, std::string explicit_id = {});

        // remove the popup layer before the widget dies, because the root owns the layer (§8.3)
        ~ComboBox() override;

        // override: measure the trigger from the widest option and the hint (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the trigger, while the popup is placed by the same bounds when it opens (§8.2)
        void arrange(const Rect& bounds) override;

        // override: record the trigger and the arrow, while the popup layer records the list (§6.1)
        void paint_content(RenderContext& context) override;

        // override: a point on the trigger or the open popup is a hit (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: run the pointer and keyboard selection paths (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce a combo box with its selected value and open state (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: expose one semantic item per option, so a reader can walk the choices (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: hand out the choice at an index (§10.1)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // override: a combo box takes the focus, because the keyboard owns its selection (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // override: hand out the trigger plate, which the theme resolves by state (§6.1)
        [[nodiscard]] Color background() const override;

        // override: hand out the trigger border width, which the theme owns (§8.4)
        [[nodiscard]] float border_width() const override;

        // override: hand out the trigger border colour, which resolves by state (§8.4)
        [[nodiscard]] Color border_color() const override;

        // replace the option list, which rebuilds the measured runs and clears an out of range selection (§8.2)
        void set_items(std::vector<std::string> items);

        // hand out the option list the trigger shows (§8)
        [[nodiscard]] const std::vector<std::string>& items() const;

        // select one option by index, where a negative index clears the selection (§8.3)
        void set_selected(int index);

        // hand out the selected index, or negative when nothing is selected (§8.3)
        [[nodiscard]] int selected() const;

        // replace the stable accessible label, which names the choice rather than its value (§10.1)
        void set_label(std::string label);

        // hand out the stable accessible label (§10.1)
        [[nodiscard]] const std::string& label() const;

        // replace the hint an empty combo box draws (§8)
        void set_placeholder(std::string placeholder);

        // hand out the hint an empty combo box draws (§8)
        [[nodiscard]] const std::string& placeholder() const;

        // report whether the popup is open, which is the expanded semantic state (§8.3, §10.1)
        [[nodiscard]] bool expanded() const;

        // open the popup, which is what a press on the trigger does (§8.3)
        void open();

        // close the popup without changing the selection (§8.3)
        void close();

        // set whether the combo box refuses input, which also closes an open popup (§8.3)
        void set_disabled(bool disabled);

        // override: report whether the control refuses input (§8.3)
        [[nodiscard]] bool disabled() const override;

        // install the work told when the user chooses another option (§8.3)
        void set_on_selection_changed(std::function<void(int)> handler);

    private:
        // rebuild the measured trigger and option runs, because a font or an item list changed (§9.1)
        void remeasure();

        // move the highlight by a signed delta while the popup is open (§8.3)
        void move_selection(int delta);

        // choose an option, which is the one path a pointer and a key share (§8.3)
        void choose(int index);

        // record the popup list, which the overlay layer calls after the ordinary tree (§6.1, §8.3)
        void paint_popup(RenderContext& context);

        // find the root the popup layer joins, which is the topmost ancestor (§8.3)
        [[nodiscard]] UiNode* popup_root();

        // the popup layer, which the root owns while the list is open (§8.3)
        ComboBoxPopup* m_popup = nullptr;

        // the layer is a friend because it only forwards the paint and input it owns (§8.3)
        friend class ComboBoxPopup;

        // hand out the trigger rectangle, which is the control's arranged bounds (§8.2)
        [[nodiscard]] Rect trigger_rect() const;

        // hand out the popup rectangle, which hangs under the trigger and is bounded by the theme (§8.2)
        [[nodiscard]] Rect popup_rect() const;

        // hand out the row rectangle at an option index, which a pointer maps through (§8.2)
        [[nodiscard]] Rect option_rect(std::size_t index) const;

        // hand out the option under a point, or negative when the point names none (§8.3)
        [[nodiscard]] int option_at(Point point) const;

        // hand out the number of rows that fit in the popup ceiling (§8.2)
        [[nodiscard]] std::size_t visible_count() const;

        // hand out the first visible option, which follows the selection when the list is longer than the popup (§8.2)
        [[nodiscard]] std::size_t first_visible() const;

        // hand out the font size the runs are measured at (§9.1)
        [[nodiscard]] float font_size() const;

        // hand out the metrics the trigger reads (§8.2)
        [[nodiscard]] const ControlMetrics& metrics() const;

        // the run of the selected option, or the hint when nothing is selected (§9.1)
        [[nodiscard]] const TextLayout& selected_layout() const;

        // the text engine that measures the options, which the application owns (§9.1)
        TextEngine* m_text_engine = nullptr;

        // the option strings in declaration order (§8)
        std::vector<std::string> m_items;

        // the measured run of every option, in the same order (§9.1)
        std::vector<TextLayout> m_item_layouts;

        // the measured run of the selected option, or the hint (§9.1)
        TextLayout m_selected_layout;

        // the measured run of the hint, which an empty choice draws (§9.1)
        TextLayout m_placeholder_layout;

        // the measured run of the disclosure marker, which the trigger draws at its trailing edge (§9.1)
        TextLayout m_arrow_layout;

        // the stable accessible label, which names what the choice means (§10.1)
        std::string m_label;

        // the hint an empty combo box draws (§8)
        std::string m_placeholder;

        // the selected index, or negative when nothing is selected (§8.3)
        int m_selected = -1;

        // the option the pointer is over while the popup is open (§8.3)
        int m_hovered = -1;

        // whether the popup is open, which is the expanded state (§8.3, §10.1)
        bool m_expanded = false;

        // whether the control refuses input (§8.3)
        bool m_disabled = false;

        // the work told when the user chooses another option (§8.3)
        std::function<void(int)> m_on_selection_changed;
    };
} // namespace albor
