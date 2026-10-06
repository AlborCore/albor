# the skia shaping backend of §9.1, which implements the text engine contract

# the backend reaches core text for the system font collection, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-text-skia: skipped, the skia text backend only builds on apple platforms")
    return()
endif()

# find_package(skia) is deprecated by the port itself and raises an author warning, so the config the port documents is used
find_package(unofficial-skia CONFIG REQUIRED)

# the paragraph module is what shapes and measures, and its absence means the backend cannot be built at all (§9.1)
if(NOT TARGET unofficial::skia::modules::skparagraph)
    message(FATAL_ERROR "albor-text-skia requires skparagraph, so the skia dependency needs its harfbuzz and icu features (§9.1)")
endif()

# the skia headers self include as "skia/...", so the port needs the directory above include/skia as an include root
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# gather macos sources, which are the ones that reach core text for the system font collection
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# create the static library from the gathered sources
add_library(albor-text-skia STATIC ${CPP_SOURCES} ${MACOS_SOURCES})

# the backend implements the contract and names albor types only (§3.5, AD-05)
target_include_directories(albor-text-skia PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-text-skia PUBLIC cxx_std_23)

# the backend public header names skia types, so anything that includes it needs the skia root, and the
# headers are consumed as system ones because their own warnings are not the framework's (§3.5, AD-05)
target_include_directories(albor-text-skia SYSTEM PUBLIC ${ALBOR_SKIA_INCLUDE_ROOT})

# the backend implements the text contract and reaches skparagraph, which carries the shaper and the icu unicode (§9.1)
target_link_libraries(albor-text-skia PUBLIC albor-text PRIVATE unofficial::skia::modules::skparagraph)

# the system font collection comes from core text, which is the platform half of shaping (§9.1)
target_link_libraries(albor-text-skia PRIVATE "-framework CoreText" "-framework CoreFoundation")