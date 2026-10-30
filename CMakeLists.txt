cmake_minimum_required(VERSION 3.28)

# the framework's own root, which every module includes through, so a consumer keeps its own root (§3.5, AD-05)
set(ALBOR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# the oldest macOS the framework runs on, which the build states rather than inheriting from the sdk (§6.3)
set(ALBOR_OSX_MINIMUM "13.3")

# the framework's own build declares the floor in its cache, while a consumer that fetches it keeps its own
if(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${ALBOR_OSX_MINIMUM}" CACHE STRING "the oldest macOS the framework runs on")
endif()

# the install runs inside project(), so the floor has to be in the environment before it: the local triplets stamp every dependency with this build's value, while a consumer wires its own before its own project() (§6.3)
if(APPLE AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # check macos target is set
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        # an empty cache value means no opinion, so the environment falls back to the floor
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${ALBOR_OSX_MINIMUM}")
    endif()
    # the local triplets shadow the builtin ones of the same name, so the selected triplet does not change
    set(VCPKG_OVERLAY_TRIPLETS "${ALBOR_SOURCE_DIR}/.triplets" CACHE STRING "the local triplets that stamp the dependencies with the floor")
endif()

project(albor VERSION 0.1.0 LANGUAGES CXX)

# a consumer inherits the framework's targets rather than its cache, so the floor is applied to them after the project
if(APPLE)
    # a floor below albor's minimum cannot work: the shipped metallib is stamped 13.3 and refuses to load under it (§6.3, AD-16)
    if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS ALBOR_OSX_MINIMUM)
        message(FATAL_ERROR "albor: CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} is below albor's minimum ${ALBOR_OSX_MINIMUM}, which the shipped shader targets and the test suites cover. Set CMAKE_OSX_DEPLOYMENT_TARGET to ${ALBOR_OSX_MINIMUM} or higher.")
    endif()
    # an empty value means the consumer has no opinion, so albor's scope adopts its tested floor
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
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

# the reference and portable backends are build-time surfaces, which a consumer picks through the package, while the developer tree carries both (§6.3, AD-26)
option(ALBOR_RENDERER_SKIA "Build the transitional skia reference backend" ${ALBOR_DEV_BUILD})
option(ALBOR_RENDERER_BLEND2D "Build the portable blend2d CPU backend" ${ALBOR_DEV_BUILD})

# the configuration name that the debug information and the log labels carry
set(ALBOR_CONFIGURATION "Release" CACHE STRING "The albor configuration this build represents")

# the shared build rules for generated components, which the examples and the fixtures use (§14.2)
include(build/AlborComponent.cmake)

# the installed package names its directories through gnu conventions, which every install rule below shares (AD-19)
include(GNUInstallDirs)

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
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wmissing-field-initializers -Wmissing-designated-field-initializers)
endif()

# the coverage build instruments every module, because a profile that only covers the test sources
# cannot report on the module code the suites exercise (§2.2)
if(ALBOR_COVERAGE)
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()

# the test binary compares against the skia backend, so a test build without the reference is refused (AD-26)
if(ALBOR_TESTS AND NOT ALBOR_RENDERER_SKIA)
    message(FATAL_ERROR "albor: ALBOR_TESTS=ON needs ALBOR_RENDERER_SKIA=ON, because the comparison reference covers the suites (AD-26)")
endif()

# the platform adapters are written against the os directly, which is what AD-16 decides
if(APPLE)
    enable_language(OBJCXX)

    # objective c++ has no c++23 story, so it takes the highest standard the toolchain offers
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# the skia package enters the link only when the reference renderer is built, because a skia-free install never compiles against it (§6.3, AD-26)
if(ALBOR_RENDERER_SKIA)
    find_package(unofficial-skia CONFIG REQUIRED)
endif()
find_package(harfbuzz CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

# the single carrier every module that reaches skia links, so the package enters a link line exactly once (AD-05)
# skia's text module is not linked, because the vendored engine shapes text and skia only rasterizes the result (§9.1, AD-34)
if(ALBOR_RENDERER_SKIA)
    add_library(albor-skia INTERFACE)
    target_link_libraries(albor-skia INTERFACE unofficial::skia::skia)
endif()

# the skia headers self include as "skia/core/...", so the skia backend needs the directory above include/skia as an
# include root. it is resolved once here rather than in each module, because the modules that reach skia and a copied
# block is a block that drifts (AD-05, AD-28)
if(ALBOR_RENDERER_SKIA)
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(ALBOR_SKIA_INCLUDE_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
    else()
        get_target_property(ALBOR_SKIA_TARGET_INCLUDES unofficial::skia::skia INTERFACE_INCLUDE_DIRECTORIES)
        list(GET ALBOR_SKIA_TARGET_INCLUDES 0 ALBOR_SKIA_INCLUDE_ROOT)
        string(REGEX REPLACE "/skia$" "" ALBOR_SKIA_INCLUDE_ROOT "${ALBOR_SKIA_INCLUDE_ROOT}")
    endif()
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

# the recommended gpu backend the default runtime names, which builds on every platform with an adapter (§3.2, AD-05)
add_subdirectory(render/render-albor)

# the transitional skia reference, which only the renderer option carries (§6.3, AD-26)
if(ALBOR_RENDERER_SKIA)
    add_subdirectory(render/render-skia)
endif()

# the cpu reference backend that the link line names when chosen, which is not a platform adapter (§3.2, AD-26)
if(ALBOR_RENDERER_BLEND2D)
    add_subdirectory(render/render-blend2d)
endif()

# the software reference the unit tests test against, which is a test-only library (§2.2, AD-26)
if(ALBOR_TEST OR ALBOR_TESTS)
    if(NOT ALBOR_RENDERER_BLEND2D)
        message(FATAL_ERROR "albor: the test surfaces need ALBOR_RENDERER_BLEND2D=ON, because the software reference is built over its rasterization (AD-26)")
    endif()
    add_subdirectory(render/render-software)
endif()

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

# the text engine is the one implementation the framework recommends, so a consumer never chooses it (§3.3, AD-19)
add_library(albor-text-auto INTERFACE)
target_link_libraries(albor-text-auto INTERFACE albor-text-harfbuzz-auto)
target_compile_features(albor-text-auto INTERFACE cxx_std_23)

# the desktop platform is selected by the host, not by the consumer, and only exists where an adapter is built (§3.3, AD-16)
if((APPLE AND NOT IOS) OR WIN32)
    # define the platform library interface
    add_library(albor-platform-auto INTERFACE)
    if(APPLE AND NOT IOS)
        target_link_libraries(albor-platform-auto INTERFACE albor-platform-macos-auto)
    else()
        target_link_libraries(albor-platform-auto INTERFACE albor-platform-windows-auto)
    endif()
    target_compile_features(albor-platform-auto INTERFACE cxx_std_23)

    # the runtime every public renderer choice shares, so the non-render modules are named once (§3.3, AD-19)
    function(_albor_link_default_runtime target)
        # everything that is needed to run the target application
        target_link_libraries(${target} INTERFACE albor-platform-auto albor-text-auto albor-layout albor-widgets albor-accessibility albor-i18n)
        target_compile_features(${target} INTERFACE cxx_std_23)
    endfunction()

    # the recommended application target: the platform, text and renderer are the framework's choice (§4.1, AD-19)
    add_library(albor INTERFACE)
    _albor_link_default_runtime(albor)
    target_link_libraries(albor INTERFACE albor-render-albor-auto)

    # the portable renderer choice: everything else is the same runtime, and it exists only where its backend was built (§6.3, AD-19)
    if(TARGET albor-render-blend2d-auto)
        add_library(albor-blend2d INTERFACE)
        _albor_link_default_runtime(albor-blend2d)
        target_link_libraries(albor-blend2d INTERFACE albor-render-blend2d-auto)
    endif()
else()
    # a desktop umbrella without a desktop platform would silently create no window, so it is not offered (§3.3, AD-16)
    message(STATUS "albor: the desktop umbrella is unavailable until this platform has a platform adapter")
endif()

# the installed package adds its exported targets, headers, tools and config file; only the top-level build owns the install (AD-19, §14.2)
if(ALBOR_DEV_BUILD)

    # the portable runtime, which every supported platform carries (§3.3); each module's own interface already carries the installed include dir (§3.5)
    install(TARGETS
            albor-core albor-window albor-runtime albor-semantics albor-accessibility albor-i18n
            albor-animation albor-style albor-resources albor-commands albor-input albor-focus
            albor-model albor-layout albor-widgets albor-application albor-ui albor-text albor-render
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # the vendored text engine with its whole archive registration and the recommended text seam (§9.1)
    install(TARGETS albor-text-harfbuzz albor-text-harfbuzz-auto albor-text-auto
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # the recommended gpu backend, which every supported platform carries (§6.3)
    install(TARGETS albor-render-albor albor-render-albor-auto
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # the transitional skia reference, which only the renderer option carries (§6.3, AD-26)
    if(ALBOR_RENDERER_SKIA)
        install(TARGETS albor-skia albor-render-skia albor-render-skia-auto
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    # the portable cpu backend, which only the renderer option carries (AD-26)
    if(ALBOR_RENDERER_BLEND2D)
        install(TARGETS albor-render-blend2d albor-render-blend2d-auto
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    # the headless platform exists on every platform and is the test surface's adapter (§5.3)
    install(TARGETS albor-platform-headless albor-platform-headless-auto
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# the desktop platform pairs with the umbrella, which exist only where a desktop adapter does (§3.3, AD-16)
if(APPLE AND NOT IOS)
    install(TARGETS albor-platform-macos albor-platform-macos-auto
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(WIN32)
    install(TARGETS albor-platform-windows albor-platform-windows-auto
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if((APPLE AND NOT IOS) OR WIN32)
    # the recommended umbrella, which the GPU runtime carries (§3.3)
    install(TARGETS albor-platform-auto albor
            EXPORT albor-targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # the portable umbrella, which the blend2d renderer option carries (§6.3, AD-19)
    if(TARGET albor-blend2d)
        install(TARGETS albor-blend2d
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# the modules' headers install with the module structure preserved, which is what the root-relative includes demand (§3.5, AD-05)
foreach(module core window runtime semantics accessibility i18n animation style resources commands input focus model layout widgets application ui text render platform)
    # the render backend subdirs stay private, which an anchored path regex covers without touching a lookalike checkout (AD-05)
    install(DIRECTORY "${ALBOR_SOURCE_DIR}/${module}" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h" REGEX "(^|/)render-(software|skia|albor|blend2d)(/|$)" EXCLUDE)
endforeach()

# the codegen drivers are executables a consumer build runs, which the installed helpers locate through their prefix (AD-19)
install(TARGETS albor-codegen albor-manifest-glue albor-font-embed RUNTIME DESTINATION tools/albor)

# the export lives at lib/cmake/albor, which vcpkg's fixup relocates for every configuration a triplet requests (AD-19)
install(EXPORT albor-targets FILE albor-targets.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/albor)

# the package config, its version file and the shared helpers land beside the export, which is where the consumer searches (AD-19)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${ALBOR_SOURCE_DIR}/build/albor-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/albor-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/albor)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/albor-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/albor-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/albor-config-version.cmake"
    "${ALBOR_SOURCE_DIR}/build/AlborComponent.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/albor)

endif()

if(ALBOR_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ALBOR_TESTS)
    add_subdirectory(tests)
endif()
