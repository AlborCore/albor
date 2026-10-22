# the vendored text engine that shapes with harfbuzz instead of a renderer bundled shaper (§9.1, §3.2)

# the shaper is discovered here so its include root and archive are named explicitly, because the engine
# shapes and draws through harfbuzz alone and no longer relies on a renderer carrying the archive (AD-28, AD-34)
#
# freetype stays installed because this baseline's harfbuzz config find_dependency's it, though no source here links it
find_package(harfbuzz CONFIG REQUIRED)

# the line breaker is ICU's, which is the shaper's own unicode dependency and the fixed one for identity (AD-34)
find_package(ICU REQUIRED COMPONENTS uc i18n)

# the engine issues the platform font the renderer adapts to its own typeface, so no renderer type enters this module (§9.1, AD-28)

# gather cpp sources, which reach harfbuzz only and therefore build everywhere (§3.5, AD-05)
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
target_include_directories(albor-text-harfbuzz PUBLIC ${ALBOR_SOURCE_DIR})
target_compile_features(albor-text-harfbuzz PUBLIC cxx_std_23)

# the third party headers are consumed as system headers, because their warnings are not the framework's (§3.5)
target_include_directories(albor-text-harfbuzz SYSTEM PRIVATE ${HARFBUZZ_INCLUDE_DIRS})

# the engine shapes and draws through harfbuzz over the bytes it carries, and reaches the registry for them (§9.1, AD-34)
#
# the engine resolves and reports fonts through the registry, because a run is keyed on the content
# hash of its bytes and a backend with no platform font access loads from the registry (§15, AD-34)
target_link_libraries(albor-text-harfbuzz PUBLIC albor-text PRIVATE harfbuzz::harfbuzz ICU::uc ICU::i18n albor-resources)

# the resolver reaches core text, which is the platform typeface source this engine draws from (§9.1, AD-28)
if(APPLE)
    set_source_files_properties("./harfbuzz_typeface_resolver.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-text-harfbuzz PRIVATE "-framework CoreText" "-framework CoreFoundation")
elseif(WIN32)
    # the resolver walks the system collection directly, which dwrite fronts, so no skia archive reaches this target (AD-16)
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
