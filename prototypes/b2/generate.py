#!/usr/bin/env python3
"""Generate the Prototype B2 suites (§30, AD-03).

One template emits every variant so the suites differ only in the axis under test.
Generated sources are written only when their content changes, so a measurement run
never invalidates a timestamp it did not mean to (§14.2).
"""

import argparse
import pathlib
import sys

# the component count every suite uses, matching Prototype B (§30)
COMPONENT_COUNT = 500

# the properties one generated component declares, matching Prototype B (§13.3)
PROPERTY_COUNT = 4

# a component may not declare more properties than a uint16_t local id can address (§13.3)
MAX_LOCAL_PROPERTIES = 65535

# the runtime header a precompiled header is built from (§14.3)
PCH_SOURCE = "pch_source.h"

# the entry point the link measurement needs; it is not part of any component count (§28.2)
MAIN_SOURCE = "suite_main.cpp"


class GenerationError(Exception):
    """A request the generator must refuse rather than silently mis-generate."""


def component_name(index: int) -> str:
    """Return the generated component name for an index."""
    return f"Component{index:03d}"


def emit_header(index: int, minimal_include: bool) -> str:
    """Return the generated facade header for one component.

    `minimal_include` selects the M3 axis: forward-declare the integer width instead
    of including <cstdint>, which is the only standard header the generated style uses.
    """
    # the include and the integer spelling are the only things M3 changes (§14.3)
    prelude = "" if minimal_include else "#include <cstdint>\n\n"
    width = "unsigned short" if minimal_include else "uint16_t"
    index_width = "unsigned int" if minimal_include else "uint32_t"

    # build the property constant declarations (§13.3)
    properties = "\n".join(
        f"    static constexpr {width} kProp{slot} = {slot};" for slot in range(PROPERTY_COUNT)
    )

    # build the accessor declarations (§14.1)
    accessors = "\n".join(f"    float value_{slot}() const;" for slot in range(PROPERTY_COUNT))
    setters = "\n".join(f"    void set_value_{slot}(float v);" for slot in range(PROPERTY_COUNT))

    return f"""#pragma once

{prelude}struct {component_name(index)}
{{
    {component_name(index)}();
    ~{component_name(index)}();
    {component_name(index)}(const {component_name(index)}&) = delete;
    {component_name(index)}& operator=(const {component_name(index)}&) = delete;
{properties}
{accessors}
{setters}

private:
    struct Impl;
    Impl* m_impl;
    {index_width} m_base_offset = 0;
}};
"""


def emit_body(index: int, minimal_include: bool) -> str:
    """Return the generated implementation for one component."""
    # the include and the integer spelling are the only things M3 changes (§14.3)
    prelude = "" if minimal_include else "#include <cstdint>\n\n"
    index_width = "unsigned int" if minimal_include else "uint32_t"

    # build the opaque implementation body (§14.1)
    fields = "\n".join(f"    float m_value_{slot} = {slot}.0f;" for slot in range(PROPERTY_COUNT))

    # build the accessor definitions (§14.1)
    getters = "\n".join(
        f"float {component_name(index)}::value_{slot}() const {{ return m_impl->m_value_{slot}; }}"
        for slot in range(PROPERTY_COUNT)
    )
    setters = "\n".join(
        f"void {component_name(index)}::set_value_{slot}(float v) {{ m_impl->m_value_{slot} = v; }}"
        for slot in range(PROPERTY_COUNT)
    )

    return f"""#include "{component_name(index)}.h"

{prelude}struct {component_name(index)}::Impl
{{
{fields}
}};

{component_name(index)}::{component_name(index)}() :
    m_impl(new Impl) {{}}
{component_name(index)}::~{component_name(index)}() {{ delete m_impl; }}

{getters}

{setters}
"""


def emit_chunk(first: int, count: int) -> str:
    """Return one unity translation unit holding `count` components.

    The M4 axis: a chunk includes each member's header and defines each member's
    bodies, so a body edit recompiles exactly one translation unit holding `count`
    components.
    """
    # the includes every member header needs (§14.3)
    includes = "\n".join(f'#include "{component_name(index)}.h"' for index in range(first, first + count))

    # the opaque implementations, one per member (§14.1)
    impls = []
    getters = []
    setters = []
    for index in range(first, first + count):
        fields = "\n".join(f"    float m_value_{slot} = {slot}.0f;" for slot in range(PROPERTY_COUNT))
        impls.append(f"struct {component_name(index)}::Impl\n{{\n{fields}\n}};")
        getters.extend(
            f"float {component_name(index)}::value_{slot}() const {{ return m_impl->m_value_{slot}; }}"
            for slot in range(PROPERTY_COUNT)
        )
        setters.extend(
            f"void {component_name(index)}::set_value_{slot}(float v) {{ m_impl->m_value_{slot} = v; }}"
            for slot in range(PROPERTY_COUNT)
        )

    ctors = []
    for index in range(first, first + count):
        ctors.append(f"{component_name(index)}::{component_name(index)}() :\n    m_impl(new Impl) {{}}")
        ctors.append(f"{component_name(index)}::~{component_name(index)}() {{ delete m_impl; }}")

    return f"""{includes}

{chr(10).join(impls)}

{chr(10).join(ctors)}

{chr(10).join(getters)}

{chr(10).join(setters)}
"""


