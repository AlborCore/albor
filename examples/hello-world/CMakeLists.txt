# the hello-world example, which demonstrates the usage of the framework with different backends (§6.3, AD-26)

# the example drives the macos platform directly, so it only builds on apple platforms (AD-16)
if(NOT APPLE AND NOT WIN32)
    message(STATUS "hello-world: skipped, the example needs the macos/windows platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# executables
add_executable(hello-world-albor ${CPP_SOURCES})
add_executable(hello-world-blend2d ${CPP_SOURCES})

# the example is the composition root, so it wires the platform and the renderer backend together (§6.3)
target_include_directories(hello-world-albor PRIVATE ${ALBOR_SOURCE_DIR})
target_include_directories(hello-world-blend2d PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(hello-world-albor PRIVATE cxx_std_23)
target_compile_features(hello-world-blend2d PRIVATE cxx_std_23)

# platform specific libraries
IF(APPLE)
    target_link_libraries(hello-world-albor PRIVATE albor-platform-macos-auto)
    target_link_libraries(hello-world-blend2d PRIVATE albor-platform-macos-auto)
elseif(WIN32)
    target_link_libraries(hello-world-albor PRIVATE albor-platform-windows-auto)
    target_link_libraries(hello-world-blend2d PRIVATE albor-platform-windows-auto)
endif()

# the framework modules arrive transitively through the backends (§6.2)
target_link_libraries(hello-world-albor PRIVATE albor-render-albor-auto albor-text-harfbuzz-auto)
target_link_libraries(hello-world-blend2d PRIVATE albor-render-blend2d-auto albor-text-harfbuzz-auto)

# when automation is enabled, link the whole-archive umbrella which installs the host at load (§10.3, AD-10)
if(ALBOR_AUTOMATION)
    target_link_libraries(hello-world-albor PRIVATE albor-automation-auto)
    target_link_libraries(hello-world-blend2d PRIVATE albor-automation-auto)
endif()
