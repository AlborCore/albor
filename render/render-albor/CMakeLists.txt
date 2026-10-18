# the in-repo gpu backend that implements the rendering hal without a third party engine (§3.2, §6.3)

# gather cpp sources, which reach no platform api and therefore build on every platform (§5.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-render-albor STATIC ${CPP_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-albor PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-render-albor PUBLIC cxx_std_23)

# the backend draws through the shared hal and carries no third party engine dependency of its own (§6.3)
target_link_libraries(albor-render-albor PUBLIC albor-render)

# the metal backend only exists where metal does, and it is the only part gated on apple (AD-16)
if(APPLE)
    # gather macos sources, which are the ones that reach metal and the core animation layer
    file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

    # the metal sources join the library under arc, which manages their object lifetime (§6.3)
    set_source_files_properties(${MACOS_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(albor-render-albor PRIVATE ${MACOS_SOURCES})
    target_link_libraries(albor-render-albor PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

# the auto target links the archive whole, so the backend installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-albor-auto INTERFACE)
target_link_libraries(albor-render-albor-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-albor>")
