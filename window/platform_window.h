#pragma once

#include <memory>
#include <string_view>

#include "core/geometry.h"
#include "core/native_window_handle.h"
#include "window/window_metrics.h"
#include "window/window_options.h"

namespace albor
{
    // the window events a platform backend raises into the framework, which is how a native window stays free of framework types (§5.3)
    class PlatformWindowListener
    {
    public:
        virtual ~PlatformWindowListener() = default;

        // report that the native window changed its size or its backing scale factor
        virtual void on_platform_resize(const WindowMetrics& metrics) = 0;

        // ask the framework whether the close request may proceed, because the application may veto it (§5.2)
        virtual bool on_platform_close_request() = 0;

        // report that the platform pacing source asks for another frame
        virtual void on_platform_frame() = 0;
    };

    // the platform half of a window, which every native backend implements (§5.3)
    class PlatformWindow
    {
    public:
        virtual ~PlatformWindow() = default;

        // attach the listener the native window raises events into
        virtual void set_listener(PlatformWindowListener* listener) = 0;

        // put the native window on screen
        virtual void show() = 0;

        // take the native window off screen
        virtual void hide() = 0;

        // replace the native window title
        virtual void set_title(std::string_view title) = 0;

        // resize the native window in logical pixels
        virtual void set_size(Size size) = 0;

        // read the authoritative native window metrics
        [[nodiscard]] virtual WindowMetrics metrics() const = 0;

        // hand out the native object a renderer backend attaches to, which is opaque here (§3.5, AD-05)
        [[nodiscard]] virtual NativeWindowHandle native_handle() const = 0;

        // ask the native window to present another frame
        virtual void request_frame() = 0;

        // report whether a frame was requested and not yet drawn, which is per window pacing (AD-18b)
        [[nodiscard]] virtual bool frame_requested() const = 0;

        // report whether the native window is currently on screen
        [[nodiscard]] virtual bool visible() const = 0;

        // close the native window without raising a close request
        virtual void close() = 0;

        // build a native window from the options, returning null when the platform refuses them
        [[nodiscard]] static std::unique_ptr<PlatformWindow> create(const WindowOptions& options);
    };
} // namespace albor
