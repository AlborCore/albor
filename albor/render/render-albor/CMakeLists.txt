# the in-repo gpu backend that implements the rendering hal without a third party engine (§3.2, §6.3)

# gather cpp sources, which reach no platform api and therefore build on every platform (§5.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# gather win32 sources, which are the ones that reach direct3d, dxgi and direct composition (AD-16)
file(GLOB_RECURSE WIN32_SOURCES "./*.win32.c++")

# gather linux sources, which are the ones that reach vulkan and the wayland or xcb surface (AD-16)
file(GLOB_RECURSE LINUX_SOURCES "./*.linux.c++")

# the build fact starts off, so a configure without the vulkan packages turns the umbrella back to blend2d (§5.3)
set(ALBOR_VULKAN_FOUND OFF CACHE INTERNAL "the vulkan backend was configured" FORCE)

# the portable seam stands in for the platform device, which apple, windows and vulkan linux replace (§5.3, AD-16)
if(APPLE)
    list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_backend_support\\.cpp$")
    # the metal backend embeds the committed shader library, which the portable stand-in stands in for elsewhere (§5.3, AD-16)
    list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_shaders\\.cpp$")
elseif(WIN32)
    # the direct3d backend supplies the seam, which the portable stand-in would otherwise answer (§5.3, AD-16)
    list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_backend_support\\.cpp$")
endif()

# the vulkan backend supplies the seam and the embedded shader on linux, which the source list must drop before the target exists (§5.3, AD-16)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND AND LINUX_SOURCES)
        list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_backend_support\\.cpp$")
        list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_shaders\\.cpp$")
    endif()
endif()

