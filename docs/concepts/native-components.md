# Native components

A native component is a C++ class that `.ui` can instantiate like any built-in widget. It is
declared in a **`.ui-manifest`** file — a small, closed description of the surface it
exposes — and the compiler **never parses a C++ header**. The manifest is the single source
of truth; the C++ glue is generated from it.

## The manifest

```text
// chart.ui-manifest
native component WaveformView {
    in property<Snapshot<WaveformData>> data;
    in property<color> line_color: #00FF00;

    signal selection_changed(float time_pos);

    semantic role: "chart";
}
```

- **properties** use the same direction, type and default syntax as `.ui` — including
  `Snapshot<T>`, `vector<T>` and `command`
- **signals** carry typed, named parameters, as in `.ui`
- **`semantic role`** publishes the accessibility/automation role the component reports

The class behind the manifest is ordinary C++. It implements the widget contract —
`measure`, `arrange`, `render` — and nothing about its internal structure is visible to the
compiler. What the manifest promises is what the generated facade exposes; what the facade
does not promise can change freely.

## Records

A manifest also declares plain records — data, not widgets — which `.ui` can use as property
types and as repeat element types:

```text
// task.ui-manifest
record Task {
    key: string;          // the row identity — a field named key
    title: string;
    done: bool = false;   // a field may carry a default
}
```

- A field named **`key`** carries the record's identity. That is what a virtualized
  `Repeat` keys its rows by — the identity a background update correlates against
  ([Control flow](../ui-language/control-flow.md#virtualized-scrolling)).
- Defaults are ordinary values; omitted fields are value-initialised.

## Importing

A `.ui` file names the manifest modules it uses:

```text
import task;      // the task.ui-manifest module

component ScrollLog {
    inout property<vector<Task>> tasks;

    ScrollView {
        row_height: 24px;
        Repeat(task in tasks) {
            Label { text: task.title; }
        }
    }
}
```

An import names a manifest module — the manifest file's base name — and is what a record's
generated header path is built from. The file's own declared types still resolve first: a
name the file itself declares shadows one an import brings in.

## Wiring the build

Two helpers generate the glue and wire it into a target:

```cmake
# record headers from the manifest (Task, ...)
albor_build_native_record(my-app "${CMAKE_CURRENT_SOURCE_DIR}/task.ui-manifest")

# the facade units for one native component class
albor_build_native_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest" WaveformView)
```

Both run the manifest glue driver as a build step, write the generated units under the build
directory, and join the target's sources and include path — the same pattern
`albor_build_ui_component` uses for `.ui` files
([Getting started](../getting-started.md#a-component-from-a-ui-file)).

On the C++ side the application implements the class the manifest declares and constructs it
inside its tree. The `chart` example (`examples/chart/`) is the reference: `WaveformView`
with a `Snapshot<WaveformData>` input and a `LineBatch`-backed draw.

## The rules

- **The manifest is the single source of truth.** The glue — facades, descriptor tables,
  preview stubs — is generated from it; a manifest test asserts the generated output agrees.
- **The compiler never parses C++ headers.** It reads the manifest and nothing else. That is
  what keeps a Release build free of any reflection machinery.
- **Names resolve file-first.** A type a `.ui` file declares shadows one an import or the
  catalog provides — the same precedence built-in elements follow
  ([Components](../ui-language/components.md#using-a-component-as-an-element)).
