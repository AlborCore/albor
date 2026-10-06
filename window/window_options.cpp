#include "window/window_options.h"

namespace albor
{
    WindowOptions::WindowOptions() {
        // the documented default is an 800 by 600 window, which is what the spec shows (§5.1)
        size = Size{800.0f, 600.0f};
    }

    const char* WindowOptions::validation_error() const {
        // a window with no title is unnamed in the window list and to assistive technology (§5.1)
        if (title.empty())
            return "window title must not be empty";
        // a window with no area cannot be laid out or presented
        if (size.width <= 0.0f || size.height <= 0.0f)
            return "window size must be positive";
        // a popup is a transient surface, so it never participates in window modality (AD-08, §5.1)
        if (role == WindowRole::Popup && modality != Modality::None)
            return "a popup window cannot be modal";
        // a tool window is a utility surface, so it never participates in window modality (AD-08, §5.1)
        if (role == WindowRole::Tool && modality != Modality::None)
            return "a tool window cannot be modal";
        // every creatable option set is valid once the rules above hold
        return nullptr;
    }

    bool WindowOptions::valid() const {
        // an option set is creatable exactly when it reports no reason to refuse it (§5.1)
        return validation_error() == nullptr;
    }

    bool WindowOptions::is_top_level() const {
        // only a normal and a dialog window participate in the ordinary application window layer (§5.1)
        return role == WindowRole::Normal || role == WindowRole::Dialog;
    }
} // namespace albor
