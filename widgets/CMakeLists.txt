# the standard widgets of §8, which are ui components that also expose semantics

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-widgets STATIC ${CPP_SOURCES})

# the widget types are header visible, because a scene is declared in terms of them (§3.5)
target_include_directories(albor-widgets PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-widgets PUBLIC cxx_std_23)

# the widget module needs the semantic tree its actions are declared on, the text engine it measures with, the theme it resolves its paint through, the shared command it presents enablement from, and the row model a list owns its selection through (§3.2, §18.3, §18.7)
target_link_libraries(albor-widgets PUBLIC albor-ui albor-semantics albor-text albor-style albor-commands albor-model)