# the pair is hashed the way the regeneration script hashes it
file(SHA256 "${SOURCE}" ACTUAL_SOURCE)
file(SHA256 "${ARTIFACT}" ACTUAL_ARTIFACT)

# the command that regenerates this family, which the direct3d caller overrides for its own mode
# a metal caller leaves it unset, because the script's default mode is the one that writes the metallib
if(NOT DEFINED REGENERATE)
    set(REGENERATE "--write")
endif()

# the stamp carries the digest of the source and of the artifact it was built into
file(STRINGS "${STAMP}" LINES)

# a stamp without exactly two lines names no pair, which is a corrupt checkout rather than a stale shader
list(LENGTH LINES STAMP_LINES)
if(NOT STAMP_LINES EQUAL 2)
    message(FATAL_ERROR "render-albor: ${STAMP} must carry the source and the artifact digest, regenerate it with: bash build/shaders.sh ${REGENERATE}")
endif()

# the first line names the source and the second the artifact
list(GET LINES 0 EXPECTED_SOURCE)
list(GET LINES 1 EXPECTED_ARTIFACT)

# either digest changing means the pair no longer matches, which must not ship
if(NOT ACTUAL_SOURCE STREQUAL EXPECTED_SOURCE OR NOT ACTUAL_ARTIFACT STREQUAL EXPECTED_ARTIFACT)
    message(FATAL_ERROR "render-albor: ${ARTIFACT} is stale against ${SOURCE}, regenerate it with: bash build/shaders.sh ${REGENERATE}")
endif()
