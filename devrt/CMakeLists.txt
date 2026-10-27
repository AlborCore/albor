# the dev runtime extension that reloads a running tree from a serialized definition (§12.1)

# the sources reach the ui tree and the runtime hook, and no platform type (§12.1, AD-02)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-devrt STATIC ${CPP_SOURCES})

# the extension reaches the project root, which is where the module includes point (§3.2)
target_include_directories(albor-devrt PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-devrt PUBLIC cxx_std_23)

# the extension patches a ui tree and answers through the runtime hook (§12.1, AD-02)
target_link_libraries(albor-devrt PUBLIC albor-ui albor-core PRIVATE albor-resources)

# the auto target links the archive whole, so the extension installs itself at load without a call (§12.1, AD-02)
add_library(albor-devrt-auto INTERFACE)
target_link_libraries(albor-devrt-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-devrt>")

# the umbrella carries the embedded host as well when automation is enabled, so one link gives an application both (§10.3)
if(ALBOR_AUTOMATION)
    target_link_libraries(albor-devrt-auto INTERFACE albor-automation-auto)
endif()
