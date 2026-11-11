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

- **properties** use the same direction, type and default syntax as `.ui`; the glue
  translates the scalar types, `Snapshot<T>` and `vector<T>`
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
    choices: vector<string>;   // a row may carry its own choices
}
```

- A field named **`key`** carries the record's identity. That is what a virtualized
  `Repeat` keys its rows by — the identity a background update correlates against
  ([Control flow](../ui-language/control-flow.md#virtualized-scrolling)).
- A field may be any manifest scalar, `Snapshot<T>`, or a collection such as
  `vector<string>` (nested collections included). A collection field becomes a
  `std::vector<T>` member of the generated header, with the headers it needs.
- A repeat row reads a collection field like any other field, so the row can carry the
  options its editor shows:

  ```text
  Repeat(option in options) {
      ComboBox { items: option.choices; }
  }
  ```

- A collection default is the empty list, `[]`; a non-empty list default is refused with
  `E0310` because the generator does not quote element values yet.
- Defaults are ordinary values; omitted fields are value-initialised.

## Using a native component

A native component is an element like any built-in widget. A `.ui` file names the manifest
module and writes the declared class:

```text
// panel.ui
import chart;

component Panel {
    WaveformView {
        line_color: #00FF00;

        on_selection_changed: {
            // ...
        }
    }
}
```

The generated facade header declares a free factory, which the application defines:

```cpp
// chart/waveform_view.h — generated
[[nodiscard]] std::unique_ptr<WaveformView> create_WaveformView(BindingEngine* engine);

// waveform_widget.cpp — application
std::unique_ptr<WaveformView> create_WaveformView(BindingEngine* engine) {
    return std::make_unique<WaveformWidget>(engine);
}
```

Generated `.ui` code calls the factory in every construction path — root, child, conditional
body and repeat row — so the generated body never names the concrete class. The facade stays
opaque, no runtime name registry is added to Release, and a missing definition fails at link
time. Property assignments reach the facade setters (`set_line_color`, `set_zoom`) and signal
handlers install on the facade slots (`set_on_selection_changed`), exactly as they do for a
built-in widget.

The catalog needs the manifest that declares the component. `albor-codegen` takes one
`--manifest` flag per module, and the CMake helpers pass it through:

```cmake
albor_build_native_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest" WaveformView)
albor_build_ui_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/panel.ui"
    --manifest "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest")
```

The native component is declared first, because its generated header must exist before the
`.ui` unit compiles. A `.ui` file that names a native component without passing its manifest
does not resolve the element. A native element whose catalog entry carries no construction
metadata is refused by codegen with `E0501`, because the generator cannot choose a
construction path.

## Importing

A `.ui` file names the manifest modules and `.ui` files it uses:

```text
import task;                        // the task.ui-manifest module
import "panels/side_panel.ui";      // a component from another ui file

component ScrollLog {
    inout property<vector<Task>> tasks;

    ScrollView {
        row_height: 24px;
        Repeat(task in tasks) {
            Label { text: task.title; }
        }
        SidePanel {}
    }
}
```

An unquoted import names a manifest module — the manifest file's base name — and is what a
record's generated header path is built from. A quoted import names a `.ui` file relative to
the importing file and brings its one component into scope. The file's own declared types
still resolve first: a name the file itself declares shadows one an import brings in, and an
imported component resolves before the native catalog.

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

A `.ui` file that instantiates the component also passes the manifest to the compiler, so the
element resolves and the factory is named:

```cmake
albor_build_ui_component(my-app "${CMAKE_CURRENT_SOURCE_DIR}/panel.ui"
    --manifest "${CMAKE_CURRENT_SOURCE_DIR}/chart.ui-manifest")
```

On the C++ side the application implements the class the manifest declares, defines the
`create_<Class>` factory, and may still construct the view directly as a root. The `chart`
example (`examples/chart/`) is the reference: `WaveformView` with a
`Snapshot<WaveformData>` input, a `LineBatch`-backed draw, and a `create_WaveformView`
definition.

## The rules

- **The manifest is the single source of truth.** The glue — facades, descriptor tables,
  preview stubs — is generated from it; a manifest test asserts the generated output agrees.
- **The compiler never parses C++ headers.** It reads the manifest and nothing else. That is
  what keeps a Release build free of any reflection machinery.
- **Names resolve file-first.** A type a `.ui` file declares shadows one an import or the
  catalog provides — the same precedence built-in elements follow
  ([Components](../ui-language/components.md#using-a-component-as-an-element)).
