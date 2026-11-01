set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

# the image-provided compiler changes without notice, so the pinned toolset is the ABI boundary (AD-16)
set(VCPKG_DISABLE_COMPILER_TRACKING ON)
