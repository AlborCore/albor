# Components

A **component** is the unit a `.ui` file declares and the compiler turns into a concrete C++
facade class. Every tree the language builds is a component tree: built-in widgets appear as
elements, file-declared components appear as elements too, and the two are used the same way.

## Declaration

```text
component Panel {
    in property<string> title;
    inout property<int> count: 0;

    signal closed();

    Column {
        Label { text: title; }
    }
}
```

A component carries, in this order:

1. **properties** — typed state with optional defaults ([Properties](properties.md))
2. **signals** — named events the host can subscribe to
   ([Signals and handlers](handlers.md))
3. **a root element** — exactly one; a component that declares none is rejected

A file may declare any number of components. Each compiles to its own facade; the file name
names the generated translation unit
([what the compiler produces](index.md#what-the-compiler-produces)).

## Using a component as an element

A component the file declares is itself an element type — it can be instantiated anywhere an
element can:

```text
component Greeting {
    in property<string> title;
    signal dismissed();

    Column { Label { text: title; } }
}

component App {
    in property<string> name;

    Greeting {
        title: name;               // an assignment with a source: a binding
        on_dismissed: { }          // a handler for the component's signal
    }
}
```

The rules:

- **The file's own components resolve first.** If a file declares a component named `Label`,
  elements named `Label` are *that* component, not the built-in one. The same shadow rule the
  type resolver follows applies to elements.
- **Composition is same-file.** A component instantiates only components its own file
  declares. Native widgets and [manifest components](../concepts/native-components.md) are
  resolved by the catalog otherwise.
- **An instance takes no children.** A composed instance builds the component's own tree;
  content written on it is rejected. Content is what
  [inheritance](#inheritance) supplies alone.
- **A graph may not instantiate itself.** A component that contains a chain back to itself is
  reported as a cycle.

On the C++ side, each instance is a member that constructs the sibling's facade with both
engines the window owns, and the host binds against that facade's generated accessors.

## Identity

```text
Button {
    id: increment_button;      // the author-assigned identity of this element
    text: "Increment";
}
```

`id` is the element's **authoritative identity** — the name automation, accessibility and
semantic keys address it by. It is not a property of the widget; it is the node's identity.
An element without an `id` still works, it is simply not addressable by name.

## Inheritance

A component may derive from one the same file declares:

```text
component Panel {
    in property<string> title;

    Column {
        Label { text: title; }
        @children                      // where a derived body's content lands
    }
}

component Wide : Panel {
    Label { text: "Wide panel"; }      // content elements: they fill @children
}
```

The rules:

- **One level deep.** The base must be a component this file declares and must itself derive
  from nothing. A deeper chain is refused rather than silently merged.
- **The derived body is content, not a tree.** It carries elements — not a root — and they
  are spliced into a *clone* of the base's tree at the `@children` marker, in the order the
  body wrote them. The base's own body is untouched.
- **One slot.** A base declares at most one `@children`. A slot inside the derived content is
  refused: only a base declares the content point.
- **A base used directly shows no content point.** The marker is stripped from every checked
  tree, so instantiating the base as an ordinary element builds exactly what it declares.
- **A redeclaration narrows a default.** The derived body may redeclare an inherited property
  with its own default — same type, same direction — which replaces the default. Anything
  else is refused.

The whole result flattens while the file is checked: the derived facade carries the merged
member set and the spliced tree. There is no runtime slot, no content API and no extra
indirection — Release sees only the native code the splice produced.

## Composition vs. inheritance

| | composition | inheritance |
| --- | --- | --- |
| written as | `Greeting { ... }` | `component Wide : Panel` |
| the instance is | a real child node, a facade instance | a flattened merge, no extra node |
| children on it | rejected | that is what a derived body *is* |
| typical use | reuse a finished component | extend a template, fill its slot |

Compose by default; inherit when the base is explicitly a shell that expects to be filled.
