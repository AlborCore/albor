cmake_minimum_required(VERSION 3.28)

project(albor LANGUAGES CXX)

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
option(ALBOR_TEST "Build the test configuration extras: automation core, headless backend, software renderer" OFF)
option(ALBOR_AUTOMATION "Build the automation core (Dev/Test configurations only, AD-10)" OFF)
option(ALBOR_EXAMPLES "Build the example applications" ON)
option(ALBOR_TESTS "Build the unit test binary" ON)
option(ALBOR_COVERAGE "Instrument the unit tests so a coverage report can be generated from them" OFF)

# the configuration name that the debug information and the log labels carry
set(ALBOR_CONFIGURATION "Release" CACHE STRING "The albor configuration this build represents")

# include paths are relative to the root of the project
include_directories(".")

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
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# the platform adapters are written against the os directly, which is what AD-16 decides
if(APPLE)
    enable_language(OBJCXX)

    # objective c++ has no c++23 story, so it takes the highest standard the toolchain offers
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
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

# the rendering hal, the frame scene and the two stage pipeline (§3.2)
add_subdirectory(render)

# application lifecycle, tasks, timers and lifetime tokens (§3.2)
add_subdirectory(application)

# the ui tree, the component facade and the scene build stage (§3.2, §8.1)
add_subdirectory(ui)

# the skia metal backend that implements the rendering hal (§3.2, AD-05)
add_subdirectory(render/render-skia)

# the macos appkit adapter with its display link pacing (§3.2, AD-16)
add_subdirectory(platform/macos)

if(ALBOR_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ALBOR_TESTS)
    add_subdirectory(tests)
endif()
