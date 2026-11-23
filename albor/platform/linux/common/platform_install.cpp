// the install mark the linux desktop adapters share, which a second platform family cannot link beside (§5.3, AD-16)

namespace
{
    // the marker is defined once for the linux desktop, so wayland and x11 may stand in one binary (§5.3, AD-22)
    [[maybe_unused]] constexpr int LINUX_PLATFORM_MARKER = 4;
} // namespace

// the mark every platform adapter defines, which the link refuses when a second family is added (§5.3, AD-16)
extern "C" {
    // the linkage block keeps the c symbol unmangled without an extern initializer (gcc)
    int albor_platform_marker = LINUX_PLATFORM_MARKER;
}
