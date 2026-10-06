#include "input/event_filter.h"

namespace albor
{
    bool EventFilter::is_pointer(const Event& event) {
        // the three pointer kinds are the ones the hit test path serves (§8.3)
        return event.kind == EventKind::PointerDown || event.kind == EventKind::PointerUp || event.kind == EventKind::PointerMove;
    }

    bool EventFilter::is_keyboard(const Event& event) {
        // the two key kinds are the ones the focus path serves (§8.3)
        return event.kind == EventKind::KeyDown || event.kind == EventKind::KeyUp;
    }

    bool EventFilter::is_scroll(const Event& event) {
        // the wheel kind is the only one that carries a scroll delta (§8.3)
        return event.kind == EventKind::Wheel;
    }

    bool EventFilter::is_text(const Event& event) {
        // the text input kind is the only one an input method produces (§8.3)
        return event.kind == EventKind::TextInput;
    }

    bool EventFilter::positioned(const Event& event) {
        // a key press or a text commit carries no position, because it has no pointer behind it (§8.3)
        return is_pointer(event) || is_scroll(event);
    }
} // namespace albor
