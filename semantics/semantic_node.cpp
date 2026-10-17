#include "semantics/semantic_node.h"

#include <utility>

namespace albor
{
    SemanticNode::SemanticNode(SemanticRole role) :
        m_role(role) {}

    SemanticRole SemanticNode::role() const {
        // the role is what an adapter announces first (§10.1)
        return m_role;
    }

    void SemanticNode::set_role(SemanticRole role) {
        // the role is replaceable because a custom node may adopt one late (§10.1)
        m_role = role;
    }

    const std::string& SemanticNode::explicit_id() const { return m_explicit_id; }

    void SemanticNode::set_explicit_id(std::string id) { m_explicit_id = std::move(id); }

    bool SemanticNode::identified() const { return !m_explicit_id.empty(); }

    const std::string& SemanticNode::name() const {
        // the accessible name is what a screen reader speaks instead of the role (§10.1)
        return m_name;
    }

    void SemanticNode::set_name(std::string name) {
        // the name is stored as written, because it is author supplied text (§10.1)
        m_name = std::move(name);
    }

    const SemanticStates& SemanticNode::states() const {
        // the states are read by the adapters, which never mutate them directly (§10.1)
        return m_states;
    }

    SemanticStates& SemanticNode::mutable_states() {
        // the owning node updates the states as focus and interaction move (§10.1)
        return m_states;
    }

    bool SemanticNode::disabled() const {
        // a disabled node is announced as unavailable (§10.1)
        return m_states.disabled;
    }

    void SemanticNode::set_disabled(bool disabled) {
        // the disabled state is independent of focus and check state (§10.1)
        m_states.disabled = disabled;
    }

    bool SemanticNode::focused() const {
        // the focused state is what a screen reader uses to follow the user (§10.1)
        return m_states.focused;
    }

    void SemanticNode::set_focused(bool focused) {
        // the focused state is independent of disabled (§10.1)
        m_states.focused = focused;
    }

    bool SemanticNode::selected() const {
        // the selected state is what a list announces for the chosen row (§10.1)
        return m_states.selected;
    }

    void SemanticNode::set_selected(bool selected) {
        // the selected state is independent of focus, so a row may carry either alone (§10.1)
        m_states.selected = selected;
    }

    CheckState SemanticNode::check() const {
        // a node that was never made checkable reports not checkable (§10.1)
        return m_states.check;
    }

    void SemanticNode::set_check(CheckState check) {
        // the check state is independent of the role, because a custom node decides (§10.1)
        m_states.check = check;
    }

    bool SemanticNode::checked() const {
        // only the checked state counts as checked, so indeterminate is not (§10.1)
        return m_states.check == CheckState::Checked;
    }

    const std::string& SemanticNode::value() const {
        // the value is what an editable node publishes, and it is empty for every role that has none (§10.1)
        return m_states.value;
    }

    void SemanticNode::set_value(std::string value) {
        // the value is owned by the node, because the adapters read it long after the control published it (§10.1)
        m_states.value = std::move(value);
    }

    bool SemanticNode::read_only() const {
        // read only is announced separately from disabled, because a read only node still takes focus (§10.1)
        return m_states.read_only;
    }

    void SemanticNode::set_read_only(bool read_only) {
        // the state is independent of disabled, because the two refuse different things (§10.1)
        m_states.read_only = read_only;
    }

    bool SemanticNode::multiline() const {
        // a single line node is the default, so the state reads false until an editable control sets it (§10.1)
        return m_states.multiline;
    }

    void SemanticNode::set_multiline(bool multiline) {
        // the state is independent of the value, because a read only node may still span lines (§10.1)
        m_states.multiline = multiline;
    }

    const Rect& SemanticNode::bounds() const {
        // the bounds let an adapter place the accessibility frame (§10.1)
        return m_bounds;
    }

    void SemanticNode::set_bounds(Rect bounds) {
        // the bounds follow the layout, which is why they are writable (§10.1)
        m_bounds = bounds;
    }

    SemanticActionId SemanticNode::add_action(std::string name, SemanticAction action) {
        // an action with no work cannot be invoked, so it is refused (§10.1)
        if (!action)
            return 0;
        Action entry;
        entry.name = std::move(name);
        entry.action = std::move(action);
        m_actions.push_back(std::move(entry));
        // the identifier is the one based index, so zero never names an action (§10.1)
        return static_cast<SemanticActionId>(m_actions.size());
    }

    bool SemanticNode::invoke(SemanticActionId action) const {
        // identifier zero names no action (§10.1)
        if (action == 0 || action > m_actions.size())
            return false;
        // a disabled node refuses every action, which is what assistive technology announces (§10.1)
        if (m_states.disabled)
            return false;
        std::as_const(m_actions[action - 1]).action();
        return true;
    }

    bool SemanticNode::invoke(std::string_view name) const {
        // the action is located by the name an adapter or automation selected it with (§10.1)
        for (std::size_t index = 0; index < m_actions.size(); index++)
            if (m_actions[index].name == name)
                return invoke(static_cast<SemanticActionId>(index + 1));
        // a name nothing registered is not an action, so the invocation fails (§10.1)
        return false;
    }

    bool SemanticNode::has_action(std::string_view name) const {
        // the lookup is by name, which is how automation discovers an action (§10.1)
        for (const Action& action : m_actions)
            if (action.name == name)
                return true;
        return false;
    }

    std::size_t SemanticNode::action_count() const { return m_actions.size(); }

    const std::string& SemanticNode::action_name(SemanticActionId action) const {
        // an unknown identifier has no name, so an empty string answers (§10.1)
        static const std::string empty;
        if (action == 0 || action > m_actions.size())
            return empty;
        return m_actions[action - 1].name;
    }

    bool operator==(const SemanticNode& lhs, const SemanticNode& rhs) {
        // the comparison covers what an adapter observes, which is role, name, states and bounds (§10.1)
        return lhs.m_role == rhs.m_role && lhs.m_name == rhs.m_name && lhs.m_states == rhs.m_states && lhs.m_bounds == rhs.m_bounds;
    }
} // namespace albor
