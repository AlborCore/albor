#pragma once

#include <cstddef>
#include <memory>

#include "core/native_window_handle.h"
#include "semantics/semantic_provider.h"

namespace albor::accessibility
{
    // the bridge publishing the semantic tree to assistive technology, naming no platform type so it stays portable (AD-05)
    class AccessibilityAdapter
    {
    public:
        virtual ~AccessibilityAdapter() = default;

        // attach the bridge to the native window, which assistive technology then reaches through (§10.2)
        virtual void attach_window(NativeWindowHandle window) = 0;

        // publish a rebuilt semantic tree, which the platform reads as its accessibility hierarchy (§10.2)
        virtual void publish(const SemanticTree& tree) = 0;

        // detach from the window, which a closed window must do before its native handle dies (§5.2)
        virtual void detach_window() = 0;

        // report how many nodes the last published tree held, which a coverage test asserts on (§10.2)
        [[nodiscard]] virtual std::size_t published_node_count() const = 0;

        // report whether this build has a platform bridge at all, which is false where none is implemented (§10.2)
        [[nodiscard]] static bool available();
    };

    // the bridge this build provides, or null where the platform has no implementation yet (§10.2)
    [[nodiscard]] std::unique_ptr<AccessibilityAdapter> create_adapter();
} // namespace albor::accessibility
