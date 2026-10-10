# the input normalization and routing of §8.3, which turns platform events into ui tree deliveries

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-input STATIC ${CPP_SOURCES})

# the input types are header visible, because a window routes events through them (§3.5)
target_include_directories(albor-input PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-input PUBLIC cxx_std_23)

# the input module needs the ui tree it routes into, the normalized event it carries and the focus ring it drives (§3.2)
target_link_libraries(albor-input PUBLIC albor-ui albor-focus)