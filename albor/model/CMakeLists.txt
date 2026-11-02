# the model views that materialize keyed rows, which is what a repeater owns (§18.1, AD-13)

# the view is a header template, so the module carries the item key it is built on and the selection a list owns (§18.1, §18.3, AD-13)
add_library(albor-model STATIC "item_key.cpp" "selection_model.cpp")

# the module needs the ui tree, because a view owns the rows it materializes (§8.1, §18.1)
target_link_libraries(albor-model PUBLIC albor-ui)

# the model headers are root-relative like every module's, which this interface publishes for the install (§3.5)
target_include_directories(albor-model PUBLIC $<BUILD_INTERFACE:${ALBOR_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
