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
endif()

# the local triplets shadow the builtin ones of the same name for the framework's own build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VCPKG_OVERLAY_TRIPLETS "${ALBOR_SOURCE_DIR}/.triplets" CACHE STRING "the local triplets that stamp the dependencies with the floor")
endif()

# the release package PR updates this default, and the port overrides it for a tagged package build (AD-19)
set(ALBOR_VERSION "0.2.0" CACHE STRING "the albor release version")

project(albor VERSION ${ALBOR_VERSION} LANGUAGES CXX)

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

# c++20 is the language of the whole framework (§14, AD-23)
set(CMAKE_CXX_STANDARD 20)
# enforce the c++20 standard rather than accepting an older one silently
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
option(ALBOR_ASAN "Instrument every target with AddressSanitizer" OFF)

# the linux platform font resolver reaches fontconfig, which a host may not ship, so the build can turn it off (§9.1, AD-34)
option(ALBOR_FONTCONFIG "Resolve linux platform fonts through fontconfig" ON)

# the portable backend is a build-time surface, which a consumer picks through the package, while the developer tree carries it (§6.3, AD-26)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # linux keeps it on because it stays the portable fallback and the software reference the tests build (§6.3, AD-26)
    set(ALBOR_RENDERER_BLEND2D_DEFAULT ON)
else()
    # the other platforms carry it only in a developer tree (§6.3, AD-26)
    set(ALBOR_RENDERER_BLEND2D_DEFAULT ${ALBOR_DEV_BUILD})
endif()
option(ALBOR_RENDERER_BLEND2D "Build the portable blend2d CPU backend" ${ALBOR_RENDERER_BLEND2D_DEFAULT})

# the configuration name that the debug information and the log labels carry
set(ALBOR_CONFIGURATION "Release" CACHE STRING "The albor configuration this build represents")

# the shared build rules for generated components, which the examples and the fixtures use (§14.2)
include(build/AlborComponent.cmake)

# the installed package names its directories through gnu conventions, which every install rule below shares (AD-19)
include(GNUInstallDirs)

# include paths resolve through the albor/ folder under the framework root, which the targets publish (§3.5, AD-05)
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
    # windows 10 1809 is the supported floor, which the sdk target macros declare so a newer api cannot slip in (§5.3, AD-16)
    add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wmissing-field-initializers)
    # the style guide forbids braces around a single statement, and a gtest assertion expands to an if/else pair,
    # so the dangling-else warning would demand braces the style guide refuses (STYLE-GUIDE.md Braces)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Wdangling-else ALBOR_HAS_DANGLING_ELSE)
    if(ALBOR_HAS_DANGLING_ELSE)
        add_compile_options(-Wno-dangling-else)
    endif()
    # the designated-field warning is clang only, which gcc does not implement (AD-19)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wmissing-designated-field-initializers)
    endif()
endif()

# the coverage build instruments every module, so profiles include the module code the suites exercise (§2.2)
# msvc has no source-based coverage, so the windows job collects it from the pdb with opencppcoverage instead (§16)
if(ALBOR_COVERAGE AND NOT MSVC)
    # source-based coverage is an LLVM feature, so the clang jobs pin clang in the workflow (§16)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ALBOR_COVERAGE requires clang; configure the coverage preset with clang++")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    # the driver adds the profile runtime at link time
    add_link_options(-fprofile-instr-generate)
endif()

# the address sanitizer instruments every target, because the lifetime gaps the runtime suites catch cross module boundaries (§16, #166)
if(ALBOR_ASAN)
    # the msvc runtime checks conflict with the sanitizer flags, so this preset stays on clang and gcc
    if(MSVC)
        message(FATAL_ERROR "ALBOR_ASAN is supported on the Clang/GCC toolchains; use the normal presets on MSVC")
    endif()
    # every load and store is checked, and the frame pointer keeps the report readable (§16)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    # the block-scope check catches a property destroyed before its binding engine (§16, #164)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fsanitize-address-use-after-scope)
    endif()
    # the sanitizer runtime is linked with the instrumented objects, which is what makes a report possible (§16)
    add_link_options(-fsanitize=address)
endif()

# the platform adapters are written against the os directly, which is what AD-16 decides
if(APPLE)
    enable_language(OBJCXX)

    # objective c++ shares the framework c++20 standard
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

find_package(harfbuzz CONFIG REQUIRED)

enable_testing()

# the shared value types every other module includes (§3.2)
add_subdirectory(albor/core)

# window roles, modality, handles and the window registry (§3.2)
add_subdirectory(albor/window)

