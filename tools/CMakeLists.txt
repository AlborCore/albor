# the developer tools, which build against the compiler and never ship in the runtime (§2.1)

# the codegen driver, which compiles a ui file into concrete facade units at build time (§11.3, AD-03a)
add_subdirectory(albor-codegen)

# the stdio language server, which is the process an editor launches to check a ui buffer (§12.2)
add_subdirectory(albor-lsp)

# the manifest glue driver, which compiles a native manifest into facade classes the application subclasses (§11.2, AD-17)
add_subdirectory(albor-manifest-glue)

# the font embedding driver, which turns declared font faces into generated registration units (§11.3, AD-34)
add_subdirectory(albor-font-embed)

# the allocation probe the test binary links, which a shipping build never carries (§2.3, AD-04)
add_subdirectory(alloc-probe)

# the metal teardown repro, which reaches a metal api, so it is apple only and every other platform excludes the
# directory without reading the file (§6.3, AD-26)
if(APPLE AND ALBOR_RENDERER_SKIA)
    add_subdirectory(metal_teardown_probe)
endif()
