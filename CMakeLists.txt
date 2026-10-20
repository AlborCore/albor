cmake_minimum_required(VERSION 3.28)

# the framework's own root, which every module includes through, so a consumer keeps its own root (§3.5, AD-05)
set(ALBOR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# the oldest macOS the framework runs on, which the build states rather than inheriting from the sdk (§6.3)
set(ALBOR_OSX_MINIMUM "13.3")

# the framework's own build declares the floor in its cache, while a consumer that fetches it keeps its own
if(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${ALBOR_OSX_MINIMUM}" CACHE STRING "the oldest macOS the framework runs on")
endif()

project(albor LANGUAGES CXX)

# a consumer inherits the framework's targets rather than its cache, so the floor is applied to them after the project
if(APPLE)
    # a consumer that named a lower floor is told why it rises, because the shipped shader targets the minimum
    if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS ALBOR_OSX_MINIMUM)
        message(STATUS "albor: raising the deployment target from ${CMAKE_OSX_DEPLOYMENT_TARGET} to ${ALBOR_OSX_MINIMUM}, which the shipped shader targets")
    endif()
    # an empty value inherits the sdk, which may be newer, so only a lower one is raised
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS ALBOR_OSX_MINIMUM)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${ALBOR_OSX_MINIMUM}")
    endif()
endif()

# c++23 is the language of the whole framework (§14, AD-23)
set(CMAKE_CXX_STANDARD 23)
# enforce the c++23 standard rather than accepting an older one silently
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# disable compiler specific extensions, because the sources are portable c++ (AD-19)
set(CMAKE_CXX_EXTENSIONS OFF)

# generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# build configurations (§2.2): the flags below select which libraries exist in the link
option(ALBOR_DEV "Build the dev runtime extension (Dev configuration)" OFF)
option(ALBOR_TEST "Build the test configuration extras: headless backend, software renderer, and, with ALBOR_AUTOMATION, the embedded automation host" OFF)
option(ALBOR_AUTOMATION "Build the automation core and embedded host (Dev/Test configurations only, AD-10)" OFF)
# a consumer that fetches these sources builds neither the framework's suites nor its examples unless it asks (§14.2)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ALBOR_DEV_BUILD ON)
else()
    set(ALBOR_DEV_BUILD OFF)
endif()
option(ALBOR_EXAMPLES "Build the example applications" ${ALBOR_DEV_BUILD})
option(ALBOR_TESTS "Build the unit test binary" ${ALBOR_DEV_BUILD})
option(ALBOR_COVERAGE "Instrument the unit tests so a coverage report can be generated from them" OFF)

# the configuration name that the debug information and the log labels carry
set(ALBOR_CONFIGURATION "Release" CACHE STRING "The albor configuration this build represents")

# the shared build rules for generated components, which the examples and the fixtures use (§14.2)
include(build/AlborComponent.cmake)

# include paths are relative to the framework root, which the targets publish to their consumers (§3.5, AD-05)
include_directories("${ALBOR_SOURCE_DIR}")

# a configuration is Release when neither the dev runtime extension nor the test extras are enabled (§2.2)
if(NOT ALBOR_DEV AND NOT ALBOR_TEST)
    set(ALBOR_RELEASE ON)
else()
    set(ALBOR_RELEASE OFF)
endif()

# the automation core may never exist in a Release link, because it is excluded at compile and link time (AD-10)
if(ALBOR_AUTOMATION AND ALBOR_RELEASE)
    message(FATAL_ERROR "the automation core is excluded from Release at compile/link time (AD-10)")
endif()

# the phase 1 modules are warning free, so the checks the prototype tree already ran stay in force
if(MSVC)
    # msvc reads -Wall as /Wall and -Wextra as /Wextra, and /W demands a numeric level, so the gcc and clang
    # spelling is a hard error there rather than a weaker warning level. /W4 is the closest equivalent strictness,
    # because it covers the conversion and shadowing diagnostics that -Wconversion and -Wshadow ask for (§5.3, AD-19)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /EHsc)
    # the tree is utf8 on every platform: the universal character names a literal carries and the corpus the text
    # suites pin are utf8, so the source and the execution charset have to be named, or msvc narrows them into the
    # ansi code page the host picked and the text the shaper sees is not the text the file wrote (§9.1, §15, AD-25)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

# the coverage build instruments every module, because a profile that only covers the test sources
# cannot report on the module code the suites exercise (§2.2)
if(ALBOR_COVERAGE)
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

