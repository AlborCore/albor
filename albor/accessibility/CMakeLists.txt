# the platform accessibility bridge that publishes the semantic tree to assistive technology (§10.2)

# the portable half names no platform type, so it builds everywhere the semantic tree does (§10.2, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# the macos bridge reaches the application kit accessibility protocol, which only exists on apple (§10.2, AD-16)
set(MACOS_SOURCES "")
if(APPLE)
    file(GLOB_RECURSE MACOS_SOURCES "./*.macos.mm")
endif()

# the windows side reports that it has no bridge until the ui automation adapter lands, and the source
# carries the win32 infix so that adapter replaces this one rather than joining it (§10.2, #42, AD-16)
set(WIN32_SOURCES "")
if(WIN32)
    file(GLOB_RECURSE WIN32_SOURCES "./*.win32.c++")
endif()

# the linux side publishes the tree to at-spi on the accessibility bus, which rides d-bus (§10.2, BDG-10)
set(LINUX_SOURCES "")
set(ALBOR_ATSPI_FOUND OFF CACHE INTERNAL "the at-spi accessibility bridge was configured" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # the bridge speaks the at-spi protocol with libdbus, which the host's development package provides (§10.2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ATSPI_DBUS_PC REQUIRED IMPORTED_TARGET GLOBAL dbus-1)
    # the source carries the linux infix, so the adapter is only compiled for the platform it targets (§5.3)
    file(GLOB_RECURSE LINUX_SOURCES "./*.linux.c++")
    # the build fact lets the suite compile the at-spi role checks only where the bridge exists (§16)
    set(ALBOR_ATSPI_FOUND ON CACHE INTERNAL "the at-spi accessibility bridge was configured" FORCE)
endif()

# create the static library from the gathered sources, which is declared once whichever platform is built (§3.2)
add_library(albor-accessibility STATIC ${CPP_SOURCES} ${MACOS_SOURCES} ${WIN32_SOURCES} ${LINUX_SOURCES})

# the bridge implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-accessibility PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(albor-accessibility PUBLIC cxx_std_20)

# the semantic tree is what the bridge publishes, so the dependency is public (AD-05, §10.1)
target_link_libraries(albor-accessibility PUBLIC albor-semantics)

# the macos sources need arc and the application kit, which only the macos half reaches (§5.3, AD-16)
if(APPLE)
    set_source_files_properties("./accessibility_bridge.macos.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(albor-accessibility PRIVATE "-framework AppKit" "-framework Foundation")
endif()

# the windows bridge reaches ui automation core and subclasses the native window, which only the win32 half does (§10.2, AD-16)
if(WIN32)
    target_link_libraries(albor-accessibility PRIVATE uiautomationcore comctl32 oleaut32)
endif()

# the at-spi bridge speaks d-bus from a dispatch thread, which only the linux half starts (§10.2, BDG-10)
if(ALBOR_ATSPI_FOUND)
    find_package(Threads REQUIRED)
    target_link_libraries(albor-accessibility PRIVATE PkgConfig::ATSPI_DBUS_PC Threads::Threads)
endif()
