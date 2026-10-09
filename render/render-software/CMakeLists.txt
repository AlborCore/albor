# the cpu reference backend that implements the rendering hal without a gpu (§3.2, AD-26)

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

# gather cpp sources, which reach only the cpu rasterizer and therefore build on every platform (§3.5, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-render-software STATIC ${CPP_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-software PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-render-software PUBLIC cxx_std_23)

# the skia headers are consumed as system headers, because their own warnings are not the framework's (§3.5)
target_include_directories(albor-render-software SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT})

# the reference draws through the same portable context the gpu backend uses, so the two cannot drift (§6.3, AD-26)
target_link_libraries(albor-render-software PUBLIC albor-render albor-render-skia PRIVATE albor-skia)
