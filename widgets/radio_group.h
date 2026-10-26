#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace albor
{
    // one choice in a radio group, which the group owns (§8)
    class RadioButton;

    // the container that lays out radio buttons in a row or column and owns their single selection (§8)
    class RadioGroup final : public Widget
    {
    public:
        // build an empty group over the engine that settles its properties (§8)
        explicit RadioGroup(BindingEngine* engine, std::string explicit_id = {});

        // override: adopt a child, because a selected value written earlier must mark the new choice (§8)
        RadioGroup& add_child(std::unique_ptr<UiNode> child);

        // override: measure the choices and the gap between them (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // override: place the choices in a row or a column (§8.2)
        void arrange(const Rect& bounds) override;

        // override: announce the group, which carries one selected value (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // override: expose one semantic child per radio button (§10.1)
        [[nodiscard]] std::size_t semantic_child_count() const override;

        // override: build the radio button at a position (§10.1)
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // override: the group is a container, so the radio buttons take the focus (§8.3)
        [[nodiscard]] bool accepts_focus() const override;

        // replace the selected value, which is the single choice the group holds (§8)
        void set_selected(int value);

        // hand out the selected value (§8)
        [[nodiscard]] int selected() const;

        // stack the choices vertically when true, or lay them in a row otherwise (§8.2)
        void set_vertical(bool vertical);

        // report whether the choices are stacked vertically (§8.2)
        [[nodiscard]] bool vertical() const;

        // replace the gap between choices (§8.2)
        void set_spacing(float spacing);

        // hand out the gap between choices (§8.2)
        [[nodiscard]] float spacing() const;

        // install the work told about every selection change, which is the signal (§8.3)
        void set_on_selection_changed(std::function<void(int)> handler);

    private:
        // a choice whose value changed asks the group to reapply the selection it owns (§8)
        friend class RadioButton;

        // rebuild the choice pointers when the children changed, which keeps the group current (§8.1)
        void sync_buttons() const;

        // mark the one choice whose value is selected, which is what a group means (§8)
        void apply_selection() const;

        // select the choice a press named (§8.3)
        void select_button(RadioButton* button);

        // move the selection by one choice, which the semantic actions share (§10.1)
        void move_selection(int direction);

        // the radio buttons the group found among its children, in layout order (§8.1)
        mutable std::vector<RadioButton*> m_buttons;

        // the child count the radio list was built from, which is only a fast path (§8.1)
        mutable std::size_t m_synced_children = static_cast<std::size_t>(-1);

        // the value of the selected choice, which an author writes and a press replaces (§8)
        int m_selected = 0;

        // whether the choices run vertically, which is the orientation the issue asks for (§8.2)
        bool m_vertical = false;

        // the gap between two choices, when the author names one rather than taking the theme default (§8.2)
        float m_spacing = 8.0f;

        // whether the gap was written, because an absent gap follows the installed theme (§8.2, §8.4)
        bool m_spacing_explicit = false;

        // the work told about every selection change (§8.3)
        std::function<void(int)> m_on_selection_changed;

        // the token a choice watches, so a choice that outlives the group cannot call into it (§8.3, AD-09)
        struct CallbackToken
        {
            // the group the choice belongs to, which is null once the group has gone (§8.3)
            RadioGroup* group = nullptr;
        };

        // the shared token the installed callbacks watch through (§8.3, AD-09)
        std::shared_ptr<CallbackToken> m_token;
    };
} // namespace albor
