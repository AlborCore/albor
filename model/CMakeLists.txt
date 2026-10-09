# the model views that materialize keyed rows, which is what a repeater owns (§18.1, AD-13)

# the view is a header template, so the module carries only the item key it is built on (§18.1, AD-13)
add_library(albor-model STATIC "item_key.cpp")

# the module needs the ui tree, because a view owns the rows it materializes (§8.1, §18.1)
target_link_libraries(albor-model PUBLIC albor-ui)
