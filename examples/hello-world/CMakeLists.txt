# the hello-world example, which demonstrates the usage of the framework with different backends (§6.3, AD-26)

# the example drives the desktop platform directly, so it only builds where an adapter exists (AD-16)
if(NOT APPLE AND NOT WIN32)
    message(STATUS "hello-world: skipped, the example needs the macos/windows platform adapter")
    return()
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# executables
add_executable(hello-world-albor ${CPP_SOURCES})
add_executable(hello-world-blend2d ${CPP_SOURCES})

# the example chooses only the renderer; the public targets carry the rest (§3.3, AD-19)
target_include_directories(hello-world-albor PRIVATE ${ALBOR_SOURCE_DIR})
target_include_directories(hello-world-blend2d PRIVATE ${ALBOR_SOURCE_DIR})
target_compile_features(hello-world-albor PRIVATE cxx_std_23)
target_compile_features(hello-world-blend2d PRIVATE cxx_std_23)

# the public targets carry the platform, text engine and every other recommended module (§3.3, AD-19)
target_link_libraries(hello-world-albor PRIVATE albor)
target_link_libraries(hello-world-blend2d PRIVATE albor-blend2d)

# when automation is enabled, link the whole-archive umbrella which installs the host at load (§10.3, AD-10)
if(ALBOR_AUTOMATION)
    target_link_libraries(hello-world-albor PRIVATE albor-automation-auto)
    target_link_libraries(hello-world-blend2d PRIVATE albor-automation-auto)
endif()
