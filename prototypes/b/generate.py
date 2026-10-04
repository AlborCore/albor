import argparse
import os

COMPONENT_COUNT = 500


# both suites reference the previous component as a forward-declared pointer rather than by including its
# header. a 500 deep include chain is not what either style really does: it cannot compile, and for the generated
# side it would defeat the isolation AD-03 claims, since editing one header would invalidate every includer. the
# comparison between the two styles is unaffected: the baseline stays template-heavy with the whole implementation
# visible at the include site and global monotonic ids, the generated side stays a concrete facade with an opaque
# implementation in the .cpp and component-local ids.


def generated_header(n: int) -> str:
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    if n > 0:
        lines.append(f"struct Component{n - 1:03d};")
        lines.append("")
    lines.append(f"struct Component{n:03d}")
    lines.append("{")
    lines.append(f"    Component{n:03d}();")
    lines.append(f"    ~Component{n:03d}();")
    lines.append(f"    Component{n:03d}(const Component{n:03d} &) = delete;")
    lines.append(f"    Component{n:03d} &operator=(const Component{n:03d} &) = delete;")
    lines.append("    static constexpr uint16_t kPropColor = 0;")
    lines.append("    static constexpr uint16_t kPropOpacity = 1;")
    lines.append("    static constexpr uint16_t kPropWidth = 2;")
    lines.append("    static constexpr uint16_t kPropHeight = 3;")
    lines.append("    float color() const;")
    lines.append("    float width() const;")
    lines.append("    void set_color(float v);")
    lines.append("    void set_opacity(float v);")
    lines.append("    void set_width(float v);")
    lines.append("    void set_height(float v);")
    if n > 0:
        lines.append(f"    void set_child(Component{n - 1:03d}* child);")
    lines.append("    void on_click();")
    lines.append("    void on_hover();")
    lines.append("    void on_focus();")
    lines.append("")
    lines.append("private:")
    lines.append("    struct Impl;")
    lines.append("    Impl* m_impl;")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def generated_source(n: int) -> str:
    lines = []
    lines.append(f'#include "Component{n:03d}.h"')
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append(f"struct Component{n:03d}::Impl")
    lines.append("{")
    # the runtime base offset every local PropertyId is added to at registration (AD-04a)
    lines.append("    uint32_t m_base_offset = 0;")
    lines.append("    float m_color = 0;")
    lines.append("    float m_opacity = 1;")
    lines.append("    float m_width = 0;")
    lines.append("    float m_height = 0;")
    if n > 0:
        lines.append(f"    Component{n - 1:03d}* m_child = nullptr;")
    lines.append("};")
    lines.append("")
    # the opaque implementation is created and destroyed here, never in the header (AD-03)
    lines.append(f"Component{n:03d}::Component{n:03d}() : m_impl(new Impl) {{}}")
    lines.append(f"Component{n:03d}::~Component{n:03d}() {{ delete m_impl; }}")
    lines.append("")
    lines.append(f"float Component{n:03d}::color() const {{ return m_impl->m_color; }}")
    lines.append(f"float Component{n:03d}::width() const {{ return m_impl->m_width; }}")
    lines.append(f"void Component{n:03d}::set_color(float v) {{ m_impl->m_color = v; }}")
    lines.append(f"void Component{n:03d}::set_opacity(float v) {{ m_impl->m_opacity = v; }}")
    lines.append(f"void Component{n:03d}::set_width(float v) {{ m_impl->m_width = v; }}")
    lines.append(f"void Component{n:03d}::set_height(float v) {{ m_impl->m_height = v; }}")
    if n > 0:
        lines.append(f"void Component{n:03d}::set_child(Component{n - 1:03d}* child) {{ m_impl->m_child = child; }}")
    lines.append(f"void Component{n:03d}::on_click() {{}}")
    lines.append(f"void Component{n:03d}::on_hover() {{}}")
    lines.append(f"void Component{n:03d}::on_focus() {{}}")
    lines.append("")
    return "\n".join(lines)


def baseline_header(n: int) -> str:
    lines = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    if n > 0:
        # the baseline components are class templates, so this forward declaration has to be a template
        # declaration as well or the child reference below names a different kind of symbol
        lines.append("template <typename Derived>")
        lines.append(f"struct Component{n - 1:03d};")
        lines.append("")
    # global monotonic PropertyId constants, the scheme AD-04a replaces
    lines.append(f"static constexpr int kProp{n:03d}_Color = {n} * 4 + 0;")
    lines.append(f"static constexpr int kProp{n:03d}_Opacity = {n} * 4 + 1;")
    lines.append(f"static constexpr int kProp{n:03d}_Width = {n} * 4 + 2;")
    lines.append(f"static constexpr int kProp{n:03d}_Height = {n} * 4 + 3;")
    lines.append("")
    # the whole implementation is visible here, at every include site: this is the cost the baseline represents
    lines.append("template <typename Derived>")
    lines.append(f"struct Component{n:03d}")
    lines.append("{")
    lines.append("    float m_color = 0;")
    lines.append("    float m_opacity = 1;")
    lines.append("    float m_width = 0;")
    lines.append("    float m_height = 0;")
    if n > 0:
        lines.append(f"    Component{n - 1:03d}<Derived>* m_child = nullptr;")
    lines.append("    float color() const { return m_color; }")
    lines.append("    float width() const { return m_width; }")
    lines.append("    void set_color(float v) { m_color = v; }")
    lines.append("    void set_opacity(float v) { m_opacity = v; }")
    lines.append("    void set_width(float v) { m_width = v; }")
    lines.append("    void set_height(float v) { m_height = v; }")
    if n > 0:
        lines.append(f"    void set_child(Component{n - 1:03d}<Derived>* child) {{ m_child = child; }}")
    lines.append("    void on_click() {}")
    lines.append("    void on_hover() {}")
    lines.append("    void on_focus() {}")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path: str, content: str) -> bool:
    # skip the write when the file already has identical content, so an unchanged run cannot restamp a file and
    # trigger a rebuild that should not have happened (§14.2)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            existing = f.read()
        if existing == content:
            return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="generate prototype B component files")
    parser.add_argument("--out-dir", required=True, help="output directory for generated files")
    parser.add_argument("--style", required=True, choices=["generated", "baseline"], help="code style to generate")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    written = 0
    for n in range(COMPONENT_COUNT):
        if args.style == "generated":
            header_path = os.path.join(args.out_dir, f"Component{n:03d}.h")
            source_path = os.path.join(args.out_dir, f"Component{n:03d}.cpp")
            if write_if_changed(header_path, generated_header(n)):
                written += 1
            if write_if_changed(source_path, generated_source(n)):
                written += 1
        else:
            header_path = os.path.join(args.out_dir, f"Component{n:03d}.h")
            if write_if_changed(header_path, baseline_header(n)):
                written += 1

    print(f"files written: {written}")


main()