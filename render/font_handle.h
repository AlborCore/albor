#pragma once

namespace albor
{
    // an opaque typeface reference, so that no shaping or renderer type reaches a public header (§3.5, §9.1, AD-19)
    class FontHandle
    {
    public:
        FontHandle() = default;

        // wrap a resolved typeface a renderer needs, such as an skia typeface the text engine shaped with (§9.1)
        [[nodiscard]] static FontHandle from_object(const void* object);

        // report whether the handle refers to a typeface at all (§9.1)
        [[nodiscard]] bool valid() const;

        // hand out the wrapped typeface as an opaque pointer (§3.5, §9.1)
        [[nodiscard]] const void* object() const;

        // compare two handles by identity (§9.1)
        friend bool operator==(const FontHandle& lhs, const FontHandle& rhs) = default;

    private:
        // the wrapped typeface, which the engine that issued the handle keeps alive (§9.1)
        const void* m_object = nullptr;
    };
} // namespace albor
