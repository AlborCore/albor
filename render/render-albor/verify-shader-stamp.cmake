# the pair is hashed the way the regeneration script hashes it
file(SHA256 "${SOURCE}" ACTUAL_SOURCE)
file(SHA256 "${ARTIFACT}" ACTUAL_ARTIFACT)

# the stamp carries the digest of the source and of the artifact it was built into
file(STRINGS "${STAMP}" LINES)

# a stamp without exactly two lines names no pair, which is a corrupt checkout rather than a stale shader
list(LENGTH LINES STAMP_LINES)
if(NOT STAMP_LINES EQUAL 2)
    message(FATAL_ERROR "render-albor: ${STAMP} must carry the source and the artifact digest, regenerate it with: bash build/shaders.sh --write")
endif()

# the first line names the source and the second the artifact
list(GET LINES 0 EXPECTED_SOURCE)
list(GET LINES 1 EXPECTED_ARTIFACT)

# either digest changing means the pair no longer matches, which must not ship
if(NOT ACTUAL_SOURCE STREQUAL EXPECTED_SOURCE OR NOT ACTUAL_ARTIFACT STREQUAL EXPECTED_ARTIFACT)
    message(FATAL_ERROR "render-albor: ${ARTIFACT} is stale against ${SOURCE}, regenerate it with: bash build/shaders.sh --write")
endif()
