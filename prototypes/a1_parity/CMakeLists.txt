# Prototype A1-Parity: renderer parity under a deterministic clock (§30)
# this is the one prototype that deliberately links both prior prototypes, because the whole question is whether skia
# and dawn produce the same image; neither prior prototype's source is modified by it

if(NOT APPLE)
    message(STATUS "prototype a1-parity: skipped, the parity slice compares the macOS metal renderers of both engines")
    return()
endif()

enable_language(OBJCXX)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# skia's headers self include as "include/core/...", so the port needs its include root as a second root; prototype a1
# already resolves this and exports it, and this target inherits it by linking that library
find_package(unofficial-skia CONFIG REQUIRED)

# dawn's config package, resolved here as well as in a1-dawn: the imported target a1-dawn links is scoped to that
# directory, so this target declares its own to get the webgpu headers
find_package(Dawn CONFIG REQUIRED)

# both engines, so the comparison can be made in one process against one generated geometry
add_executable(albor-prototype-a1-parity
    parity.h
    parity_main.cpp
    dawn_offscreen.h
    dawn_offscreen.macos.mm
    skia_offscreen.h
    skia_offscreen.macos.mm
)

# the dawn config package adds its include directory at directory scope rather than on the imported target, so a
# target in another directory has to name the root itself; this is the same root a1-dawn compiles against
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_PARITY_VCPKG_INCLUDE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    set(ALBOR_PARITY_VCPKG_INCLUDE "${CMAKE_SOURCE_DIR}/.build-release/vcpkg_installed/x64-osx/include")
endif()

target_include_directories(albor-prototype-a1-parity PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(albor-prototype-a1-parity SYSTEM PRIVATE ${ALBOR_PARITY_VCPKG_INCLUDE})
target_compile_features(albor-prototype-a1-parity PRIVATE cxx_std_23)

# both prior prototypes already export skia and dawn publicly, so naming them again here only produces a duplicate
# library warning at link time
target_link_libraries(albor-prototype-a1-parity PRIVATE
    albor-prototype-a1
    albor-prototype-a1-dawn
    "-framework QuartzCore"
    "-framework Metal"
)