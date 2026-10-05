#pragma once

namespace albor
{
    // an opaque platform window handle, so that no platform type reaches a public header (§3.5, AD-05)
    class NativeWindowHandle
    {
    public:
        NativeWindowHandle() = default;

        // wrap a platform object the renderer backend needs, such as a CAMetalLayer
        [[nodiscard]] static NativeWindowHandle from_object(const void* object);

        // report whether the handle refers to a platform object at all
        [[nodiscard]] bool valid() const;

        // hand out the wrapped platform object as an opaque pointer
        [[nodiscard]] const void* object() const;

        // compare two handles by identity
        friend bool operator==(const NativeWindowHandle& lhs, const NativeWindowHandle& rhs) = default;

    private:
        // the wrapped platform object, never dereferenced by the framework
        const void* m_object = nullptr;
    };
} // namespace albor