# the reactive properties, the eager push engine and the immutable snapshots (§3.2, §7)
add_subdirectory(albor/runtime)

# the semantic tree that accessibility and automation consume (§3.2, §10.1)
add_subdirectory(albor/semantics)

# the platform accessibility bridge consumes the semantic tree, so it comes after it (§10.2)
add_subdirectory(albor/accessibility)
add_subdirectory(albor/i18n)

# the automation core exists only when it is explicitly enabled, and never in a release link (§10.3, AD-10)
if(ALBOR_AUTOMATION)
    add_subdirectory(albor/automation)
endif()

# the dev runtime extension is linked only in a Dev configuration, so a release carries no reload path (§12.1, AD-02)
if(ALBOR_DEV)
    add_subdirectory(albor/devrt)
    add_subdirectory(albor/inspector)
endif()

# the rendering hal, the frame scene and the two stage pipeline (§3.2)
add_subdirectory(albor/render)

# application lifecycle, tasks, timers and lifetime tokens (§3.2)
add_subdirectory(albor/application)

# the ui tree, the component facade and the scene build stage (§3.2, §8.1)
add_subdirectory(albor/ui)

# the two pass layout containers of §8.2, which measure and arrange the ui tree (§3.2)
add_subdirectory(albor/layout)

# the text engine of §9.1, which measures a run so containers adapt to their content (§3.2)
add_subdirectory(albor/text)
add_subdirectory(albor/text/text-harfbuzz)

# the frame scheduler integrated animation core of §7.3, which drives properties through their presence channel (§3.2)
add_subdirectory(albor/animation)

# the theme and style tokens of §6.1, which the widgets resolve their paint through (§3.2)
add_subdirectory(albor/style)
add_subdirectory(albor/resources)

# the shared action object of AD-15, whose accelerators, selection contract and undo history every consumer binds to (§3.2, §18)
add_subdirectory(albor/commands)

# the standard widgets of §8, which are ui components that also expose semantics (§3.2)
add_subdirectory(albor/widgets)

# the input normalization and routing of §8.3, which turns platform events into tree deliveries (§3.2)
add_subdirectory(albor/input)

# the per window focus manager of §8.3, which owns which node holds the caret (§3.2)
add_subdirectory(albor/focus)

# the model views that materialize keyed rows, which is what a repeater owns (§3.2, §18.1, AD-13)
add_subdirectory(albor/model)

# the recommended gpu backend the default runtime names, which builds on every platform with an adapter (§3.2, AD-05)
add_subdirectory(albor/render/render-albor)

# the cpu reference backend that the link line names when chosen, which is not a platform adapter (§3.2, AD-26)
if(ALBOR_RENDERER_BLEND2D)
    add_subdirectory(albor/render/render-blend2d)
endif()

# the software reference the unit tests test against, which is a test-only library (§2.2, AD-26)
if(ALBOR_TEST OR ALBOR_TESTS)
    if(NOT ALBOR_RENDERER_BLEND2D)
        message(FATAL_ERROR "albor: the test surfaces need ALBOR_RENDERER_BLEND2D=ON, because the software reference is built over its rasterization (AD-26)")
    endif()
    add_subdirectory(albor/render/render-software)
endif()

# the macos appkit adapter with its display link pacing (§3.2, AD-16)
add_subdirectory(albor/platform/macos)
add_subdirectory(albor/platform/headless)

# the win32 adapter with its waitable timer pacing, which self registers when linked (§3.2, AD-16)
add_subdirectory(albor/platform/windows)

# the input and surface facts every linux adapter shares, which wayland and x11 both build against (§5.3, AD-16)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(albor/platform/linux/common)
endif()

# the wayland adapter with its timer pacing, which self registers when linked (§3.2, AD-16, AD-22)
add_subdirectory(albor/platform/linux/wayland)

# the x11 adapter, the tier two fallback beside wayland (§3.2, AD-16, AD-22)
add_subdirectory(albor/platform/linux/x11)

# the compiler frontend that turns .ui source into native c++ (§11.1)
add_subdirectory(albor/compiler)
add_subdirectory(albor/language-server)
add_subdirectory(albor/preview)

# the manifest parser that declares the native types a ui file may use (§11.2, AD-17)
add_subdirectory(albor/manifest)

# the developer tools, which build against the compiler and never ship in the runtime (§2.1)
add_subdirectory(tools)

