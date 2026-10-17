# the unit tests of every module, which mirror the source tree layout (STYLE-GUIDE.md)

# gather cpp sources (all platforms), without recursing: a suite that reaches a module only some configurations build
# lives in conditional/ and is added under its own condition below. recursing here once compiled every conditional
# suite unconditionally, which failed the release link on symbols the release link does not have (AD-10)
file(GLOB CPP_SOURCES "./*.cpp")

# the build-scope fixture only feeds a scripted check, so it never joins the test binary (§14.3)
list(FILTER CPP_SOURCES EXCLUDE REGEX "/scope/")

# a tree without a single suite has nothing to build, which is reported rather than silently ignored
if(NOT CPP_SOURCES)
    message(STATUS "albor-tests: no unit suite is present in this tree")
    return()
endif()

find_package(GTest CONFIG REQUIRED)

# create the test binary from the gathered suites, which link into one executable (STYLE-GUIDE.md)
add_executable(albor-tests ${CPP_SOURCES})

# the tests reach the framework through the project root, which is where the module includes point (§3.5)
target_include_directories(albor-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-tests PRIVATE cxx_std_23)

# the ofl test font ships in the tree, so a suite that uses it has a carried font no host must supply (§15, AD-34, #92)
target_compile_definitions(albor-tests PRIVATE ALBOR_TEST_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf" ALBOR_TEST_FONT_FAMILY="Inter")

# the dual-engine corpus source is compiled into a facade and serialized at test time, so the path is named here (§16)
target_compile_definitions(albor-tests PRIVATE ALBOR_DUAL_ENGINE_UI="${CMAKE_CURRENT_SOURCE_DIR}/vectors/dual_engine_panel.ui")

# the tests link the framework modules and nothing platform specific, so they run without a window server (§3.2)
#
# only the modules that are not already re-exported are named here. albor-preview publishes albor-widgets,
# albor-layout and albor-i18n, albor-application publishes albor-input, and albor-widgets publishes
# albor-style and the row model a list owns, so naming those again would reach the linker twice and produce a
# duplicate library warning. Their closure already carries albor-core, albor-window, albor-runtime, albor-render,
# albor-animation, albor-text and albor-ui (§3.2)
#
# GTest::gtest_main already publishes GTest::gtest as part of its interface, so naming gtest here as
# well would hand the linker the same archive twice. Naming only the entry point keeps one copy (§2.2)
#
# albor-manifest is named explicitly because it is a tool the framework itself never includes:
# nothing inside core, ui or render may reach it, because a production build links no parser and
# no interpreter (AD-01, §2.1). It publishes albor-compiler publicly, so the suites reach the
# frontend through it and naming albor-compiler as well would hand the linker the same archive twice (§11.2)
#
# albor-alloc-probe is linked because the binary counts its heap traffic, which is how a steady state frame and a
# cached measure are measured rather than assumed (§2.2). A shipping build never links it, so the probe is excluded
# at link time instead of behind a flag, which is the rule of AD-04 (§2.3)
target_link_libraries(albor-tests PRIVATE albor-render-software albor-platform-headless albor-text-harfbuzz albor-resources albor-text-skia albor-manifest albor-accessibility albor-language-server albor-preview albor-alloc-probe albor-render-blend2d GTest::gtest_main)

# the suite that draws through the skia render context reaches that backend directly, which is the one module the
# other suites never link because it reaches a platform surface (§3.2, AD-16). the backend is apple only, so the
# suite is gathered and linked on apple platforms alone, and the raster canvas it draws into needs no window
# server, so the binary still runs headless (§2.2)
if(ALBOR_DEV)
    target_sources(albor-tests PRIVATE "./conditional/devrt.test.cpp")
    target_link_libraries(albor-tests PRIVATE albor-devrt)
    target_sources(albor-tests PRIVATE "./conditional/inspector.test.cpp")
    target_link_libraries(albor-tests PRIVATE albor-inspector)
endif()

if(APPLE)
    target_sources(albor-tests PRIVATE "./conditional/render_skia.test.cpp")
    target_link_libraries(albor-tests PRIVATE albor-render-skia)

    # the accessibility suite walks the application kit hierarchy the macos bridge publishes into, because the
    # role mapping is only reachable once the bridge hands its tree to that hierarchy (§10.2, AD-16). the suite is
    # an objective c++ source and links the framework itself, because the bridge keeps its platform link private (§3.2)
    target_sources(albor-tests PRIVATE "./conditional/accessibility_role.test.macos.mm")
    set_source_files_properties("./conditional/accessibility_role.test.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-tests PRIVATE "-framework AppKit" "-framework Foundation")

    # the popup suite drives the appkit window adapter directly, because the popup shape is a native appkit surface
    # (§5.1, AD-16). the adapter is linked plain rather than through its -auto interface, because only the whole-archive
    # form pulls both platform_install units and collides on albor_platform_marker; the plain link pulls the window
    # unit on demand and installs nothing, which the suite does not need because it calls the create factory directly
    target_sources(albor-tests PRIVATE "./conditional/platform_window_popup.test.macos.mm")
    set_source_files_properties("./conditional/platform_window_popup.test.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-tests PRIVATE albor-platform-macos)

    # the backend lifecycle suite destroys a second gpu context in one process, which the metal driver traps on
    # after ganesh has used it. the trap kills the process, so no release run could finish while the suite is in,
    # and issue 29 tracks the driver defect rather than this repository. the suite is held out of the release
    # configuration at link time, which is what AD-04 requires of a configuration specific suite, and the two
    # tests come back the day the driver is fixed (§6.3, issue 29)
    if(NOT ALBOR_CONFIGURATION STREQUAL "Release")
        target_sources(albor-tests PRIVATE "./conditional/metal_teardown.test.cpp")
    endif()
endif()


# the automation suite reaches the automation core, which exists only when it is enabled, so the suite is gathered
# under the same condition. a release configure has no such target, and referencing it would fail the link (AD-10)
if(ALBOR_AUTOMATION)
    target_sources(albor-tests PRIVATE "./conditional/automation.test.cpp" "./conditional/mcp_server.test.cpp")
    target_sources(albor-tests PRIVATE "./conditional/semantic_commands.test.cpp")
    target_sources(albor-tests PRIVATE "./conditional/command_commands.test.cpp")
    if(APPLE)
        target_sources(albor-tests PRIVATE "./conditional/automation_transport.test.cpp")
    endif()
    target_link_libraries(albor-tests PRIVATE albor-automation)
endif()

# the allocation probe replaces the compiler's operator new, which an optimised build resolves before the archive
# member is ever pulled in, so a counting bound cannot hold there. the suite that asserts one is therefore held
# out of the release configuration at link time rather than left failing, which is what issue 31 asks for and
# what AD-04 requires of a configuration specific suite (§2.3, issue 31)
if(NOT ALBOR_CONFIGURATION STREQUAL "Release")
    target_sources(albor-tests PRIVATE "./conditional/allocation_release_blind.test.cpp")
endif()

# the test binary is executed directly rather than through ctest, so it never needs a display (§2.2)
include(GoogleTest)
gtest_discover_tests(albor-tests DISCOVERY_TIMEOUT 60)

# the blend2d load time install checks need the whole archive link, which the main test binary cannot take (§6.3, AD-26)
#
# the main binary links albor-render-blend2d lazily, so the load time installers stay out of it and the suites
# above control the factory hooks themselves. this second binary links the archive whole, which runs both
# installers at load, and asserts that a plain link of the backend leaves a process that renders (§5.3)
add_executable(albor-blend2d-auto-checks "./conditional/blend2d_auto.test.cpp")
target_include_directories(albor-blend2d-auto-checks PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_features(albor-blend2d-auto-checks PRIVATE cxx_std_23)
target_link_libraries(albor-blend2d-auto-checks PRIVATE GTest::gtest_main albor-render-blend2d-auto)
gtest_discover_tests(albor-blend2d-auto-checks DISCOVERY_TIMEOUT 60)

# the window half of the install only exists where the macos presenter does, so its suite joins under apple (§5.3, AD-16)
#
# the suite builds a real foreign object for the handle, which the presenter refuses by kind, so it is an
# objective c++ source and carries arc like the other conditional platform suites (§5.3, AD-16)
if(APPLE)
    target_sources(albor-blend2d-auto-checks PRIVATE "./conditional/blend2d_window_auto.test.macos.mm")
    set_source_files_properties("./conditional/blend2d_window_auto.test.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-blend2d-auto-checks PRIVATE "-framework Foundation")
endif()

# the build-scope fixture is its own product, which the §14.3 check compiles and observes (AGENTS.md, §14.3)
add_subdirectory(scope)

# the collection fixture generates its facade, which the test binary links so the generated code is compiled (§8.1)
# the row model it repeats over arrives through albor-widgets, which albor-preview publishes (§18.3, AD-13)
add_subdirectory(vectors)
target_link_libraries(albor-tests PRIVATE albor-vector-fixture)
