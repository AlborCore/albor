# the win32 adapter with its waitable timer pacing (§3.2, AD-16)

# the adapter is written directly against win32, so it only builds on windows platforms (AD-16)
if(NOT WIN32)
    message(STATUS "albor-platform-windows: skipped, the win32 adapter only builds on windows platforms")
    return()
endif()

# gather win32 sources, which the .win32 infix marks as the ones excluded on every other platform
file(GLOB_RECURSE WIN32_SOURCES "./*.win32.c++")

# gather the portable sources, which the adapter carries for the self install that linking selects (§5.3, AD-16)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-platform-windows STATIC ${WIN32_SOURCES} ${CPP_SOURCES})

# the adapter implements the framework's platform interfaces, which stay free of win32 types (§3.5, AD-05)
target_include_directories(albor-platform-windows PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-platform-windows PUBLIC cxx_std_23)

# the adapter needs the application loop it implements, and reaches the user32 window api, the input method editor
# the text anchor excludes through, and the registry the system appearance reads from (AD-16)
# albor-window arrives transitively through albor-application, so naming it again would duplicate the archive on the link line
target_link_libraries(albor-platform-windows PUBLIC albor-application PRIVATE user32 imm32 advapi32)

# the auto target links the archive whole, so the adapter installs itself at load without a call (§5.3, AD-16)
add_library(albor-platform-windows-auto INTERFACE)
target_link_libraries(albor-platform-windows-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-platform-windows>")
