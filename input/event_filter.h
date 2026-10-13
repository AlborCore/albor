#pragma once

#include "ui/ui_event.h"

namespace albor
{
    // the classifier that tells a normalized event apart from the others, which is what routing branches on (§8.3)
    class EventFilter
    {
    public:
        // report whether the event came from a pointer, which is what the hit test path serves (§8.3)
        [[nodiscard]] static bool is_pointer(const Event& event);

        // report whether the event came from the keyboard, which is what the focus path serves (§8.3)
        [[nodiscard]] static bool is_keyboard(const Event& event);

        // report whether the event came from the wheel, which is what a scroll handler serves (§8.3)
        [[nodiscard]] static bool is_scroll(const Event& event);

        // report whether the event carries input method text, committed or pre-edit, which is what a field answers (§8.3)
        [[nodiscard]] static bool is_text(const Event& event);

        // report whether the event needs a position, because a key press carries none (§8.3)
        [[nodiscard]] static bool positioned(const Event& event);
    };
} // namespace albor
