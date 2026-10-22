# embed the committed metal library into a c++ source, because a static archive cannot carry a resource
# the source is written only when its content changes, and touched otherwise so the build converges (14.2)
# invoked as: cmake -DINPUT=<metallib> -DOUTPUT=<cpp> -P embed-metallib.cmake

# every byte is read as two hex digits, because cmake carries no binary reader
file(READ "${INPUT}" HEX_CONTENT HEX)

# the byte count is the half of the hex length, which names the span the library hands out
string(LENGTH "${HEX_CONTENT}" HEX_LENGTH)
math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")

# an empty library carries no shader, so the build stops rather than embedding nothing
if(BYTE_COUNT EQUAL 0)
    message(FATAL_ERROR "embed-metallib: '${INPUT}' is empty, so the committed library carries no bytes")
endif()

# the literals are gathered sixteen bytes to a line, because cmake's regex carries no bounded repetition
set(BYTES "")
set(INDEX 0)
while(INDEX LESS HEX_LENGTH)
    math(EXPR REMAINING "${HEX_LENGTH} - ${INDEX}")
    # the last line is shorter than the sixteen the others carry
    if(REMAINING LESS 32)
        set(TAKE ${REMAINING})
    else()
        set(TAKE 32)
    endif()
    # the chunk is copied as hex and turned into byte literals on its own
    string(SUBSTRING "${HEX_CONTENT}" ${INDEX} ${TAKE} CHUNK)
    string(REGEX REPLACE "(..)" "0x\\1, " LITERALS "${CHUNK}")
    # the line ends on its last literal, because a trailing space is what a formatter rewrites
    string(REGEX REPLACE ", $" "," LITERALS "${LITERALS}")
    string(APPEND BYTES "            ${LITERALS}\n")
    math(EXPR INDEX "${INDEX} + 32")
endwhile()

# the source is rendered whole, because a partial write would leave a source that cannot compile
set(GENERATED "// generated from the committed metal library, which no build edits by hand\n\n#include \"render/render-albor/albor_shaders.h\"\n\nnamespace albor::render\n{\n    std::span<const std::byte> metal_library_data() {\n        // the byte list stays as the generator wrote it, because a formatter reflows it into one line (14.2)\n        // clang-format off\n        static const unsigned char DATA[] = {\n${BYTES}        };\n        // clang-format on\n        // the span covers every byte the file carried, which is what the loader reads\n        return std::span<const std::byte>(reinterpret_cast<const std::byte*>(DATA), ${BYTE_COUNT});\n    }\n} // namespace albor::render\n")

# the existing source is compared first, so an unchanged library rewrites nothing (14.2)
set(PREVIOUS "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" PREVIOUS)
endif()

# the source is written only when it differs from what is already there
if(NOT PREVIOUS STREQUAL GENERATED)
    file(WRITE "${OUTPUT}" "${GENERATED}")
else()
    # an unchanged source is touched, because a build system that sees it older than its inputs reruns this step
    file(TOUCH "${OUTPUT}")
endif()
