# the rendering hal, the immutable frame scene and the two stage pipeline (§3.2)

# gather cpp sources (all platforms), dropping every nested backend because each owns its own target
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# a backend module is a directory below render/, and its sources belong to its own target rather than this one
list(FILTER CPP_SOURCES EXCLUDE REGEX "/render-(skia|software|blend2d|albor)/")

# create the static library from the gathered cpp sources
add_library(albor-render STATIC ${CPP_SOURCES})

# the rendering hal is header visible, because every backend implements it (§6.2)
target_include_directories(albor-render PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-render PUBLIC cxx_std_23)

# the render module needs the window metrics it presents into and the shared value types (§3.2)
target_link_libraries(albor-render PUBLIC albor-core albor-window)

# the render thread of stage two is the only thread the module owns (§6.1, AD-18b)
find_package(Threads REQUIRED)
target_link_libraries(albor-render PUBLIC Threads::Threads)