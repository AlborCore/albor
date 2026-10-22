# the shared action object of AD-15, its accelerators, its selection contract and its undo history (§18)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered cpp sources
add_library(albor-commands STATIC ${CPP_SOURCES})

# the command types are header visible, because a scene binds controls to them from outside the module (§3.5)
target_include_directories(albor-commands PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-commands PUBLIC cxx_std_23)

# the module needs the shared value types and the normalized key vocabulary an accelerator is written in, because a
# shortcut is defined in terms of the portable event types of §8.3 (§3.2, §18.5)
target_link_libraries(albor-commands PUBLIC albor-core albor-ui)