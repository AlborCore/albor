#pragma once

#include <string>

#include "core/geometry.h"

namespace albor
{
    // the immutable creation time OS window style of a window (AD-08, §5.1)
    enum class WindowRole
    {
        // standard top level application window
        Normal,

        // secondary window, typically with a parent
        Dialog,

        // floating utility palette or inspector window
        Tool,

        // temporary non activating surface
        Popup
    };

    // the framework enforced behavioural constraint of a window, which is separate from its role (§5.1)
    enum class Modality
    {
        // the window takes no input away from the others
        None,

        // the window blocks its parent window only
        WindowModal,

        // the window blocks every window of the application
        ApplicationModal
    };

    // the creation time description of a window, which the platform turns into a native window (§5.1)
    struct WindowOptions
    {
        // the window title, which must not be empty because both the window manager and assistive technology announce it (§5.1)
        std::string title = "Albor Application";

        // the logical size the window opens at
        Size size;

        // the immutable OS window style
        WindowRole role = WindowRole::Normal;

        // the framework enforced modality
        Modality modality = Modality::None;

        // whether the user may resize the window
        bool resizable = true;

        // whether the window is on screen as soon as it is created
        bool visible = true;

        // the default construction of the documented 800 by 600 window
        WindowOptions();

        // report whether the options describe a creatable window
        [[nodiscard]] bool valid() const;

        // report why the options are not creatable, which is empty when they are
        [[nodiscard]] const char* validation_error() const;

        // report whether the role participates in the ordinary application window layer
        [[nodiscard]] bool is_top_level() const;

        // compare two option sets field by field
        friend bool operator==(const WindowOptions& lhs, const WindowOptions& rhs) = default;
    };
} // namespace albor
