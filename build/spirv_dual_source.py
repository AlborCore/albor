#!/usr/bin/env python3
"""Rewrite the dual source output of the Vulkan exact fragment.

glslang lowers the HLSL ``SV_Target1`` semantic to ``Location 1``, which is a
second colour attachment. Vulkan dual-source blending instead requires the
second fragment output at ``Location 0`` with ``Index 1`` (§6.2, AD-05), which
is the decoration the direct3d front end already gives the same source.

The rewrite touches one module, moves one decoration and adds one, and it fails
when the pattern it expects is absent, so a glslang change is caught by the
shader build rather than by a validation error at run time.
"""

import struct
import sys

# the spirv words the rewrite reads, from the core specification's decoration section
HEADER_WORDS = 5
OP_DECORATE = 71
OP_VARIABLE = 59
STORAGE_CLASS_OUTPUT = 3
DECORATION_LOCATION = 30
DECORATION_INDEX = 32

# the second fragment output the exact stage declares, which the hlsl semantic names
SECOND_OUTPUT_LOCATION = 1


def fail(message):
    """report a module the rewrite cannot prove, which the build refuses"""
    print(f"spirv_dual_source: error: {message}", file=sys.stderr)
    sys.exit(1)


def split_instructions(words):
    """split the word stream into instructions, keeping each one's word count"""
    instructions = []
    offset = HEADER_WORDS
    while offset < len(words):
        count = words[offset] >> 16
        if count == 0:
            fail("an instruction carries no word count")
        instructions.append(words[offset:offset + count])
        offset += count
    if offset != len(words):
        fail("the last instruction runs past the module")
    return instructions


def output_variables(instructions):
    """collect the result ids of every output variable, which the rewrite distinguishes from inputs"""
    outputs = set()
    for instruction in instructions:
        # an op variable names its result type, its result id and the storage class it lives in
        if instruction[0] & 0xFFFF != OP_VARIABLE or len(instruction) < 4:
            continue
        if instruction[3] == STORAGE_CLASS_OUTPUT:
            outputs.add(instruction[2])
    return outputs


def main():
    """rewrite one module in place, which the shader build calls per artifact"""
    if len(sys.argv) != 2:
        fail("usage: spirv_dual_source.py <module.spv>")
    path = sys.argv[1]
    with open(path, "rb") as source:
        data = source.read()
    if len(data) % 4 != 0 or len(data) < HEADER_WORDS * 4:
        fail(f"{path} is not a spirv module")
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    if words[0] != 0x07230203:
        fail(f"{path} does not carry the spirv magic")
    instructions = split_instructions(words)
    outputs = output_variables(instructions)
    # the output the hlsl semantic placed at the second location is the one the rewrite owns
    second = None
    indexed = set()
    for position, instruction in enumerate(instructions):
        if instruction[0] & 0xFFFF != OP_DECORATE or len(instruction) < 4:
            continue
        target = instruction[1]
        decoration = instruction[2]
        literal = instruction[3]
        if decoration == DECORATION_INDEX:
            indexed.add(target)
        elif decoration == DECORATION_LOCATION and literal == SECOND_OUTPUT_LOCATION and target in outputs:
            if second is not None:
                fail(f"{path} names more than one output at location {SECOND_OUTPUT_LOCATION}")
            second = (position, target)
    if second is None:
        fail(f"{path} carries no output at location {SECOND_OUTPUT_LOCATION}")
    position, target = second
    if target in indexed:
        fail(f"{path} already indexes the output, which the rewrite must not repeat")
    # the output moves onto the first location, and the index makes it the second blend source
    instructions[position][3] = 0
    instructions.insert(position + 1, [4 << 16 | OP_DECORATE, target, DECORATION_INDEX, 1])
    rewritten = words[:HEADER_WORDS]
    for instruction in instructions:
        rewritten.extend(instruction)
    with open(path, "wb") as rewritten_file:
        rewritten_file.write(struct.pack(f"<{len(rewritten)}I", *rewritten))
    print(f"spirv_dual_source: {path} moves {target} to location 0 index 1")


if __name__ == "__main__":
    main()
