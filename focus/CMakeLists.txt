# the per window focus manager of §8.3, which owns which node holds the caret

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-focus STATIC ${CPP_SOURCES})

# the focus manager is header visible, because a window owns one (§3.5)
target_include_directories(albor-focus PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-focus PUBLIC cxx_std_23)

# the focus module needs the ui tree it walks and nothing else (§3.2)
target_link_libraries(albor-focus PUBLIC albor-ui)