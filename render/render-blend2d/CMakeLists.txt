# the blend2d cpu backend that implements the rendering hal without a gpu (§3.2, AD-26)

# the blend2d port ships a config package that exposes the static library as blend2d::blend2d
find_package(blend2d CONFIG REQUIRED)

# gather cpp sources, which reach only blend2d and therefore build on every platform (§5.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# gather macos sources, which are the ones that reach metal and the core animation layer (AD-16)
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# create the static library from the gathered sources
add_library(albor-render-blend2d STATIC ${CPP_SOURCES})

# the windowed half only exists where metal does, and it is the only part gated on apple (AD-16)
if(APPLE)
    set_source_files_properties(${MACOS_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(albor-render-blend2d PRIVATE ${MACOS_SOURCES})
    target_link_libraries(albor-render-blend2d PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-blend2d PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-render-blend2d PUBLIC cxx_std_23)

# the blend2d headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
target_include_directories(albor-render-blend2d SYSTEM PRIVATE $<TARGET_PROPERTY:blend2d::blend2d,INTERFACE_INCLUDE_DIRECTORIES>)

# the backend draws through the shared hal and reads the registered font bytes when it resolves a run (§6.3, AD-34)
target_link_libraries(albor-render-blend2d PUBLIC albor-render PRIVATE blend2d::blend2d albor-resources)

# the auto target links the archive whole, so the backend installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-blend2d-auto INTERFACE)
target_link_libraries(albor-render-blend2d-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-blend2d>")
