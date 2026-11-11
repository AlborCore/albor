# Signals and handlers

A **signal** is a declared event; a **handler** is what this component does when one fires.
Together they are the only way a `.ui` component reacts to anything — there is no other
callback surface, and a handler reaches only members of its own component.

## Declaring a signal

```text
component Chart {
    in property<float> cursor;

    signal run_clicked();                  // no payload
    signal selection_changed(float time);  // one typed, named parameter
    signal closed_by(float time, string reason);

    // ...
}
```

The parameter list is required — write `()` for none — and each parameter carries a type and
the name a handler body will use. A signal stores nothing: it is raised, interested parties
hear it, and the frame moves on.

On the C++ side each declared signal becomes a slot on the generated facade:

```cpp
panel->set_on_selection_changed([](float time) { /* ... */ });
```

## Attaching a handler

Inside the component that declares the signal, an element attaches to a **native** signal of
the element's type:

```text
Button {
    text: @tr("Increment");
    on_clicked: {
        click_count = click_count + 1;
    }
}

Slider {
    value: level;
    on_changed: {
        level = value;          // the signal's named parameter, referenced by name
    }
}
```

For a **file component** instance, the same form attaches to the component's declared
signal:

```text
Greeting {
    title: name;
    on_dismissed: { }
}
```

A handler block writes `on_<signal>: { ... }` with **no parameter list** — the parameters are
the declared ones, and the body refers to them by name. A signal is attached at most once on
one element; a second `on_` block for the same signal is a compile error.

Every element type's signals are listed with that component —
[Button](../components/button.md#signals), [Slider](../components/slider.md#signals),
[TextField](../components/text-field.md#signals),
[CheckBox](../components/checkbox.md#signals),
[ScrollView](../components/scroll-view.md#signals).

## The context-menu handler

Every element raises `context_menu(x, y)`, so a component opens a menu without a host callback
for the trigger:

```text
component Chart {
    inout property<float> menu_x;
    inout property<float> menu_y;
    signal menu_requested(float x, float y);

    Canvas {
        width: 640.0;
        height: 480.0;
        on_context_menu: {
            menu_x = x;
            menu_y = y;
            menu_requested(x, y);
        }
    }
}
```

`x` and `y` are the press in the element's own coordinates — the space a `Canvas` draws in. The
handler runs on a secondary press (a right click, or a Control click on macOS) and on the menu
key or `Shift+F10`, where the point is the element's centre. The component forwards the point
through its own signal, and the host opens the menu at `bounds + (x, y)` through
`PopupHost::open_popup`. The body is a normal handler, so it may also write properties directly.

## The four statement forms

A handler body is a **closed grammar**. Exactly four statement forms are allowed, and each
reaches a member of *this* component:

| form | example | effect |
| --- | --- | --- |
| assignment | `click_count = click_count + 1;` | writes an `inout` property |
| signal invocation | `run_clicked();` / `frame_changed(frame);` | raises a declared signal, payload by value |
| built-in method | `items.append("row");` / `save.invoke();` | mutates a `vector<T>` in place, or fires a `command` |
| field write | `frame.width = 10.0;` | writes one field of a record-typed `inout` property |

### Assignment

```text
on_clicked: {
    presses = presses + 1;
    armed = false;
}
```

The target must be an `inout` property of this component — an `in` property is not
assignable from a handler, because a binding owns it
([Properties](properties.md#directions)). The value is type-checked against the property;
nothing widens implicitly.

### Signal invocation

```text
on_clicked: {
    run_clicked();                    // no payload
    frame_changed(current_frame);     // a scalar or a record, by value
}
```

A handler may raise any signal this component declares. That is how a component forwards
meaning upward: the widget's `clicked`, the component's own `dismissed`.

### Built-in methods

A `vector<T>` property is mutated in place with a closed method set:

```text
on_clicked: {
    items.append("row");     // add to the end
    items.remove_at(0);      // remove by index
    items.clear();           // remove everything
    items.set(1, "replaced");// write one index
}
```

A `command` property is fired with `invoke` — its only method:

```text
Button {
    action: save;                  // bound to the shared command
}

// or, from a handler:
on_shortcut: {
    save.invoke();
}
```

Arity and argument types are checked; anything outside this set is refused.

### Field writes

```text
inout property<Frame> frame;

on_changed: {
    frame.width = 10.0;     // working copy taken, field assigned, record set back
}
```

## The boundary

A handler cannot call an arbitrary method, run a loop, read another component's property or
reach a native object. It is four statements over this component's own members — which is
exactly what lets the compiler check it completely and compile it to a static function with
no allocation.

A handler attached inside a `Repeat` body is one callback shared by every row, so it cannot
read the repeat item or index. Pass the value through a signal that the row raises instead;
a read of either is a compile error.

## What handlers compile to

Each `on_` block becomes a **static function** on the generated implementation, subscribed
through the element's signal slot. There is no `std::function` per handler and no dynamic
dispatch: the handler is a plain function over the component's own context, which is the
same convention the binding system uses
([Properties and bindings](../concepts/bindings.md#what-compiles)).
