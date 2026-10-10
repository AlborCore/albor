# the inspector that describes a frame and overlays its bounds, which is a development tool (§12.2)

# the sources reach the retained scene, which is what a scene debugger reads (§6.1, §12.2)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-inspector STATIC ${CPP_SOURCES})

# the tool reaches the project root, which is where the module includes point (§3.2)
target_include_directories(albor-inspector PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-inspector PUBLIC cxx_std_23)

# the tool describes and overlays a retained scene, so it reads the render module (§6.1, §12.2)
target_link_libraries(albor-inspector PUBLIC albor-core)
