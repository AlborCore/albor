# the skia backend that implements the rendering hal (§3.2, AD-05, AD-26)
#
# the portable sources build on every platform, because the software rasterizer reference is what CI compares
# against and a CI runner is not an apple machine. Only the metal backend is apple only, and the .macos infix
# marks exactly the sources that reach the metal device and the core animation layer (§5.3, AD-16)


# gather cpp sources, which reach skia only and therefore build everywhere (§3.5, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# the whole archive seam is created before the platform split, because every support level carries the portable half (§6.3, AD-16)
add_library(albor-render-skia-auto INTERFACE)
target_link_libraries(albor-render-skia-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-skia>")

# the metal backend only exists where metal does, and it is the only thing gated on apple (§5.3, AD-16)
if(NOT APPLE)
    message(STATUS "albor-render-skia: the portable context builds, and the metal backend is skipped")

    # create the static library from the portable sources only
    add_library(albor-render-skia STATIC ${CPP_SOURCES})

    # the backend implements the rendering hal and names albor types only (§3.5, AD-05)
    target_include_directories(albor-render-skia PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(albor-render-skia PUBLIC cxx_std_23)

    # the skia headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
    target_include_directories(albor-render-skia SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT})

    # the portable half reaches the cpu rasterizer only, so it needs skia and no platform framework (§6.3, AD-26)
    target_link_libraries(albor-render-skia PUBLIC albor-render PRIVATE albor-skia albor-resources)
    return()
endif()

# gather macos sources, which are the ones that reach metal and the core animation layer
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# create the static library from the gathered sources
add_library(albor-render-skia STATIC ${CPP_SOURCES} ${MACOS_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-skia PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-render-skia PUBLIC cxx_std_23)

# the backend holds metal objects, so arc manages their lifetime rather than hand written retain and release (§6.3)
target_compile_options(albor-render-skia PRIVATE -fobjc-arc)

# the skia headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
target_include_directories(albor-render-skia SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT})

# the backend implements the rendering hal and reaches the metal device and the core animation layer (§6.3, AD-16)
target_link_libraries(albor-render-skia PUBLIC albor-render PRIVATE albor-skia albor-resources "-framework Metal" "-framework QuartzCore" "-framework Foundation")