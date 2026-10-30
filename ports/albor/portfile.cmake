# the albor port, which pins the source the package builds from and installs the checked-in cmake story of it (AD-19)
#
# a local pass points ALBOR_PORT_SOURCE_PATH at a checkout, which is how the test-bed exercises this exact
# port definition before a release exists; the release workflow rewrites the pinned REF below per release
# and opens its PR, so no hand edits a stamp and no tree contains a digest of itself.

if(DEFINED ENV{ALBOR_PORT_SOURCE_PATH})
    # a local pass builds the checkout itself, which is announced rather than silently adopted (AD-19)
    set(local_source "$ENV{ALBOR_PORT_SOURCE_PATH}")
    if(local_source STREQUAL "" OR NOT IS_DIRECTORY "${local_source}" OR NOT EXISTS "${local_source}/CMakeLists.txt")
        message(FATAL_ERROR "albor port: ALBOR_PORT_SOURCE_PATH points at '${local_source}', which is not a framework checkout")
    endif()
    set(SOURCE_PATH "${local_source}")
    message(STATUS "albor port: building from the local checkout ${SOURCE_PATH} (unpinned)")
else()
    # the pinned source is a main commit, which a git object carries immutably; the release automation
    # rewrites this one line per release, so no release needs a human stamp (AD-19)
    vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL "https://github.com/AlborCore/albor.git"
        REF bb1108bca3fea4b3e23100b1a693132a197bf9fb
        HEAD_REF main
    )
endif()

# the framework is only static, which a dynamic triplet's preference silently overrides without the declaration (AD-19)
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# the renderer backends ride the features, which the package passes to the checked-in cmake options (§6.3)
vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        blend2d         ALBOR_RENDERER_BLEND2D
        skia-reference  ALBOR_RENDERER_SKIA
)

# the port builds the framework and nothing else, because examples and the test binary are development surfaces (§2.2)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DALBOR_EXAMPLES=OFF
        -DALBOR_TESTS=OFF
)
vcpkg_cmake_install()

# the checked-in install story puts the config, its version file and the helpers in lib/cmake/albor, whose
# relocation the fixup performs for the debug and release prefixes vcpkg installs (AD-19)
vcpkg_cmake_config_fixup(PACKAGE_NAME albor CONFIG_PATH lib/cmake/albor)

# the codegen drivers already land in tools/albor from the checked-in install rules, which is the layout a port ships (AD-19)
# the debug half never ships headers, tools or the config it carried, which the fixup consumed and the port clears (AD-19)
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/debug/tools"
)

# the usage note travels with the package, which is how a consumer learns the link contract (AD-19)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/albor")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
