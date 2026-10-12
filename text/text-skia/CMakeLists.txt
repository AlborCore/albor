# the skia shaping backend of §9.1, which implements the text engine contract

# the backend reaches the platform font manager for the system collection, so it builds where one exists (AD-16)
if(NOT APPLE AND NOT WIN32)
    message(STATUS "albor-text-skia: skipped, the skia text backend builds on apple and windows only")
    return()
endif()

# the paragraph module is what shapes and measures, and its absence means the backend cannot be built at all (§9.1)
if(NOT TARGET unofficial::skia::modules::skparagraph)
    message(FATAL_ERROR "albor-text-skia requires skparagraph, so the skia dependency needs its harfbuzz and icu features (§9.1)")
endif()

# the skia headers self include as "skia/...", so the directory above include/skia has to be a root, and
# the skia headers themselves include "include/...", so include/skia has to be a root as well (§3.5)
set(ALBOR_SKIA_INCLUDE_DIRS "${ALBOR_SKIA_INCLUDE_ROOT}")
if(IS_DIRECTORY "${ALBOR_SKIA_INCLUDE_ROOT}/skia")
    list(APPEND ALBOR_SKIA_INCLUDE_DIRS "${ALBOR_SKIA_INCLUDE_ROOT}/skia")
endif()

# gather cpp sources, which reach skparagraph alone and therefore build on either platform (§9.1)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# the platform half reaches the system font collection, and the infix keeps each one out of the other platform (§5.3)
set(PLATFORM_SOURCES "")
if(APPLE)
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.macos.mm")
elseif(WIN32)
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.win32.c++")
endif()

# create the static library from the gathered sources
add_library(albor-text-skia STATIC ${CPP_SOURCES} ${PLATFORM_SOURCES})

# the backend implements the contract and names albor types only (§3.5, AD-05)
target_include_directories(albor-text-skia PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-text-skia PUBLIC cxx_std_23)

# the backend public header names skia types, so anything that includes it needs the skia root, and the
# headers are consumed as system ones because their own warnings are not the framework's (§3.5, AD-05)
target_include_directories(albor-text-skia SYSTEM PUBLIC ${ALBOR_SKIA_INCLUDE_DIRS})

# the backend implements the text contract and reaches skparagraph, which carries the shaper and the icu unicode (§9.1)
target_link_libraries(albor-text-skia PUBLIC albor-text PRIVATE albor-skia)

# the system font collection comes from the platform font manager, which is the platform half of shaping (§9.1)
if(APPLE)
    set_source_files_properties("./skia_text_services.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-text-skia PRIVATE "-framework CoreText" "-framework CoreFoundation")
elseif(WIN32)
    # skia guards its windows ports behind a define it sets for its own build, so a consumer has to set it too (§9.1)
    target_compile_definitions(albor-text-skia PRIVATE SK_BUILD_FOR_WIN)
    target_link_libraries(albor-text-skia PRIVATE dwrite)
endif()

# the auto target links the archive whole, so the engine installs itself at load without a call (§9.1)
add_library(albor-text-skia-auto INTERFACE)
target_link_libraries(albor-text-skia-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-text-skia>")