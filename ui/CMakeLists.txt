# the ui tree, the component facade and the scene build stage of the frame pipeline (§8.1)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-ui STATIC ${CPP_SOURCES})

# the ui tree is header visible, because every component derives from it (§3.5)
target_include_directories(albor-ui PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-ui PUBLIC cxx_std_23)

# the ui module needs the reactive engine, the render hal, the window metrics it presents into and the animation
# engine the frame pipeline advances on every frame (§3.2, §7.3)
target_link_libraries(albor-ui PUBLIC albor-core albor-runtime albor-window albor-render albor-animation)