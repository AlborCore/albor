# the automation core that exposes the running application to test tooling (§10.3)

# the portable half names no platform type, so it builds wherever the framework builds (§10.3, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# the local transport reaches the unix domain socket api, which each platform spells differently (§10.3, AD-16)
set(PLATFORM_SOURCES "")
if(APPLE)
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.macos.mm")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.linux.c++")
elseif(WIN32)
    file(GLOB_RECURSE PLATFORM_SOURCES "./*.win32.c++")
endif()

# the units are objects rather than an archive, because the transport calls the session and the session is in the
# same library. a static archive is scanned once, so an intra-archive cycle would not resolve at link time (§10.3)
add_library(albor-automation-objects OBJECT ${CPP_SOURCES} ${PLATFORM_SOURCES})

# the objects reach the project root, which is where the module includes point (§3.2)
target_include_directories(albor-automation-objects PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-automation-objects PUBLIC cxx_std_23)

# the command surface routes through the shared action object of AD-15, which is production code below this module (§10.3, §18)
target_link_libraries(albor-automation-objects PUBLIC albor-commands albor-semantics)

# the linkable name the consumers use, which carries every unit so a cycle inside the library still resolves (§10.3)
add_library(albor-automation INTERFACE)

# the object library is named so a consumer built by name rather than through all still has a rule that
# produces its units; $<TARGET_OBJECTS:> alone puts them on the link line without that dependency. an
# interface library does not forward an object library's units, so the two forms carry different halves
# and neither duplicates what the other adds (§10.3)
target_link_libraries(albor-automation INTERFACE albor-automation-objects $<TARGET_OBJECTS:albor-automation-objects>)
