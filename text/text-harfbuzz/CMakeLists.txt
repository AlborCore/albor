# the vendored text engine that shapes with harfbuzz instead of a renderer bundled shaper (§9.1, §3.2)

# the vendored shaper and the font reader it needs are direct dependencies, not an accident of the renderer
find_package(harfbuzz CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

# the engine issues the typeface the renderer draws with, which this skia version builds through its font manager (§9.1, AD-28)
find_package(unofficial-skia CONFIG REQUIRED)

# the skia headers self include as "skia/core/...", so the port needs the directory above include/skia as a root
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
endif()

# gather cpp sources, which reach harfbuzz and freetype only and therefore build everywhere (§3.5, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-text-harfbuzz STATIC ${CPP_SOURCES})

# the engine names albor types only, and no shaping type reaches a public header (§3.5, §9.1, AD-19)
target_include_directories(albor-text-harfbuzz PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-text-harfbuzz PUBLIC cxx_std_23)

# the third party headers are consumed as system headers, because their warnings are not the framework's (§3.5)
target_include_directories(albor-text-harfbuzz SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT} ${HARFBUZZ_INCLUDE_DIRS})

# the engine shapes with harfbuzz over a freetype face, and issues the typeface the renderer draws with (§9.1, AD-28)
target_link_libraries(albor-text-harfbuzz PUBLIC albor-text PRIVATE unofficial::skia::skia harfbuzz::harfbuzz Freetype::Freetype)