def emit_floor_tu(index: int, minimal_include: bool) -> str:
    """Return one M1 translation unit: the include and nothing else.

    This is the fixed per-TU cost — compiler startup, the header parse and the
    declarations — with no body to generate code for.
    """
    del minimal_include
    return f'#include "{component_name(index)}.h"\n'


def write_if_changed(path: pathlib.Path, content: str) -> bool:
    """Write content only when it differs, so timestamps stay meaningful (§14.2)."""
    # a missing file is always written (§14.2)
    if not path.exists():
        path.write_text(content)
        return True

    # an unchanged file is left alone, so a rebuild is not triggered by generation (§14.2)
    if path.read_text() == content:
        return False

    # the content changed, so the file is rewritten (§14.2)
    path.write_text(content)
    return True


def generate(out_dir: pathlib.Path, variant: str, chunk: int) -> int:
    """Generate one suite and return how many files were written."""
    # every suite starts from an empty directory so a stale file cannot join a build (§14.3)
    out_dir.mkdir(parents=True, exist_ok=True)

    written = 0

    # every suite carries one entry point so the link measurement has something to link (§28.2)
    written += int(write_if_changed(out_dir / MAIN_SOURCE, "int main() { return 0; }\n"))

    # M8: a component that declares more properties than a uint16_t local id can address
    # must be refused, because the ids would wrap silently (§13.3)
    if variant == "m8":
        written += generate_over_limit(out_dir)
        return written

    # M3 is the only variant whose headers forward-declare instead of including (§14.3)
    minimal = variant == "m3"

    # M4 is the only variant that packs several components into one translation unit (§14.3)
    if variant == "m4":
        if chunk < 1:
            raise GenerationError(f"chunk size must be at least 1, got {chunk}")

        # a chunk includes its members' headers, so the headers exist alongside the chunks (§14.3)
        for index in range(COMPONENT_COUNT):
            written += int(write_if_changed(out_dir / f"{component_name(index)}.h", emit_header(index, minimal)))

        for first in range(0, COMPONENT_COUNT, chunk):
            # a trailing chunk may be short, and that is not an error (§14.3)
            count = min(chunk, COMPONENT_COUNT - first)
            target = out_dir / f"chunk_{first:03d}.cpp"
            written += int(write_if_changed(target, emit_chunk(first, count)))

        return written

    # M1 and M2 share the floor suite; M2 adds a precompiled header on top of it (§14.3)
    floor = variant in {"m1", "m2"}

    for index in range(COMPONENT_COUNT):
        written += int(write_if_changed(out_dir / f"{component_name(index)}.h", emit_header(index, minimal)))

        # M1 and M2 define no bodies, which is the whole point of the floor (§14.3)
        body = emit_floor_tu(index, minimal) if floor else emit_body(index, minimal)
        written += int(write_if_changed(out_dir / f"{component_name(index)}.cpp", body))

    # M2 needs the header a precompiled header is built from (§14.3)
    if variant == "m2":
        prelude = "" if minimal else "#include <cstdint>\n"
        written += int(write_if_changed(out_dir / PCH_SOURCE, f"{prelude}\n"))

    return written


def generate_over_limit(out_dir: pathlib.Path) -> int:
    """Emit a component that declares one property too many, and refuse to generate it.

    T2.6 requires a clear error naming both the limit and the count, because a
    uint16_t local id cannot address 65536 properties and a silent wrap would
    corrupt every property id in the component (§13.3).
    """
    requested = MAX_LOCAL_PROPERTIES + 1

    # the generator refuses rather than emitting an id space that wraps (§13.3)
    if requested > MAX_LOCAL_PROPERTIES:
        raise GenerationError(
            f"component declares {requested} properties but a uint16_t PropertyId addresses at most "
            f"{MAX_LOCAL_PROPERTIES}; reduce the component to {MAX_LOCAL_PROPERTIES} properties or fewer"
        )

    # unreachable while the guard above holds, and kept so the failure mode is explicit (§13.3)
    return int(write_if_changed(out_dir / "OverLimit.h", emit_header(0, False)))


def main() -> int:
    """Parse arguments, generate the requested suite and report what it wrote."""
    parser = argparse.ArgumentParser(description="generate a Prototype B2 suite")
    parser.add_argument("--variant", required=True, choices=["m1", "m2", "m3", "m4", "m8"])
    parser.add_argument("--chunk", type=int, default=1, help="components per translation unit for m4")
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    try:
        written = generate(pathlib.Path(args.out_dir), args.variant, args.chunk)
    except GenerationError as error:
        # the refusal is the measured behaviour for M8, so it goes to stderr with a non-zero status (§28.4)
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"variant {args.variant} chunk {args.chunk}: files written {written}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
