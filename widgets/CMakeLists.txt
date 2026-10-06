# the standard widgets of §8, which are ui components that also expose semantics

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-widgets STATIC ${CPP_SOURCES})

# the widget types are header visible, because a scene is declared in terms of them (§3.5)
target_include_directories(albor-widgets PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-widgets PUBLIC cxx_std_23)

# the widget module needs the semantic tree its actions are declared on and the text engine it measures with (§3.2)
target_link_libraries(albor-widgets PUBLIC albor-ui albor-semantics albor-text)