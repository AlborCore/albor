# the blend2d cpu backend that implements the rendering hal without a gpu (§3.2, AD-26)

# the blend2d port ships a config package that exposes the static library as blend2d::blend2d
find_package(blend2d CONFIG REQUIRED)

# gather cpp sources, which reach only blend2d and therefore build on every platform (§5.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# gather macos sources, which are the ones that reach metal and the core animation layer (AD-16)
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# gather win32 sources, which are the ones that reach direct3d and dxgi (AD-16)
file(GLOB_RECURSE WIN32_SOURCES "./*.win32.c++")

# gather linux sources, which are the ones that reach wayland and present through its shared memory pool (AD-16, AD-26)
file(GLOB_RECURSE LINUX_SOURCES "./*.linux.c++")

# create the static library from the gathered sources
add_library(albor-render-blend2d STATIC ${CPP_SOURCES})

# the apple windowed half only exists where metal does, which is the one part gated on apple (AD-16)
if(APPLE)
    set_source_files_properties(${MACOS_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(albor-render-blend2d PRIVATE ${MACOS_SOURCES})
    target_link_libraries(albor-render-blend2d PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

# the win32 windowed half presents through direct composition, which is the platform path of §5.3 (AD-16)
if(WIN32)
    target_sources(albor-render-blend2d PRIVATE ${WIN32_SOURCES})
    target_link_libraries(albor-render-blend2d PRIVATE d3d11 dxgi dcomp)
endif()

# the linux windowed half presents through a wayland shared memory pool, which needs the wayland client library (AD-16, AD-26)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND LINUX_SOURCES)
    find_package(PkgConfig QUIET)
    # the pool is only present where the wayland client library is, which the platform adapter needs as well (AD-16)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WAYLAND_CLIENT_PC QUIET IMPORTED_TARGET wayland-client)
    endif()
    # a host without the wayland development packages builds the portable raster alone, which the headless surface covers (AD-26)
    if(WAYLAND_CLIENT_PC_FOUND)
        target_sources(albor-render-blend2d PRIVATE ${LINUX_SOURCES})
        target_link_libraries(albor-render-blend2d PRIVATE PkgConfig::WAYLAND_CLIENT_PC)
    endif()
endif()

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-blend2d PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-render-blend2d PUBLIC cxx_std_23)

# the blend2d headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
target_include_directories(albor-render-blend2d SYSTEM PRIVATE $<TARGET_PROPERTY:blend2d::blend2d,INTERFACE_INCLUDE_DIRECTORIES>)

# the backend draws through the shared hal and reads the registered font bytes when it resolves a run (§6.3, AD-34)
target_link_libraries(albor-render-blend2d PUBLIC albor-render PRIVATE blend2d::blend2d albor-resources)

# the auto target links the archive whole, so the backend installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-blend2d-auto INTERFACE)
target_link_libraries(albor-render-blend2d-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-blend2d>")
