# the minimal repro for the metal teardown defect, which exists to keep an upstream report reproducible (§6.3, AD-26)

# the file names metal and skia, so it carries the platform infix and never builds elsewhere (§6.3)
add_executable(albor-metal-teardown-probe "main.macos.mm")

# the skia headers are consumed as system headers, because their own warnings are not the tools ($3.5)
target_include_directories(albor-metal-teardown-probe SYSTEM PRIVATE ${ALBOR_SKIA_INCLUDE_ROOT})

# arc is what makes the locals release at scope exit, and without it the repro silently never deallocates anything
# and exits clean, which is the opposite of what it is there to show (§6.3)
target_compile_options(albor-metal-teardown-probe PRIVATE -fobjc-arc)

# skia supplies the ganesh api, which is the whole point of the repro (§6.3)
target_link_libraries(albor-metal-teardown-probe PRIVATE albor-skia "-framework Metal" "-framework QuartzCore" "-framework Foundation")
