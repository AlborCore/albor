# Control flow

Two forms express presence, and they are not interchangeable — plus one form builds rows over
a collection.

## `if`: conditional instantiation

```text
Column {
    if show_details {
        Column {
            Label { text: detail_text; }
            Button { text: @tr("Hide"); on_clicked: { show_details = false; } }
        }
    }
}
```

`if condition { ... }` instantiates the elements inside **only while the condition holds**.
A non-present element occupies no layout, and it rebuilds from scratch when it reappears —
its identity, its bindings and its state start over.

That makes `if` the right tool for a mutually exclusive pair where only one alternative is
ever valid: a loading spinner or the content, a success panel or an error panel.

## `visible`: hide in place

```text
Label { text: status_text; visible: armed; }
```

`visible` is a property every element carries. While `false`, the element stays **in the
tree** — its identity, bindings and state intact — and is skipped by layout and drawing.
Show it again and everything is exactly as it was: what the user typed survives, no
re-initialisation fires, and focus traversal skips it only while it is hidden.

That makes `visible` the right tool for a tabbed surface, a collapsible section or a
toolbar that dimly disables — anything whose state must survive being switched away from.

| | `if` | `visible` |
| --- | --- | --- |
| element exists while hidden | no | yes |
| state survives re-showing | no | yes |
| occupies layout while hidden | no | no |
| rebuild cost on re-show | full | none |
| best for | alternatives that never coexist | panels that come back |

## `Repeat`: rows over a collection

```text
inout property<vector<string>> items;

Column {
    Repeat(item in items) {
        Label { text: item; }
    }
}
```

`Repeat(item in collection)` instantiates its body once per element. The item name binds the
current element; the body is ordinary markup and may be any size. An optional second name is
the row index:

```text
Repeat(item in items, index) {
    Label { text: string(index) + ": " + item; }
}
```

Inside the body the item name shadows outer names of the same spelling, exactly as a signal
parameter does ([Expressions and bindings](expressions.md#names)).

### Virtualized scrolling

A `Repeat` placed directly inside a [ScrollView](../components/scroll-view.md), with the
view's `row_height` set, builds only the rows the viewport plus its overscan needs:

```text
inout property<vector<Task>> tasks;
inout property<float> line_height;

ScrollView {
    viewport_width: 240px;
    viewport_height: 160px;
    row_height: line_height;
    overscan: 40px;
    scrollbar: Scrollbar.on;

    Repeat(task in tasks) {
        Label { text: task.title; }
    }
}
```

Rows scroll by offset; only the visible window plus the overscan band is instantiated. A
record may declare its identity with a field named `key`, which is what a row is keyed by —
the identity a background update correlates against ([Native components](../concepts/native-components.md#records)).

Mutate the underlying collection from a handler with the closed method set
([Built-in methods](handlers.md#built-in-methods)); the rows follow.
