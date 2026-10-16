# Properties

A property is a declared piece of typed state of a component. It is the only state a
`.ui` file declares, the only thing a binding reads or writes, and the only thing the
generated facade publishes to C++.

## Declaration

```text
component Form {
    in property<string> name: "World";      // declared with a default
    in property<bool> armed: false;
    in property<float> level: 0.5;
    inout property<int> presses: 0;         // declared without one

    Column {
        Label { text: "Hello, " + name; }
        Button {
            text: "press";
            on_clicked: { presses = presses + 1; }
        }
    }
}
```

The shape is `direction property<type> name;` or `direction property<type> name: default;`.

## Directions

The language has two directions:

- **`in`** — an input. The host (or a parent's assignment) fills it; the component reads it.
  A handler inside the component may **not** write it — a binding owns it, and an
  imperative write would fight the binding every frame.
- **`inout`** — state the component owns. Handlers may assign it, mutate its vector in place
  or write one of its record fields, and the host may read and write it through the facade.

!!! note "Why `in` is read-only inside"
    An `in` property is usually the target of an assignment-with-source — an eager binding
    whose write lands on the setter. If a handler could assign the same property, the last
    writer would win silently. The direction keeps one owner per property.

## Defaults

A default is an ordinary expression of the file's scope:

```text
in property<string> title: "Untitled";
in property<float> scale: 0.5;
in property<Frame> frame: Frame { width: 16.0, height: 9.0 };   // a record construction
```

A property with no default is default-initialised (zero, empty string, false). A default may
read other properties of the same component — the dependency is tracked like any binding —
and, in a derived component, inherited names ([inheritance](components.md#inheritance)).

## Types

| written type | meaning |
| --- | --- |
| `bool` | a truth value |
| `int` | a signed integer; never widens to float on its own |
| `float` | a floating-point number; never narrows to int on its own |
| `string` | text |
| `color` | an RGBA colour |
| `length` | a length literal (`12px` logical or `12phx` physical, see [Layout](../concepts/layout.md)) |
| `align` | a member of the `Align` value set |
| `scrollbar` | a member of the `Scrollbar` value set |
| `command` | a reference to a shared command object ([Commands](../concepts/commands.md)) |
| `vector<T>` | an ordered collection of `T` |
| `Snapshot<T>` | an immutable shared value handed across threads |
| a declared `struct` / `enum` | a record or value set the file declared ([Types](types.md)) |
| a manifest type | a native component the catalog holds ([Native components](../concepts/native-components.md)) |

Nothing widens implicitly: a `float` does not become an `int`, an `int` does not become a
`float`. Convert explicitly — see [Expressions and bindings](expressions.md#conversions).

## Signals

A signal is a declared event, not state:

```text
signal run_clicked();
signal frame_changed(float time);
signal closed_by(float time, string reason);
```

Parameters carry a type and a name; the name is what a handler body refers to. A signal
crosses no property and stores nothing — it is raised, interested parties hear it, and the
frame moves on. Subscribing and raising are covered on
[Signals and handlers](handlers.md).

## What the facade exposes

For each declared property the generated header carries accessors over the native type:

```cpp
// .ui:  in property<string> user_name;   /   inout property<int> click_count;
panel->set_user_name("Ada");             // the setter
const std::string& name = panel->user_name();   // the getter (string returns a reference)
panel->set_click_count(panel->click_count() + 1);
```

For each declared signal the header carries a slot:

```cpp
panel->set_on_run_clicked([] { /* ... */ });
```

These are plain members of a concrete class — no virtual dispatch, no name lookup at
runtime. The facade is the whole C++ surface of the component; what sits behind it is
private and can change without breaking callers.