# the platform adapters are written against the os directly, which is what AD-16 decides
if(APPLE)
    enable_language(OBJCXX)

    # objective c++ has no c++23 story, so it takes the highest standard the toolchain offers
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# the skia package is discovered once here, because three modules reach it and a second discovery would put the
# same archive on a link line twice, which the linker then reports as a duplicate (AD-05, AD-26)
find_package(unofficial-skia CONFIG REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

# the single carrier every module that reaches skia links, so the package enters a link line exactly once (AD-05)
# skia's text module is not linked, because the vendored engine shapes text and skia only rasterizes the result (§9.1, AD-34)
add_library(albor-skia INTERFACE)
target_link_libraries(albor-skia INTERFACE unofficial::skia::skia)

# the skia headers self include as "skia/core/...", so every consumer needs the directory above include/skia as an
# include root. it is resolved once here rather than in each module, because three modules reach skia and a copied
# block is a block that drifts (AD-05, AD-28)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
else()
    get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
    list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
    string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
endif()

enable_testing()

# the shared value types every other module includes (§3.2)
add_subdirectory(core)

# window roles, modality, handles and the window registry (§3.2)
add_subdirectory(window)

# the reactive properties, the eager push engine and the immutable snapshots (§3.2, §7)
add_subdirectory(runtime)

# the semantic tree that accessibility and automation consume (§3.2, §10.1)
add_subdirectory(semantics)

# the platform accessibility bridge consumes the semantic tree, so it comes after it (§10.2)
add_subdirectory(accessibility)
add_subdirectory(i18n)

# the automation core exists only when it is explicitly enabled, and never in a release link (§10.3, AD-10)
if(ALBOR_AUTOMATION)
    add_subdirectory(automation)
endif()

# the dev runtime extension is linked only in a Dev configuration, so a release carries no reload path (§12.1, AD-02)
if(ALBOR_DEV)
    add_subdirectory(devrt)
    add_subdirectory(inspector)
endif()

# the rendering hal, the frame scene and the two stage pipeline (§3.2)
add_subdirectory(render)

# application lifecycle, tasks, timers and lifetime tokens (§3.2)
add_subdirectory(application)

# the ui tree, the component facade and the scene build stage (§3.2, §8.1)
add_subdirectory(ui)

# the two pass layout containers of §8.2, which measure and arrange the ui tree (§3.2)
add_subdirectory(layout)

# the text engine of §9.1, which measures a run so containers adapt to their content (§3.2)
add_subdirectory(text)
add_subdirectory(text/text-harfbuzz)

# the frame scheduler integrated animation core of §7.3, which drives properties through their presence channel (§3.2)
add_subdirectory(animation)

# the theme and style tokens of §6.1, which the widgets resolve their paint through (§3.2)
add_subdirectory(style)
add_subdirectory(resources)

# the shared action object of AD-15, whose accelerators, selection contract and undo history every consumer binds to (§3.2, §18)
add_subdirectory(commands)

# the standard widgets of §8, which are ui components that also expose semantics (§3.2)
add_subdirectory(widgets)

# the input normalization and routing of §8.3, which turns platform events into tree deliveries (§3.2)
add_subdirectory(input)

# the per window focus manager of §8.3, which owns which node holds the caret (§3.2)
add_subdirectory(focus)

# the model views that materialize keyed rows, which is what a repeater owns (§3.2, §18.1, AD-13)
add_subdirectory(model)

# the skia metal backend that implements the rendering hal (§3.2, AD-05)
add_subdirectory(render/render-skia)

# the cpu reference backend that ci compares against, which is not a platform adapter (§3.2, AD-26)
add_subdirectory(render/render-software)

# the blend2d cpu backend, a portable alternative to the skia reference (§3.2, AD-26)
add_subdirectory(render/render-blend2d)

# the in-repo gpu backend whose retirement of skia is decided on issue 102 (§6.3, AD-26)
add_subdirectory(render/render-albor)

# the macos appkit adapter with its display link pacing (§3.2, AD-16)
add_subdirectory(platform/macos)
add_subdirectory(platform/headless)

# the win32 adapter with its waitable timer pacing, which self registers when linked (§3.2, AD-16)
add_subdirectory(platform/windows)

# the compiler frontend that turns .ui source into native c++ (§11.1)
add_subdirectory(compiler)
add_subdirectory(language-server)
add_subdirectory(preview)

# the manifest parser that declares the native types a ui file may use (§11.2, AD-17)
add_subdirectory(manifest)

# the developer tools, which build against the compiler and never ship in the runtime (§2.1)
add_subdirectory(tools)

if(ALBOR_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ALBOR_TESTS)
    add_subdirectory(tests)
endif()
