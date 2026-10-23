# the in-repo gpu backend that implements the rendering hal without a third party engine (§3.2, §6.3)

# gather cpp sources, which reach no platform api and therefore build on every platform (§5.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# gather win32 sources, which are the ones that reach direct3d, dxgi and direct composition (AD-16)
file(GLOB_RECURSE WIN32_SOURCES "./*.win32.c++")

# the portable seam stands in for the platform device, which apple and windows replace (§5.3, AD-16)
if(APPLE)
    list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_backend_support\\.cpp$")
    # the metal backend embeds the committed shader library, which the portable stand-in stands in for elsewhere (§5.3, AD-16)
    list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_shaders\\.cpp$")
elseif(WIN32)
    # the direct3d backend supplies the seam, which the portable stand-in would otherwise answer (§5.3, AD-16)
    list(FILTER CPP_SOURCES EXCLUDE REGEX "/albor_backend_support\\.cpp$")
endif()

# create the static library from the gathered sources
add_library(albor-render-albor STATIC ${CPP_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-albor PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-render-albor PUBLIC cxx_std_23)

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

# the auto target links the archive whole, so the backend installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-albor-auto INTERFACE)
target_link_libraries(albor-render-albor-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-albor>")
