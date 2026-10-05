# the skia metal backend that implements the rendering hal (§3.2, AD-05)

# the backend reaches the metal device and the core animation layer, so it only builds on apple platforms (AD-16)
if(NOT APPLE)
    message(STATUS "albor-render-skia: skipped, the metal backend only builds on apple platforms")
    return()
endif()

# find_package(skia) is deprecated by the port itself and raises an author warning, so the config package the port documents is used
find_package(unofficial-skia CONFIG REQUIRED)

# the skia headers self include as "skia/core/...", so the port needs the directory above include/skia as an include root
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
endif()

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# gather macos sources, which are the ones that reach metal and the core animation layer
file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")

# create the static library from the gathered sources
add_library(albor-render-skia STATIC ${CPP_SOURCES} ${MACOS_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-skia PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-render-skia PUBLIC cxx_std_23)

# the backend holds metal objects, so arc manages their lifetime rather than hand written retain and release (§6.3)
target_compile_options(albor-render-skia PRIVATE -fobjc-arc)

# the skia headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
target_include_directories(albor-render-skia SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT})

# the backend implements the rendering hal and reaches the metal device and the core animation layer (§6.3, AD-16)
target_link_libraries(albor-render-skia PUBLIC albor-render PRIVATE unofficial::skia::skia "-framework Metal" "-framework QuartzCore" "-framework Foundation")