#include "albor/platform/linux/common/input_normalization.h"

namespace albor::linux_platform
{
    std::string text_from_key(const Modifiers& modifiers, const char* typed, int length) {
        // an accelerator chord names a command rather than text, so it commits no character (§8.3)
        if (modifiers.control || modifiers.option || modifiers.command)
            return std::string();
        // a key that produced no character, like a dead key before its composition, types nothing (§8.3)
        if (typed == nullptr || length <= 0)
            return std::string();
        // a control character is a named key the tree already routes, so it commits no text (§8.3)
        const auto first = static_cast<unsigned char>(typed[0]);
        if (first < 0x20 || first == 0x7f)
            return std::string();
        return std::string(typed, static_cast<std::size_t>(length));
    }

    void PendingWheel::note_axis(float value) {
        // the batch now carries an axis, which is what makes a frame emit a wheel event
        m_axis_seen = true;
        // the deltas of one frame sum, because the compositor may split one gesture over several events (§8.3)
        m_value += value;
    }

    void PendingWheel::note_discrete(std::int32_t steps) {
        // the batch now carries a notch count, which the wheel event reports as lines (§8.3)
        m_discrete_seen = true;
        // the steps of one frame sum, because a fast wheel may report several clicks per frame (§8.3)
        m_steps += steps;
    }

    void PendingWheel::note_value120(std::int32_t value120) {
        // the batch now carries a high resolution count, which the wheel event reports as lines (§8.3)
        m_value120_seen = true;
        // the counts of one frame sum, because a high resolution wheel reports fractions of a notch (§8.3)
        m_value120 += value120;
    }

    bool PendingWheel::pending() const {
        // only an axis event produces a wheel event, so its presence is the whole answer
        return m_axis_seen;
    }

    float PendingWheel::scroll() const {
        // a high resolution count is notches of one hundred twentieths, which a line wheel reports fractionally (§8.3)
        if (m_value120_seen)
            return static_cast<float>(m_value120) / 120.0f;
        // a discrete step count is notches, which a line wheel reports whole (§8.3)
        if (m_discrete_seen)
            return static_cast<float>(m_steps);
        // a continuous delta is surface units, which a point precision wheel reports as is (§8.3)
        return m_value;
    }

    ScrollPrecision PendingWheel::precision() const {
        // a high resolution count and a discrete step count are both notches, which arrive as lines (§8.3)
        if (m_value120_seen || m_discrete_seen)
            return ScrollPrecision::Line;
        // an axis without a discrete count is a continuous delta, which arrives as points (§8.3)
        return ScrollPrecision::Point;
    }

    void PendingWheel::clear() {
        // every recorded fact resets, because the next pointer frame batches on its own (§8.3)
        m_axis_seen = false;
        m_discrete_seen = false;
        m_value120_seen = false;
        m_value = 0.0f;
        m_steps = 0;
        m_value120 = 0;
    }

    Key key_from_keysym(std::uint32_t keysym) {
        // the letters map both cases onto one portable key, because the name follows the printed label (§18.5)
        if (keysym >= KEYSYM_A_LOWER && keysym <= KEYSYM_Z_LOWER)
            return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(keysym - KEYSYM_A_LOWER));
        if (keysym >= KEYSYM_A_UPPER && keysym <= KEYSYM_Z_UPPER)
            return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(keysym - KEYSYM_A_UPPER));
        // the digits occupy one contiguous keysym range, which names every one of them (§18.5)
        if (keysym >= KEYSYM_ZERO && keysym <= KEYSYM_ZERO + 9)
            return static_cast<Key>(static_cast<int>(Key::Zero) + static_cast<int>(keysym - KEYSYM_ZERO));
        // the function keys occupy one contiguous keysym range, which names every one of them (§18.5)
        if (keysym >= KEYSYM_F1 && keysym <= KEYSYM_F12)
            return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(keysym - KEYSYM_F1));
        // the named keys map one keysym onto one portable name (§8.3)
        switch (keysym) {
        // the space bar
        case KEYSYM_SPACE:
            return Key::Space;
        // the backspace key
        case KEYSYM_BACKSPACE:
            return Key::Backspace;
        // the tab key
        case KEYSYM_TAB:
            return Key::Tab;
        // the return key and the keypad enter both activate
        case KEYSYM_RETURN:
        case KEYSYM_KEYPAD_ENTER:
            return Key::Return;
        // the escape key
        case KEYSYM_ESCAPE:
            return Key::Escape;
        // the home key
        case KEYSYM_HOME:
            return Key::Home;
        // the left arrow
        case KEYSYM_LEFT:
            return Key::ArrowLeft;
        // the up arrow
        case KEYSYM_UP:
            return Key::ArrowUp;
        // the right arrow
        case KEYSYM_RIGHT:
            return Key::ArrowRight;
        // the down arrow
        case KEYSYM_DOWN:
            return Key::ArrowDown;
        // the page up key
        case KEYSYM_PAGE_UP:
            return Key::PageUp;
        // the page down key
        case KEYSYM_PAGE_DOWN:
            return Key::PageDown;
        // the end key
        case KEYSYM_END:
            return Key::End;
        // the delete key
        case KEYSYM_DELETE:
            return Key::Delete;
        // every other keysym carries no portable name
        default:
            return Key::None;
        }
    }

    Modifiers modifiers_from_state(bool shift, bool control, bool alt, bool logo) {
        // the modifier keys map onto the portable set, one flag per key (§8.3)
        Modifiers modifiers;
        modifiers.shift = shift;
        modifiers.control = control;
        // the alt key is the option, and the logo key is the command, which the portable names carry (§8.3)
        modifiers.option = alt;
        modifiers.command = logo;
        return modifiers;
    }

    PointerButton button_from_evdev(std::uint32_t button) {
        // the evdev codes name the three standard buttons and the two side buttons (§8.3)
        switch (button) {
        // the left button, which activates a control and starts a drag (§8.3)
        case 0x110:
            return PointerButton::Left;
        // the right button, which asks for a context menu (§8.3)
        case 0x111:
            return PointerButton::Right;
        // the middle button, which a control may claim for a gesture of its own (§8.3)
        case 0x112:
            return PointerButton::Middle;
        // the side and extra buttons are the portable Other, which an application binds (§8.3)
        default:
            return PointerButton::Other;
        }
    }
} // namespace albor::linux_platform
