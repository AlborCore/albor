# the unit tests of every module, which mirror the source tree layout (STYLE-GUIDE.md)

# gather cpp sources (all platforms)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

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

# the tests link the framework modules and nothing platform specific, so they run without a window server (§3.2)
#
# only the modules that are not already re-exported are named here. albor-widgets, albor-input and
# albor-application publish their own dependencies publicly, because their public headers name them, so
# naming the leaves again would reach the linker twice and produce a duplicate library warning. Their
# closure already carries albor-core, albor-window, albor-runtime, albor-render, albor-animation,
# albor-text and albor-ui (§3.2)
#
# albor-layout and albor-focus are named explicitly because nothing else re-exports them: a tree is built
# out of the containers, but no module header names one, so the tests must reach for them (§8.2, §8.3)
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
target_link_libraries(albor-tests PRIVATE albor-layout albor-focus albor-widgets albor-style albor-input albor-render-software albor-platform-headless albor-text-skia albor-application albor-manifest albor-alloc-probe GTest::gtest_main)

# the suite that draws through the skia render context reaches that backend directly, which is the one module the
# other suites never link because it reaches a platform surface (§3.2, AD-16). the backend is apple only, so the
# suite is gathered and linked on apple platforms alone, and the raster canvas it draws into needs no window
# server, so the binary still runs headless (§2.2)
if(APPLE)
    file(GLOB_RECURSE APPLE_CPP_SOURCES "./render_skia.test.cpp")
    target_sources(albor-tests PRIVATE ${APPLE_CPP_SOURCES})
    target_link_libraries(albor-tests PRIVATE albor-render-skia)
endif()


# the test binary is executed directly rather than through ctest, so it never needs a display (§2.2)
include(GoogleTest)
gtest_discover_tests(albor-tests DISCOVERY_TIMEOUT 60)

# the build-scope fixture is its own product, which the §14.3 check compiles and observes (AGENTS.md, §14.3)
add_subdirectory(scope)