# create the static library from the gathered sources
add_library(albor-render-albor STATIC ${CPP_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-albor PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-render-albor PUBLIC cxx_std_20)

# the backend draws through the shared hal and carries no third party engine dependency of its own (§6.3)
target_link_libraries(albor-render-albor PUBLIC albor-render)

# the metal backend only exists where metal does, and it is the only part gated on apple (AD-16)
if(APPLE)
    # gather macos sources, which are the ones that reach metal and the core animation layer
    file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

    # the compiled shader is a build input, so a consumer build never needs the metal toolchain (§6.3, AD-16)
    set(METAL_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.metallib")
    # the generated source lives beside the other generated units and carries the portable extension it needs (§14.2)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(METAL_EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/generated/albor_shaders.cpp")
    # every build proves the artifact came from the shader it ships with, which the stamp records (14.2)
    add_custom_command(
        OUTPUT "${METAL_EMBEDDED}"
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.metal -DARTIFACT=${METAL_LIBRARY} -DSTAMP=${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.metal.sha256 -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-shader-stamp.cmake"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${METAL_LIBRARY} -DOUTPUT=${METAL_EMBEDDED} -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-metallib.cmake"
        DEPENDS "${METAL_LIBRARY}" "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.metal" "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.metal.sha256" "${CMAKE_CURRENT_SOURCE_DIR}/embed-metallib.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/verify-shader-stamp.cmake"
        COMMENT "embedding the committed albor_shaders.metallib"
        VERBATIM)

    # the metal sources join the library under arc, which manages their object lifetime (§6.3)
    set_source_files_properties(${MACOS_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(albor-render-albor PRIVATE ${MACOS_SOURCES} "${METAL_EMBEDDED}")
    target_link_libraries(albor-render-albor PRIVATE "-framework Metal" "-framework QuartzCore" "-framework CoreText" "-framework CoreGraphics" "-framework Foundation")
endif()

# the direct3d backend only exists where direct3d, dxgi and direct composition do, which is the windows half (AD-16)
if(WIN32)
    # the compiled artifact is a build input too, so a consumer build needs no shader toolchain (§6.3, AD-16)
    set(D3D_ARTIFACT "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.dxbc")
    # the generated source lives beside the metal one, which the same rules cover (§14.2)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(D3D_EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/generated/albor_shaders_d3d.cpp")
    # every build proves the artifact came from the shader it ships with, which the stamp records (§14.2)
    add_custom_command(
        OUTPUT "${D3D_EMBEDDED}"
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.hlsl -DARTIFACT=${D3D_ARTIFACT} -DSTAMP=${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.hlsl.sha256 "-DREGENERATE=--d3d --write" -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-shader-stamp.cmake"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${D3D_ARTIFACT} -DOUTPUT=${D3D_EMBEDDED} -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-d3dshader.cmake"
        DEPENDS "${D3D_ARTIFACT}" "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.hlsl.sha256" "${CMAKE_CURRENT_SOURCE_DIR}/embed-d3dshader.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/verify-shader-stamp.cmake"
        COMMENT "embedding the committed albor_shaders.dxbc"
        VERBATIM)
    target_sources(albor-render-albor PRIVATE ${WIN32_SOURCES} "${D3D_EMBEDDED}")
    target_link_libraries(albor-render-albor PRIVATE d3d11 dxgi dcomp)
endif()

# the vulkan backend only exists where vulkan does, which is the linux gpu half (AD-16)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # the loader and its headers arrive from vcpkg, so the configure names the package the backend links (§5.3)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND AND LINUX_SOURCES)
        # the committed container is a build input, so a consumer build never needs the shader toolchain (§6.3, AD-16)
        set(VULKAN_ARTIFACT "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.spv")
        # the generated source lives beside the other generated units and carries the portable extension it needs (§14.2)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set(VULKAN_EMBEDDED "${CMAKE_CURRENT_BINARY_DIR}/generated/albor_shaders_vulkan.cpp")
        # every build proves the artifact came from the shader it ships with, which the stamp records (§14.2)
        add_custom_command(
            OUTPUT "${VULKAN_EMBEDDED}"
            COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.hlsl -DARTIFACT=${VULKAN_ARTIFACT} -DSTAMP=${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.spv.sha256 "-DREGENERATE=--vulkan --write" -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-shader-stamp.cmake"
            COMMAND ${CMAKE_COMMAND} -DINPUT=${VULKAN_ARTIFACT} -DOUTPUT=${VULKAN_EMBEDDED} -P "${CMAKE_CURRENT_SOURCE_DIR}/embed-spirv.cmake"
            DEPENDS "${VULKAN_ARTIFACT}" "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/albor_shaders.spv.sha256" "${CMAKE_CURRENT_SOURCE_DIR}/embed-spirv.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/verify-shader-stamp.cmake"
            COMMENT "embedding the committed albor_shaders.spv"
            VERBATIM)
        # the window system objects the surface creation reads arrive from the platform development packages (AD-16)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(WAYLAND_CLIENT_PC QUIET IMPORTED_TARGET wayland-client)
            pkg_check_modules(XCB_PC QUIET IMPORTED_TARGET xcb)
        endif()
        # the public header carries no vulkan type, so only this target sees the loader's include directory (§3.5, AD-05)
        target_sources(albor-render-albor PRIVATE ${LINUX_SOURCES} "${VULKAN_EMBEDDED}")
        target_link_libraries(albor-render-albor PRIVATE Vulkan::Vulkan)
        if(WAYLAND_CLIENT_PC_FOUND)
            # the wayland surface path names the display and surface types the handle carries (AD-16)
            target_compile_definitions(albor-render-albor PRIVATE ALBOR_VULKAN_WAYLAND=1)
            target_link_libraries(albor-render-albor PRIVATE PkgConfig::WAYLAND_CLIENT_PC)
        endif()
        if(XCB_PC_FOUND)
            # the xcb surface path names the connection and window types the handle carries (AD-16)
            target_compile_definitions(albor-render-albor PRIVATE ALBOR_VULKAN_XCB=1)
            target_link_libraries(albor-render-albor PRIVATE PkgConfig::XCB_PC)
        endif()
        # the backend build is published, so the umbrella and the suites see which renderer the host can carry (§5.3)
        set(ALBOR_VULKAN_FOUND ON CACHE INTERNAL "the vulkan backend was configured" FORCE)
    else()
        message(STATUS "albor-render-albor: skipped the vulkan backend, the vulkan loader is not present")
    endif()
endif()

# the auto target links the archive whole, so the backend installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-albor-auto INTERFACE)
target_link_libraries(albor-render-albor-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-albor>")
