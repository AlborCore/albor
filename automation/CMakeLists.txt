# the automation core that exposes the running application to test tooling (§10.3)

# the portable half names no platform type, so it builds wherever the framework builds (§10.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-automation STATIC ${CPP_SOURCES})

# the core reaches the project root, which is where the module includes point (§3.2)
target_include_directories(albor-automation PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-automation PUBLIC cxx_std_23)
