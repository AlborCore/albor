#pragma once

#include <string>

#include "albor/core/geometry.h"
#include "albor/core/native_window_handle.h"
#include "albor/window/window_handle.h"

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

    // the keyboard and focus conventions a dialog follows, which is what makes a dialog behave like one (§5.1, §8.3)
    struct DialogConventions
    {
        // whether escape dismisses the dialog unless a child popup consumed it
        bool escape_dismisses = true;

        // whether return runs the default control when nothing claimed the key
        bool return_invokes_default = true;

        // whether the first focusable control takes focus once the tree is arranged
        bool focus_first_control = true;

        // compare two convention sets field by field
        friend bool operator==(const DialogConventions& lhs, const DialogConventions& rhs) = default;
    };

    // the creation time description of a window, which the platform turns into a native window (§5.1)
    struct WindowOptions
    {
        // the window title, which the window manager and assistive technology announce (§5.1)
        std::string title = "Albor Application";

        // the logical size the window opens at
        Size size;

        // the immutable OS window style
        WindowRole role = WindowRole::Normal;

        // the framework enforced modality
        Modality modality = Modality::None;

        // the keyboard and focus conventions a dialog follows, which is default for every other role (§5.1, §8.3)
        DialogConventions dialog{};

        // whether the user may resize the window
        bool resizable = true;

        // whether the window is on screen as soon as it is created
        bool visible = true;

        // the framework handle of the owner a transient surface follows, which is invalid for a top level window (§5.1)
        WindowHandle parent{};

        // the native owner the registry resolves from parent before the platform creates the window (§5.1)
        NativeWindowHandle native_parent{};

        // the owner content rectangle a popup anchors to, in logical content coordinates (§5.1)
        Rect anchor{};

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
