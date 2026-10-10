# the cpu reference backend that implements the rendering hal without a gpu (§3.2, AD-26)

# find_package(skia) is deprecated by the port itself and raises an author warning, so the config package the port documents is used
find_package(unofficial-skia CONFIG REQUIRED)

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

# the auto target links the archive whole, so the cpu reference installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-software-auto INTERFACE)
target_link_libraries(albor-render-software-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-software>")
