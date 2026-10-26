# Types

Beyond the scalar set (`bool`, `int`, `float`, `string`, `color`, `length`, `image`), a `.ui` file
declares its own compound types: records and value sets. Both compile to native C++; neither
needs a manifest entry.

## Structs: records

```text
struct Frame {
    float width;
    float height;
}

component Plot {
    inout property<Frame> frame;

    signal frame_changed(Frame frame);     // a record crosses a signal by value
}
```

- A record is a plain value: fields in declaration order, no methods, no inheritance.
- A field of a record-typed `inout` property is written in place from a handler
  (`frame.width = 10.0;`): the working copy is taken, the field is assigned, the rebuilt
  record is set back.
- A record crosses a signal **by value**.
- Construction is the form shown in
  [Expressions and bindings](expressions.md#record-construction): named fields, omitted
  fields value-initialised.

The compiler emits the record as a guarded native struct, and a `.ui`-declared record
resolves **before** the manifest catalog is consulted — a file's own names always win.

!!! note "Records that cross threads"
    Application data that reaches a custom node or a binding from a worker thread does not
    cross as a mutable record: it crosses as a `Snapshot<T>` — see
    [Properties and bindings](../concepts/bindings.md#crossing-threads).

## Enums: value sets

```text
enum Status { Todo, Doing, Done }

component Task {
    in property<Status> status;

    Label { text: string(status == Status.Done); }
}
```

A value set enumerates named members. A member is written `Set.member` — `Status.Done`,
`Align.center`, `Scrollbar.as_needed` — and resolves against the set the context expects. An
unknown member or a member of the wrong set is a compile error that lists the closed set.

## The built-in value sets

Two value sets ship with the language, and their members are what widget alignment and
scrollbar policies are written with:

| set | members | used by |
| --- | --- | --- |
| `Align` | `start`, `center`, `end`, `stretch` | `horizontal_align`, `vertical_align` on every element |
| `Scrollbar` | `as_needed`, `off`, `on` | `Scrollbar` policy on [ScrollView](../components/scroll-view.md) |

```text
Label { horizontal_align: Align.stretch; }
ScrollView { scrollbar: Scrollbar.off; }
```

`stretch` is the spacer idiom: a child that declares no natural size absorbs the leftover
space on its axis. [Layout](../concepts/layout.md) walks through the patterns.

## Collections

```text
inout property<vector<string>> items;
inout property<vector<Task>> tasks;
```

`vector<T>` is an ordered collection of one element type. Handlers mutate it **in place**
with the closed method set — `append`, `remove_at`, `clear`, `set`
([Signals and handlers](handlers.md#built-in-methods)) — and `Repeat` builds rows over it
([Control flow](control-flow.md#repeat-rows-over-a-collection)).

## Snapshot

```text
in property<Snapshot<WaveformData>> data;
```

`Snapshot<T>` is an immutable shared value — `std::shared_ptr<const T>` — handed across
threads. A background worker publishes a new snapshot through an observable; the binding on
the UI thread reads it without locking and without the worker ever touching a live UI
object. It is the only way application data crosses the thread boundary.

## Commands

```text
in property<command> save;
```

A `command` property is a reference to a shared command object the application owns — one
object observed by every consumer, carrying label, enablement and the action itself. Bind it
to a control's `action:` or fire it from a handler with `save.invoke();`. See
[Commands](../concepts/commands.md).

## Images

```text
in property<image> logo;
```

An `image` is a retained handle to decoded pixels, which the renderer uploads once and
reuses across frames. A written asset must be a PNG; it is embedded at build time, and the
path is resolved beside the `.ui` file that writes it:

```text
Image { source: @image("assets/logo.png"); }
```

A bound `image` property is the form for a picture the application supplies at run time.
The [Image](../components/image.md) widget is the standard consumer; the handle itself is
`std::shared_ptr<const ImageResource>` in C++.

## Native types

A type declared in a `.ui-manifest` file — a native component or a record the manifest
declares — resolves by name like any other. That is covered with the manifest on
[Native components](../concepts/native-components.md).
