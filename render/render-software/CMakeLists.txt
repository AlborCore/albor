# the cpu reference backend that implements the rendering hal without a gpu (§3.2, AD-26)

# the blend2d port ships a config package that exposes the static library as blend2d::blend2d
find_package(blend2d CONFIG REQUIRED)

# the reference rasterizes glyphs from the carried font bytes with harfbuzz's own rasterizer (AD-26, AD-34)
find_package(harfbuzz CONFIG REQUIRED)

# the rasterizer is a separate archive the harfbuzz config does not expose, so it is found and carried here (AD-26)
find_library(HARFBUZZ_RASTER_DEBUG NAMES harfbuzz-raster PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib" NO_DEFAULT_PATH)
find_library(HARFBUZZ_RASTER_RELEASE NAMES harfbuzz-raster PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib" NO_DEFAULT_PATH)
# a missing archive would fail the link opaquely, so it is refused here where the reason is named (§6.3, AD-26)
if(NOT HARFBUZZ_RASTER_DEBUG OR NOT HARFBUZZ_RASTER_RELEASE)
    message(FATAL_ERROR "albor-render-software requires the harfbuzz rasterizer archive, which was not found")
endif()
add_library(albor-harfbuzz-raster INTERFACE)
target_link_libraries(albor-harfbuzz-raster INTERFACE "$<$<CONFIG:Debug>:${HARFBUZZ_RASTER_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${HARFBUZZ_RASTER_RELEASE}>")

# gather cpp sources, which reach only the cpu rasterizer and therefore build on every platform (§3.5, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-render-software STATIC ${CPP_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-software PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-render-software PUBLIC cxx_std_23)

# the blend2d headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
target_include_directories(albor-render-software SYSTEM PRIVATE $<TARGET_PROPERTY:blend2d::blend2d,INTERFACE_INCLUDE_DIRECTORIES>)

# the harfbuzz rasterizer headers are consumed the same way, because their warnings are not the framework's (§3.5)
target_include_directories(albor-render-software SYSTEM PRIVATE ${HARFBUZZ_INCLUDE_DIRS})

# the reference draws through the same portable context the gpu backend uses, so the two cannot drift (§6.3, AD-26)
#
# the atlas reads the carried font bytes through the registry, so the reference stops asking a gpu text stack (§6.3)
# blend2d arrives through albor-render-blend2d, so a second listing here would put the archive on the link line twice (§6.3)
target_link_libraries(albor-render-software PUBLIC albor-render albor-render-blend2d albor-harfbuzz-raster PRIVATE harfbuzz::harfbuzz albor-resources)

# the auto target links the archive whole, so the cpu reference installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-software-auto INTERFACE)
target_link_libraries(albor-render-software-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-software>")
