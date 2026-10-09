# the standalone preview host that shows a ui file without a build step (§12.2)

# the sources name the compiler front end and the real widgets, which is the point of a preview (§12.2)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-preview STATIC ${CPP_SOURCES})

# the host reaches the project root, which is where the module includes point (§3.2)
target_include_directories(albor-preview PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-preview PUBLIC cxx_std_23)

# the preview drives the compiler and the widgets, so it links both (§12.2)
target_link_libraries(albor-preview PUBLIC albor-compiler albor-widgets albor-layout albor-text albor-i18n albor-ui albor-runtime)