# the text engine is the one implementation the framework recommends, so a consumer never chooses it (§3.3, AD-19)
add_library(albor-text-auto INTERFACE)
target_link_libraries(albor-text-auto INTERFACE albor-text-harfbuzz-auto)
target_compile_features(albor-text-auto INTERFACE cxx_std_20)

# the runtime every public renderer choice shares, so the non-render modules are named once (§3.3, AD-19)
function(_albor_link_default_runtime target)
    # everything that is needed to run the target application
    target_link_libraries(${target} INTERFACE albor-platform-auto albor-text-auto albor-layout albor-widgets albor-accessibility albor-i18n)
    target_compile_features(${target} INTERFACE cxx_std_20)
endfunction()

# the desktop platform is selected by the host, not by the consumer, and only exists where an adapter is built (§3.3, AD-16)
if((APPLE AND NOT IOS) OR WIN32)
    # define the platform library interface
    add_library(albor-platform-auto INTERFACE)
    if(APPLE AND NOT IOS)
        target_link_libraries(albor-platform-auto INTERFACE albor-platform-macos-auto)
    else()
        target_link_libraries(albor-platform-auto INTERFACE albor-platform-windows-auto)
    endif()
    target_compile_features(albor-platform-auto INTERFACE cxx_std_20)

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
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (ALBOR_WAYLAND_FOUND OR ALBOR_X11_FOUND))
    # the linux desktop adapters self register when linked, and the environment picks the one the session runs (§5.3, AD-16, AD-22)
    add_library(albor-platform-auto INTERFACE)
    if(ALBOR_WAYLAND_FOUND)
        target_link_libraries(albor-platform-auto INTERFACE albor-platform-wayland-auto)
    endif()
    if(ALBOR_X11_FOUND)
        target_link_libraries(albor-platform-auto INTERFACE albor-platform-x11-auto)
    endif()
    target_compile_features(albor-platform-auto INTERFACE cxx_std_20)

    # the linux umbrella carries the vulkan backend when the host has it, and blend2d where it does not (§6.3, AD-05)
    if(ALBOR_VULKAN_FOUND)
        # the recommended application target, which pairs the wayland platform with the vulkan backend (§4.1, AD-19)
        add_library(albor INTERFACE)
        _albor_link_default_runtime(albor)
        target_link_libraries(albor INTERFACE albor-render-albor-auto)
    elseif(TARGET albor-render-blend2d-auto)
        # a host without the vulkan packages keeps the portable renderer, which the umbrella can still carry (§6.3, AD-26)
        add_library(albor INTERFACE)
        _albor_link_default_runtime(albor)
        target_link_libraries(albor INTERFACE albor-render-blend2d-auto)
    else()
        # a linux umbrella without a renderer would silently create no surface, so it is not offered (§3.3, AD-16)
        message(STATUS "albor: the linux umbrella needs the vulkan backend or ALBOR_RENDERER_BLEND2D=ON")
    endif()

    # the portable renderer choice, which is the same runtime on linux (§6.3, AD-19)
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
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # the shared linux input and surface facts, which both adapters link (§5.3, AD-16)
        install(TARGETS albor-platform-linux-common albor-platform-linux-common-auto
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        if(ALBOR_WAYLAND_FOUND)
            install(TARGETS albor-platform-wayland albor-platform-wayland-auto
                    EXPORT albor-targets
                    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
        if(ALBOR_X11_FOUND)
            install(TARGETS albor-platform-x11 albor-platform-x11-auto
                    EXPORT albor-targets
                    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endif()

    if(TARGET albor-platform-auto)
        # the platform selection the umbrella names (§3.3)
        install(TARGETS albor-platform-auto
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    if(TARGET albor)
        # the recommended umbrella, which the chosen renderer carries (§3.3)
        install(TARGETS albor
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    if(TARGET albor-blend2d)
        # the portable umbrella, which the blend2d renderer option carries (§6.3, AD-19)
        install(TARGETS albor-blend2d
                EXPORT albor-targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    # the modules' headers install under the albor/ folder with the module structure preserved, which is what the include paths demand (§3.5, AD-05)
    foreach(module core window runtime semantics accessibility i18n animation style resources commands input focus model layout widgets application ui text render platform)
        # the render backend subdirs and the platform-infix headers stay private, which the anchored path regex covers (AD-05)
        install(DIRECTORY "${ALBOR_SOURCE_DIR}/albor/${module}" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/albor FILES_MATCHING PATTERN "*.h" REGEX "(^|/)(render-(software|albor|blend2d)(/|$)|[^/]+\\.(macos|win32|linux)\\.h$)" EXCLUDE)
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
