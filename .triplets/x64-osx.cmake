# copied from the builtin triplet of the same name, which this file shadows, because an overlay resolves before a builtin triplet
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

# dependencies carry the same floor as the binary that links them (§6.3): the install runs as a separate process, so the floor reaches the triplet through the environment rather than through the cache
if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(VCPKG_OSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}")
else()
    message(FATAL_ERROR "MACOSX_DEPLOYMENT_TARGET is not set, so the dependencies would be stamped with the sdk default rather than the floor")
endif()
