# the vendored text engine that shapes with harfbuzz instead of a renderer bundled shaper (§9.1, §3.2)

# the shaper and the font reader are discovered here so their include roots are known, but they enter a link line
# through the shared carrier alone, because skparagraph already carries both archives (AD-28)
find_package(harfbuzz CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

# the engine issues the typeface the renderer draws with, which this skia version builds through its font manager (§9.1, AD-28)

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

# the typeface resolver reaches the platform font manager, so the platform infix marks the one file that is not
# portable. each glob sits inside its own platform guard, because cmake hands a source to the cxx compiler on
# every other platform and neither the core text nor the directwrite manager it includes exists there (§5.3, AD-16)
set(PLATFORM_SOURCES "")
if(APPLE)
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.macos.mm")
elseif(WIN32)
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.win32.c++")
endif()

# create the static library from the gathered sources
add_library(albor-text-harfbuzz STATIC ${CPP_SOURCES} ${PLATFORM_SOURCES})

# the engine names albor types only, and no shaping type reaches a public header (§3.5, §9.1, AD-19)
target_include_directories(albor-text-harfbuzz PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-text-harfbuzz PUBLIC cxx_std_23)

# the third party headers are consumed as system headers, because their warnings are not the framework's (§3.5)
target_include_directories(albor-text-harfbuzz SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT} ${HARFBUZZ_INCLUDE_DIRS})

# the engine shapes with harfbuzz over a freetype face, and issues the typeface the renderer draws with (§9.1, AD-28)
target_link_libraries(albor-text-harfbuzz PUBLIC albor-text PRIVATE albor-skia)

# the resolver reaches core text, which is the platform typeface source this engine draws from (§9.1, AD-28)
if(APPLE)
    set_source_files_properties("./harfbuzz_typeface_resolver.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-text-harfbuzz PRIVATE "-framework CoreText" "-framework CoreFoundation")
elseif(WIN32)
    # skia guards its windows ports behind a define it sets for its own build, so a consumer has to set it too (§9.1)
    target_compile_definitions(albor-text-harfbuzz PRIVATE SK_BUILD_FOR_WIN)
    target_link_libraries(albor-text-harfbuzz PRIVATE dwrite)
endif()

# the windows skia build carries neither freetype nor harfbuzz on its link interface, where the apple one does, so
# the font reader this engine walks an embedded face with is named here rather than assumed to arrive (AD-28)
if(WIN32)
    target_link_libraries(albor-text-harfbuzz PRIVATE freetype)
endif()

# the auto target links the archive whole, so the vendored path installs itself at load without a call (§9.1)
add_library(albor-text-harfbuzz-auto INTERFACE)
target_link_libraries(albor-text-harfbuzz-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-text-harfbuzz>")
