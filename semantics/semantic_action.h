#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "semantics/semantic_node.h"

namespace albor
{
    // the actions assistive technology asks a semantic node to perform (§10.1, §10.2)
    enum class SemanticActionKind
    {
        // run the primary action of a push control
        Activate,

        // flip the state of a checkable control
        Toggle,

        // move a bounded value one step up
        Increment,

        // move a bounded value one step down
        Decrement,

        // open the popup a compact control owns
        Expand,

        // close the popup a compact control owns
        Collapse
    };

    // one action a role supports, which is the portable table both platform adapters read (§10.1, AD-16)
    struct SemanticActionEntry
    {
        // the role the action belongs to
        SemanticRole role;

        // the kind the platform method performs
        SemanticActionKind kind;

        // the name the semantic node registers the action under
        std::string_view name;
    };

    // the roles and actions both adapters agree on, declared once rather than in each platform source (§10.2)
    inline constexpr std::array<SemanticActionEntry, 13> SEMANTIC_ACTIONS{{
        {SemanticRole::Button, SemanticActionKind::Activate, "activate"},
        {SemanticRole::RadioButton, SemanticActionKind::Activate, "activate"},
        {SemanticRole::TabItem, SemanticActionKind::Activate, "activate"},
        {SemanticRole::ListItem, SemanticActionKind::Activate, "activate"},
        {SemanticRole::MenuItem, SemanticActionKind::Activate, "activate"},
        {SemanticRole::Checkbox, SemanticActionKind::Toggle, "toggle"},
        {SemanticRole::Slider, SemanticActionKind::Increment, "increment"},
        {SemanticRole::Slider, SemanticActionKind::Decrement, "decrement"},
        {SemanticRole::NumericStepper, SemanticActionKind::Increment, "increment"},
        {SemanticRole::NumericStepper, SemanticActionKind::Decrement, "decrement"},
        {SemanticRole::ComboBox, SemanticActionKind::Expand, "expand"},
        {SemanticRole::ComboBox, SemanticActionKind::Collapse, "collapse"},
        {SemanticRole::ComboBox, SemanticActionKind::Activate, "activate"},
    }};

    // hand out the name a semantic node registers an action kind under (§10.1)
    [[nodiscard]] constexpr std::string_view action_name(SemanticActionKind kind) {
        // the six kinds are a closed set, so a switch names each one (§10.2)
        switch (kind) {
        case SemanticActionKind::Activate:
            return "activate";
        case SemanticActionKind::Toggle:
            return "toggle";
        case SemanticActionKind::Increment:
            return "increment";
        case SemanticActionKind::Decrement:
            return "decrement";
        case SemanticActionKind::Expand:
            return "expand";
        case SemanticActionKind::Collapse:
            return "collapse";
        }
        // a kind outside the enumeration names nothing, which the quiet default states (§10.2)
        return {};
    }

    // report whether a role supports an action, which is what each adapter asks before it offers a method (§10.2)
    [[nodiscard]] constexpr bool role_supports_action(SemanticRole role, SemanticActionKind kind) {
        // the table is the single statement of the mapping, so a platform cannot invent its own (§10.2, AD-16)
        for (const SemanticActionEntry& entry : SEMANTIC_ACTIONS)
            if (entry.role == role && entry.kind == kind)
                return true;
        return false;
    }

    // hand out the action a press performs for a role, which is empty when a press means nothing (§10.2)
    [[nodiscard]] constexpr std::optional<SemanticActionKind> primary_action(SemanticRole role) {
        // a push control activates, while a checkable control toggles and a combo box opens (§10.1)
        if (role == SemanticRole::Checkbox)
            return SemanticActionKind::Toggle;
        if (role == SemanticRole::ComboBox)
            return SemanticActionKind::Expand;
        // every other role in the table activates, which the entry itself states (§10.2)
        if (role_supports_action(role, SemanticActionKind::Activate))
            return SemanticActionKind::Activate;
        // a role with no press action reports no kind, which no name maps to (§10.2)
        return std::nullopt;
    }
} // namespace albor
