#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "window/window_handle.h"

namespace albor
{
    class BindingEngine;
    class TextEngine;
    class UiNode;

    // the tree a popup builds over the engines of its owner, which is where its content lives (§8.1)
    using PopupRootBuilder = std::function<std::unique_ptr<UiNode>(BindingEngine&, TextEngine&)>;

    // the service a widget uses to open a native transient surface, which the application implements (§5.1, AD-08)
    class PopupHost
    {
    public:
        virtual ~PopupHost() = default;

        // open a popup owned by the window that holds the anchor, with the content the builder returns (§5.1)
        [[nodiscard]] virtual WindowHandle open_popup(const UiNode& anchor, Rect anchor_rect, Size size, PopupRootBuilder builder, Color background, std::function<void()> on_closed) = 0;

        // hide a popup without destroying its tree, which is what an outside dismissal means (§5.1)
        virtual void close_popup(WindowHandle popup) = 0;

        // show a popup whose tree is already built, which is how a control reopens it (§5.1)
        virtual void show_popup(WindowHandle popup) = 0;

        // destroy a popup and its tree, which its owner does when the control dies (§5.1)
        virtual void destroy_popup(WindowHandle popup) = 0;
    };

    // the host slot is inline because the service is one process wide value, not a per translation unit copy (§5.1)
    inline PopupHost*& window_popup_host_slot() {
        // a process without an application has no host, which is the null default (§5.1)
        static PopupHost* host = nullptr;
        return host;
    }

    // the live application hosts, ordered by construction, which is what a nested or out of order teardown restores (§5.1)
    inline std::vector<PopupHost*>& window_popup_host_stack() {
        // one process wide stack names the host every widget reaches (§5.1)
        static std::vector<PopupHost*> stack;
        return stack;
    }

    // install the host the widget layer reaches, which the application owns for its lifetime (§5.1)
    inline void set_window_popup_host(PopupHost* host) {
        // the application replaces the host when it starts and clears it when it dies (§5.1)
        window_popup_host_slot() = host;
    }

    // make a live application the current host, which keeps the previous one for a later removal (§5.1)
    inline void push_window_popup_host(PopupHost* host) {
        // the stack records the host before the slot points at it (§5.1)
        window_popup_host_stack().push_back(host);
        // the newest live application is the one the widgets reach (§5.1)
        set_window_popup_host(host);
    }

    // drop a host in any destruction order and restore the newest host still alive (§5.1)
    inline void remove_window_popup_host(PopupHost* host) {
        // the host is removed wherever it sits, because an outer application may outlive an inner one (§5.1)
        std::vector<PopupHost*>& stack = window_popup_host_stack();
        stack.erase(std::remove(stack.begin(), stack.end(), host), stack.end());
        // the top of the remaining stack is live by construction, so no dangling host is ever installed (§5.1)
        set_window_popup_host(stack.empty() ? nullptr : stack.back());
    }

    // hand out the installed host, which is null in a process without an application (§5.1)
    [[nodiscard]] inline PopupHost* window_popup_host() {
        // the slot is the one the application wrote (§5.1)
        return window_popup_host_slot();
    }
} // namespace albor
