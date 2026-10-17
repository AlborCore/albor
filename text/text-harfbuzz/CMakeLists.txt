# the vendored text engine that shapes with harfbuzz instead of a renderer bundled shaper (§9.1, §3.2)

# the shaper is discovered here so its include root and archive are named explicitly, because the engine
# shapes and draws through harfbuzz alone and no longer relies on a renderer carrying the archive (AD-28, AD-34)
#
# freetype stays installed because this baseline's harfbuzz config find_dependency's it, though no source here links it
find_package(harfbuzz CONFIG REQUIRED)

# the engine issues the typeface the renderer draws with, which this skia version builds through its font manager (§9.1, AD-28)

# the skia headers self include as "skia/core/...", so the port needs the directory above include/skia as a root
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
endif()

# gather cpp sources, which reach harfbuzz only and therefore build everywhere (§3.5, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# the typeface resolver reaches the platform font manager, so the .macos infix marks the one file that is not portable
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# create the static library from the gathered sources
add_library(albor-text-harfbuzz STATIC ${CPP_SOURCES} ${MACOS_SOURCES})

# the engine names albor types only, and no shaping type reaches a public header (§3.5, §9.1, AD-19)
target_include_directories(albor-text-harfbuzz PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-text-harfbuzz PUBLIC cxx_std_23)

# the third party headers are consumed as system headers, because their warnings are not the framework's (§3.5)
target_include_directories(albor-text-harfbuzz SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT} ${HARFBUZZ_INCLUDE_DIRS})

# the engine shapes and draws through harfbuzz over the bytes it carries, and reaches the registry for them (§9.1, AD-34)
#
# the engine resolves and reports fonts through the registry, because a run is keyed on the content
# hash of its bytes and a backend with no platform font access loads from the registry (§15, AD-34)
target_link_libraries(albor-text-harfbuzz PUBLIC albor-text PRIVATE harfbuzz::harfbuzz albor-skia albor-resources)

# the resolver reaches core text, which is the platform typeface source this engine draws from (§9.1, AD-28)
if(APPLE)
    set_source_files_properties("./harfbuzz_typeface_resolver.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-text-harfbuzz PRIVATE "-framework CoreText" "-framework CoreFoundation")
endif()

# the auto target links the archive whole, so the vendored path installs itself at load without a call (§9.1)
add_library(albor-text-harfbuzz-auto INTERFACE)
target_link_libraries(albor-text-harfbuzz-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-text-harfbuzz>")
