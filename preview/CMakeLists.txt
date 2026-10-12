# the standalone preview host that shows a ui file without a build step (§12.2)

# the sources name the compiler front end and the real widgets, which is the point of a preview (§12.2)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# the units are objects rather than an archive, because the host drives the compiler and the widgets at once. a
# static archive is scanned once, so a consumer linking both would take each module twice (§12.2)
add_library(albor-preview-objects OBJECT ${CPP_SOURCES})

# the objects reach the project root, which is where the module includes point (§3.2)
target_include_directories(albor-preview-objects PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-preview-objects PUBLIC cxx_std_23)

# the host drives the compiler and the widgets, so it links them (§12.2)
target_link_libraries(albor-preview-objects PUBLIC albor-compiler albor-widgets albor-layout albor-text albor-i18n albor-runtime)

# the linkable name the consumers use, which carries every unit so no module reaches the line twice (§12.2)
add_library(albor-preview INTERFACE)

# the object library is named so a consumer built by name rather than through all still has a rule that
# produces its units; $<TARGET_OBJECTS:> alone puts them on the link line without that dependency. an
# interface library does not forward an object library's units, so the two forms carry different halves
# and neither duplicates what the other adds (§12.2)
target_link_libraries(albor-preview INTERFACE albor-preview-objects $<TARGET_OBJECTS:albor-preview-objects>)
