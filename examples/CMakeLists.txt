# the example applications of the phase 1 and phase 2 deliverables (§14)

add_subdirectory(hello-window)

# the reactive counter, which shows the reactive engine driving the frame pipeline (§14, phase 2)
add_subdirectory(reactive-counter)

# the compiled ui greeting, which shows a component generated from a ui file at build time (§14, phase 4)
add_subdirectory(greeting)

# the manifest chart, which shows a native facade generated from a manifest and implemented by the application (§14, phase 4)
add_subdirectory(chart)

# the widget form, which exercises every widget the framework ships and shows how a composition root links its backends (§14, §5.3)
add_subdirectory(form)
