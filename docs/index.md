# Albor

**A cross-platform declarative UI framework for C++.**

Albor compiles a compact `.ui` declarative language directly to native C++ — no interpreter,
no reflection, no runtime UI evaluator in production builds. It pairs conventional desktop
controls with first-class support for high-throughput custom rendering (schematics, CAD views,
waveforms, plots), real multi-window behavior with modality, and a two-stage render pipeline
that never blocks the UI thread on drawing.

This guide is the user-facing documentation of the framework. It grows with the project:
every new feature lands here alongside its code.

!!! note "Status"
    Albor is under heavy development. macOS (AppKit + Metal) is the implemented platform
    backend; Windows and Linux follow. See [Status and platforms](status.md) for what ships
    today, and the repository `README.md` for the same table at the source.

## How this guide is organised

- **[Getting started](getting-started.md)** — build the repository, wire a `.ui` file into a
  CMake target, open the first window.
- **[Consuming Albor](consuming.md)** — link the framework from another project, choose a
  renderer, and use the advanced composition targets.
- **[The .ui language](ui-language/index.md)** — the complete language: components,
  properties, expressions, types, handlers, control flow.
- **Concepts** — the cross-cutting machinery:
  [properties and bindings](concepts/bindings.md),
  [commands](concepts/commands.md),
  [layout](concepts/layout.md),
  [rendering](concepts/rendering.md),
  [style and theming](concepts/theming.md),
  [internationalization](concepts/i18n.md),
  [native components](concepts/native-components.md).
- **Components** — one section per element the language ships: containers
  ([Column](components/column.md), [Row](components/row.md),
  [Padding](components/padding.md), [Stack](components/stack.md),
  [Grid](components/grid.md), [ScrollView](components/scroll-view.md),
  [ListBox](components/list-box.md)) and leaves
  ([Label](components/label.md), [Button](components/button.md),
  [TextField](components/text-field.md), [Slider](components/slider.md),
  [CheckBox](components/checkbox.md), [Canvas](components/canvas.md)).
- **[Tooling](tooling.md)** — the codegen driver, the dev runtime, the language server,
  automation and how they are wired into a build.
- **[Status and platforms](status.md)** — what is implemented, what is planned.

## A taste

```text
component GreetingPanel {
    in property<string> user_name: "World";
    inout property<int> click_count: 0;

    signal run_clicked();

    Column {
        spacing: 12px;
        padding: 16px;

        Label {
            text: @tr("Hello, ") + user_name + "!";
            font_size: 18px;
        }

        Button {
            id: increment_button;
            text: @tr("Increment");
            on_clicked: {
                click_count = click_count + 1;
                run_clicked();
            }
        }

        Label {
            text: @tr("Count: ") + string(click_count);
        }
    }
}
```

This compiles to a concrete C++ facade class — properties, signals and children as a stable,
non-virtual interface — that ordinary C++ owns and drives:

```cpp
albor::GreetingPanel* panel = nullptr;
const albor::WindowHandle handle = application.create_window(options, panel);

panel->set_on_run_clicked([] { /* react to the signal */ });
```

Start with [Getting started](getting-started.md), or jump straight into
[The .ui language](ui-language/index.md).
