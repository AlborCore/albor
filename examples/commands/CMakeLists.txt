# the shared action object example, which shows one command bound across a toolbar, a menu row and the keyboard (§14, §18)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-commands-example: skipped, the example needs the macos platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the executable from the gathered cpp sources
add_executable(albor-commands-example ${CPP_SOURCES})

# the public target carries the platform, text engine, widgets and recommended renderer (§3.3, AD-19)
target_link_libraries(albor-commands-example PRIVATE albor)