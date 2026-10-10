# the developer tools, which build against the compiler and never ship in the runtime (§2.1)

# the codegen driver, which compiles a ui file into concrete facade units at build time (§11.3, AD-03a)
add_subdirectory(albor-codegen)

# the manifest glue driver, which compiles a native manifest into facade classes the application subclasses (§11.2, AD-17)
add_subdirectory(albor-manifest-glue)

# the allocation probe the test binary links, which a shipping build never carries (§2.3, AD-04)
add_subdirectory(alloc-probe)

# the metal teardown repro, which reaches a metal api, so it is apple only and every other platform excludes the
# directory without reading the file (§6.3, AD-26)
if(APPLE)
    add_subdirectory(metal_teardown_probe)
endif()
