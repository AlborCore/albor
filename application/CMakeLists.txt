# application lifecycle, tasks, timers and lifetime tokens (§3.2)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-application STATIC ${CPP_SOURCES})

# the application interfaces are header visible, because every application names them (§3.5)
target_include_directories(albor-application PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-application PUBLIC cxx_std_23)

# the application owns the window registry and reaches the rendering hal, and needs nothing else (§4.1)
target_link_libraries(albor-application PUBLIC albor-core albor-window albor-render)
