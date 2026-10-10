#include "core/native_window_handle.h"

namespace albor
{
    NativeWindowHandle NativeWindowHandle::from_object(const void* object) {
        // wrap the platform object in a handle, which is the only form the framework passes it in
        NativeWindowHandle handle;
        handle.m_object = object;
        return handle;
    }

    bool NativeWindowHandle::valid() const {
        // a handle with no object names nothing, so a backend refuses it rather than drawing into nothing
        return m_object != nullptr;
    }

    const void* NativeWindowHandle::object() const {
        // the object leaves the handle as an opaque pointer, which no platform type may name (§3.5)
        return m_object;
    }
} // namespace albor
