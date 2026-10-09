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

# the shared build rules for generated components, which the examples and the fixtures use (§14.2)
include(build/AlborComponent.cmake)

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

# the single carrier every module that reaches skia or the vendored shaper links, so the package enters a link line
# exactly once (AD-05, AD-28). skparagraph already carries harfbuzz and freetype on its interface, so naming them
# here as well would place the same archives on a link line twice, which the linker reports as a duplicate
add_library(albor-skia INTERFACE)
target_link_libraries(albor-skia INTERFACE unofficial::skia::skia unofficial::skia::modules::skparagraph)

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

# the standard widgets of §8, which are ui components that also expose semantics (§3.2)
add_subdirectory(widgets)

# the input normalization and routing of §8.3, which turns platform events into tree deliveries (§3.2)
add_subdirectory(input)

# the per window focus manager of §8.3, which owns which node holds the caret (§3.2)
add_subdirectory(focus)

# the skia metal backend that implements the rendering hal (§3.2, AD-05)
add_subdirectory(render/render-skia)

# the cpu reference backend that ci compares against, which is not a platform adapter (§3.2, AD-26)
add_subdirectory(render/render-software)

# the macos appkit adapter with its display link pacing (§3.2, AD-16)
add_subdirectory(platform/macos)
add_subdirectory(platform/headless)

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
