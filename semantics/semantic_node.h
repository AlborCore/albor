#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/geometry.h"

namespace albor
{
    // the accessible role of a semantic node, which screen readers announce (§10.1, AD-09)
    enum class SemanticRole
    {
        // a node that carries no meaning to assistive technology
        None,

        // a push control that performs an action
        Button,

        // a toggle that carries an on and off state
        Checkbox,

        // a title of a section of the interface
        Heading,

        // static descriptive text
        Label,

        // a continuous value the user drags
        Slider,

        // a read only value that shows how far a task has advanced
        ProgressBar,

        // a container that groups tab items and owns their single selection
        TabView,

        // one page of a tab view, which the user selects
        TabItem,

        // a container that groups radio buttons and owns their single choice
        RadioGroup,

        // one choice in a radio group
        RadioButton,

        // a bounded numeric value with an increment and a decrement
        NumericStepper,

        // a drawing surface, which the user may interact with directly
        Canvas,

        // a single line editable field
        TextField,

        // an image that carries an accessible name
        Image,

        // a row of a list, which repeats the role of its owning list
        ListItem,

        // a container of list items that share one selection, which assistive technology groups (§10.1)
        List,

        // a container that groups other semantic nodes
        Group,

        // a viewport over content larger than it shows, which scrolls (§8, AD-13)
        ScrollView,

        // a native window of the application
        Window
    };

    // the tri state of a checkable semantic node, which a checkbox and a toggle share (§10.1)
    enum class CheckState
    {
        // the node is not checkable, which is what a button reports
        NotCheckable,

        // the node is checked
        Checked,

        // the node is not checked
        Unchecked,

        // the node is checkable and its state is indeterminate
        Indeterminate
    };

    // the current states of a semantic node, which assistive technology reads (§10.1)
    struct SemanticStates
    {
        // whether the node refuses input, which is the disabled state
        bool disabled = false;

        // whether the node holds keyboard focus
        bool focused = false;

        // whether the node is the selected row of a list, which is independent of focus (§10.1)
        bool selected = false;

        // the check state of the node, which a non checkable node reports as not checkable
        CheckState check = CheckState::NotCheckable;

        // the text the node carries, which an editable node reports and every other role leaves empty (§10.1)
        std::string value;

        // whether the node refuses edits but still takes focus and still permits a copy (§10.1)
        bool read_only = false;

        // whether the node edits more than one line, which assistive technology announces (§10.1)
        bool multiline = false;

        // compare two state sets field by field
        friend bool operator==(const SemanticStates& lhs, const SemanticStates& rhs) = default;
    };

    // the identifier of a registered semantic action, where zero names no action
    using SemanticActionId = std::uint64_t;

    // the work a semantic action performs, which automation and accessibility both invoke (§10.1)
    using SemanticAction = std::function<void()>;

    // one node of the unified semantic tree, which is what assistive technology consumes (§10.1, AD-09)
    class SemanticNode
    {
    public:
        // build a semantic node with the given role, which is the minimum an adapter can announce (§10.1)
        explicit SemanticNode(SemanticRole role);

        // hand out the role the node announces (§10.1)
        [[nodiscard]] SemanticRole role() const;

        // replace the role the node announces (§10.1)
        void set_role(SemanticRole role);

        // hand out the author assigned id, which is empty when the author named nothing (AD-11, §18.1)
        [[nodiscard]] const std::string& explicit_id() const;

        // replace the author assigned id, which is what makes a node addressable (AD-11, §18.1)
        void set_explicit_id(std::string id);

        // report whether the node carries an author assigned id, which automation requires (AD-11, §18.1)
        [[nodiscard]] bool identified() const;

        // hand out the accessible name, which is what a screen reader speaks (§10.1)
        [[nodiscard]] const std::string& name() const;

        // replace the accessible name (§10.1)
        void set_name(std::string name);

        // hand out the states the node is in (§10.1)
        [[nodiscard]] const SemanticStates& states() const;

        // hand out the states for writing, which an adapter updates (§10.1)
        [[nodiscard]] SemanticStates& mutable_states();

        // report whether the node refuses input (§10.1)
        [[nodiscard]] bool disabled() const;

        // set or clear the disabled state (§10.1)
        void set_disabled(bool disabled);

        // report whether the node holds keyboard focus (§10.1)
        [[nodiscard]] bool focused() const;

        // set or clear the focused state (§10.1)
        void set_focused(bool focused);

        // report whether the node is the selected row of a list (§10.1)
        [[nodiscard]] bool selected() const;

        // set or clear the selected state, which is independent of focus (§10.1)
        void set_selected(bool selected);

        // hand out the check state of the node (§10.1)
        [[nodiscard]] CheckState check() const;

        // set the check state of the node (§10.1)
        void set_check(CheckState check);

        // report whether the node is currently checked (§10.1)
        [[nodiscard]] bool checked() const;

        // hand out the text the node carries, which an editable node fills and every other role leaves empty (§10.1)
        [[nodiscard]] const std::string& value() const;

        // replace the text the node carries, which is what an editable control publishes (§10.1)
        void set_value(std::string value);

        // report whether the node refuses edits but still takes focus and still permits a copy (§10.1)
        [[nodiscard]] bool read_only() const;

        // set or clear the read only state, which is distinct from disabled because focus is still taken (§10.1)
        void set_read_only(bool read_only);

        // report whether the node edits more than one line (§10.1)
        [[nodiscard]] bool multiline() const;

        // set or clear the multi line state, which assistive technology announces (§10.1)
        void set_multiline(bool multiline);

        // hand out the bounds the node occupies in logical pixels (§10.1)
        [[nodiscard]] const Rect& bounds() const;

        // replace the bounds the node occupies (§10.1)
        void set_bounds(Rect bounds);

        // register an action the node exposes, which assistive technology and automation both invoke (§10.1)
        [[nodiscard]] SemanticActionId add_action(std::string name, SemanticAction action);

        // invoke a registered action, which is const because running one does not change the node (§10.1)
        bool invoke(SemanticActionId action) const;

        // invoke the action registered under a name, which automation selects by label (§10.1)
        bool invoke(std::string_view name) const;

        // report whether the node exposes an action under a name (§10.1)
        [[nodiscard]] bool has_action(std::string_view name) const;

        // hand out how many actions the node exposes (§10.1)
        [[nodiscard]] std::size_t action_count() const;

        // hand out the name of an action by its index (§10.1)
        [[nodiscard]] const std::string& action_name(SemanticActionId action) const;

        // compare two semantic nodes by role, name and state, which the tests assert on
        friend bool operator==(const SemanticNode& lhs, const SemanticNode& rhs);

    private:
        // the role the node announces (§10.1)
        SemanticRole m_role;

        // the author assigned id, which is the only identity automation may select by (AD-11, §18.1)
        std::string m_explicit_id;

        // the accessible name a screen reader speaks (§10.1)
        std::string m_name;

        // the current states of the node (§10.1)
        SemanticStates m_states;

        // the bounds the node occupies in logical pixels (§10.1)
        Rect m_bounds;

        // one registered action, with the name assistive technology and automation select it by (§10.1)
        struct Action
        {
            // the name the action is selected by (§10.1)
            std::string name;

            // the work the action performs (§10.1)
            SemanticAction action;
        };

        // the registered actions, in registration order (§10.1)
        std::vector<Action> m_actions;
    };
} // namespace albor
