#pragma once

#include <cstdint>
#include <string>

#include "albor/ui/ui_event.h"

namespace albor::linux_platform
{
    // the xkb keysym of the space bar, which the portable set names Space (§8.3)
    constexpr std::uint32_t KEYSYM_SPACE = 0x00000020;

    // the xkb keysym of the zero key, where the ten digit keysyms occupy one contiguous range (§18.5)
    constexpr std::uint32_t KEYSYM_ZERO = 0x00000030;

    // the xkb keysym of the first upper case letter, which a shift held letter key names (§8.3)
    constexpr std::uint32_t KEYSYM_A_UPPER = 0x00000041;

    // the xkb keysym of the twelfth upper case letter, where the upper case range is contiguous (§18.5)
    constexpr std::uint32_t KEYSYM_Z_UPPER = 0x0000005a;

    // the xkb keysym of the first lower case letter, where the lower case range is contiguous (§18.5)
    constexpr std::uint32_t KEYSYM_A_LOWER = 0x00000061;

    // the xkb keysym of the twelfth lower case letter, which ends the lower case range (§18.5)
    constexpr std::uint32_t KEYSYM_Z_LOWER = 0x0000007a;

    // the xkb keysym of the backspace key, which removes the character before the caret (§8.3)
    constexpr std::uint32_t KEYSYM_BACKSPACE = 0x0000ff08;

    // the xkb keysym of the tab key, which moves focus forward unless shift is held
    constexpr std::uint32_t KEYSYM_TAB = 0x0000ff09;

    // the xkb keysym of the return key, which activates the focused control
    constexpr std::uint32_t KEYSYM_RETURN = 0x0000ff0d;

    // the xkb keysym of the escape key, which cancels a pending action
    constexpr std::uint32_t KEYSYM_ESCAPE = 0x0000ff1b;

    // the xkb keysym of the home key, which reaches the start of the content a viewport scrolls
    constexpr std::uint32_t KEYSYM_HOME = 0x0000ff50;

    // the xkb keysym of the left arrow key, which reaches a drag target on its left
    constexpr std::uint32_t KEYSYM_LEFT = 0x0000ff51;

    // the xkb keysym of the up arrow key, which reaches a drag target above
    constexpr std::uint32_t KEYSYM_UP = 0x0000ff52;

    // the xkb keysym of the right arrow key, which reaches a drag target on its right
    constexpr std::uint32_t KEYSYM_RIGHT = 0x0000ff53;

    // the xkb keysym of the down arrow key, which reaches a drag target below
    constexpr std::uint32_t KEYSYM_DOWN = 0x0000ff54;

    // the xkb keysym of the page up key, which reaches one viewport back
    constexpr std::uint32_t KEYSYM_PAGE_UP = 0x0000ff55;

    // the xkb keysym of the page down key, which reaches one viewport forward
    constexpr std::uint32_t KEYSYM_PAGE_DOWN = 0x0000ff56;

    // the xkb keysym of the end key, which reaches the end of the content a viewport scrolls
    constexpr std::uint32_t KEYSYM_END = 0x0000ff57;

    // the xkb keysym of the keypad enter key, which activates like the plain return (§8.3)
    constexpr std::uint32_t KEYSYM_KEYPAD_ENTER = 0x0000ff8d;

    // the xkb keysym of the first function key, which an accelerator binds without a letter (§18.5)
    constexpr std::uint32_t KEYSYM_F1 = 0x0000ffbe;

    // the xkb keysym of the twelfth function key, where the twelve function keysyms occupy one contiguous range (§18.5)
    constexpr std::uint32_t KEYSYM_F12 = 0x0000ffc9;

    // the xkb keysym of the delete key, which removes the selected content
    constexpr std::uint32_t KEYSYM_DELETE = 0x0000ffff;

    // the wheel facts one pointer frame batched, which a wheel event carries one of (§8.3)
    class PendingWheel
    {
    public:
        // note the continuous delta the axis event carried, which a trackpad reports as points
        void note_axis(float value);

        // note the steps the axis discrete event carried, which a notched wheel counts
        void note_discrete(std::int32_t steps);

        // note the high resolution count the axis value120 event carried, where one hundred twenty counts one notch
        void note_value120(std::int32_t value120);

        // report whether an axis arrived since the last flush, which a frame answers with one wheel event
        [[nodiscard]] bool pending() const;

        // hand out the scroll the batch reports, which is notches when a wheel turned and points otherwise (§8.3)
        [[nodiscard]] float scroll() const;

        // hand out the unit the scroll arrives in, which a notched wheel counts lines with (§8.3)
        [[nodiscard]] ScrollPrecision precision() const;

        // empty the batch, which the next pointer frame starts fresh
        void clear();

    private:
        // whether an axis event arrived, which is the only kind that produces a wheel event
        bool m_axis_seen = false;

        // whether a discrete event arrived, which carries whole notches on pointer versions before eight (§8.3)
        bool m_discrete_seen = false;

        // whether a value120 event arrived, which carries whole notches on pointer version eight (§8.3)
        bool m_value120_seen = false;

        // the continuous delta the axis events summed, which a trackpad reports in surface units
        float m_value = 0.0f;

        // the steps the discrete events summed, which a notched wheel reports
        std::int32_t m_steps = 0;

        // the counts the value120 events summed, where one hundred twenty counts one notch
        std::int32_t m_value120 = 0;
    };

    // map one xkb keysym onto one portable key, where every other keysym names none (§8.3)
    [[nodiscard]] Key key_from_keysym(std::uint32_t keysym);

    // map one evdev button code onto the portable pointer button, where every other code is Other (§8.3)
    [[nodiscard]] PointerButton button_from_evdev(std::uint32_t button);

    // map the modifiers a keymap resolved onto the portable set, one flag per key (§8.3)
    [[nodiscard]] Modifiers modifiers_from_state(bool shift, bool control, bool alt, bool logo);

    // hand out the text a key press commits, which is empty for a control character or an accelerator chord (§8.3)
    [[nodiscard]] std::string text_from_key(const Modifiers& modifiers, const char* typed, int length);
} // namespace albor::linux_platform
