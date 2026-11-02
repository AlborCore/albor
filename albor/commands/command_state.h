#pragma once

#include <string>

namespace albor
{
    // the state a command publishes, which every consumer reads from the same object rather than caching (§18.2, AD-15)
    struct CommandState
    {
        // whether the command may be invoked right now, which leaves the consumer in place and visible (§18.2)
        bool enabled = true;

        // whether the command applies at all, which removes the consumer from layout and from focus traversal (§18.2)
        bool visible = true;

        // the text every consumer presents, read from one place so that three consumers cannot disagree (§18.2)
        std::string label;

        // compare two states field by field, which the publication tests assert on
        friend bool operator==(const CommandState& lhs, const CommandState& rhs) = default;
    };

    // report whether an invocation may run against this state, which is the one gate every route reads (§18.3)
    [[nodiscard]] constexpr bool invocable(const CommandState& state) {
        // a command that does not apply is not invocable, and neither is one that is not permitted right now (§18.2)
        return state.enabled && state.visible;
    }
} // namespace albor
