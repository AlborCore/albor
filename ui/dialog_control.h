#pragma once

namespace albor
{
    // a control a dialog key can run, which a button marked default or cancel provides (§8.3)
    class DialogControl
    {
    public:
        virtual ~DialogControl() = default;

        // report whether the control is the dialog's default, which return runs (§8.3)
        [[nodiscard]] virtual bool is_default() const = 0;

        // report whether the control is the dialog's cancel, which escape runs (§8.3)
        [[nodiscard]] virtual bool is_cancel() const = 0;

        // perform the press the control performs on a click, which is the one activation path (§8.3)
        virtual void press() = 0;
    };
} // namespace albor
