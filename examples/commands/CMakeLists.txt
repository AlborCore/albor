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

# the backends install themselves at load, and the host owns the wiring, so the example only builds and runs its scene (§5.3, §6.3, §9.1, AD-16)
target_link_libraries(albor-commands-example PRIVATE albor-platform-macos-auto albor-render-skia-auto albor-text-harfbuzz-auto albor-widgets albor-layout)