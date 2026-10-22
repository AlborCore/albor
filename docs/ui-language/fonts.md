# Fonts

A `.ui` file names a family and a weight; it never names a file, a platform font or a
font size in device pixels. The build carries the bytes, the engine shapes from them, and
the renderer draws the outlines. That is what makes text layout identical on every platform
(§15, AD-34).

## Declaring a selection

```text
Label {
    text: @tr("Total");
    font_family: "Inter";
    font_weight: Weight.bold;
    font_fallback: ["Albor Fallback"];
}
```

- `font_family` names a family the target carries.
- `font_weight` selects a face inside that family. The members are `Weight.thin`,
  `Weight.light`, `Weight.regular`, `Weight.medium`, `Weight.semibold`, `Weight.bold` and
  `Weight.black`.
- `font_fallback` is tried in order when the named family is absent or does not carry the
  requested weight.

The selection is exact. A family without the requested weight is a compile error when no
fallback carries it, and the run is refused at runtime when the selection is computed. The
framework does not synthesise bold and does not substitute a nearby weight.

## Carrying the faces

The build declares the files once per target:

```cmake
add_executable(my_app main.cpp)

albor_build_font_assets(my_app
    DEFAULT Inter
    FONTS
        Inter regular "assets/Inter-Regular.ttf"
        Inter bold    "assets/Inter-Bold.ttf")

albor_build_ui_component(my_app "main.ui")
```

`albor_build_font_assets` must be called before `albor_build_ui_component`, because the
generated facade is checked against the same list. The helper generates one registration
unit per target, so two components never embed the same font twice. The target must link a
text engine (`albor-text-harfbuzz-auto`), which publishes the font registry the generated
unit calls.

The generated header names the default family and exposes the installer:

```cpp
#include <string>

#include "albor_embedded_fonts.h"
#include "text/text_engine.h"

int main() {
    albor::set_installed_font_family(std::string(albor::generated::DEFAULT_FAMILY));
    // create the window and run
}
```

The registration runs at load. The explicit `set_installed_font_family` call names the
family a run without `font_family` resolves; do it before the first window is created.
`albor::generated::install_embedded_fonts()` is available when a static-library consumer
needs to force registration. Using `DEFAULT_FAMILY` is enough to pull the generated object
out of a static archive, because the constant calls the installer.

A binary links one generated font unit. Two targets that both declare fonts and are linked
into one image collide on the generated names, so give each such target its own executable
or consolidate the faces into one target.

## Requirements

- The default family must be declared and must carry `regular`, because a bare family name resolves it.
- A non-default family may carry only the weights a selection names.
- The same family and weight cannot be declared twice.
- A face must be an sfnt (TrueType, OpenType, TrueType collection or Apple TrueType).
- The same file declared under two weights of one family is a warning: the engine does not
  instance variable fonts, so both weights would shape identically.

A target that declares no fonts keeps the platform font path. That path exists for
transitional applications; it is not the identity path of AD-34.

## Diagnostics

The build driver checks a `.ui` against the target's font asset list. The LSP and the
preview do not receive that list yet, so the font diagnostics appear in the build rather
than in the editor; the build is authoritative for this surface.

| code | meaning |
| --- | --- |
| `E0510` | the named family is not carried and no fallback can supply it |
| `E0511` | the family has no requested weight and no fallback can supply it |
| `E0512` | the default family is missing, not carried, or has no regular face |
| `W0513` | a fallback family is not carried or lacks the requested weight |
| `E0701` | duplicate family and weight in the font asset list |
| `E0702` | a face declares no family |
| `E0703` | a face carries no bytes |
| `E0704` | a face is not an sfnt |
| `E0705` | the default family is missing or not declared |
| `E0706` | the default family carries no regular face |
| `E0708` | a font file cannot be read |
| `E0709` | the target declares no face (library validation) |
| `E0710` | a family or default carries a control byte |
| `W0707` | two weights of one family use the same file |
