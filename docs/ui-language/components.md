# Components

A **component** is the unit the compiler turns into a concrete C++ facade class; a file exports
one component and may declare further same-file bases for it
([one exported component per file](#one-exported-component-per-file)). Every tree the language
builds is a component tree: built-in widgets appear as elements, file-declared components appear
as elements too, and the two are used the same way.

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

A file exports one component: the one whose name is its file stem in PascalCase. It may declare
further components only as same-file bases for that export
([one exported component per file](#one-exported-component-per-file)). The exported component's
facade is the generated translation unit
([what the compiler produces](index.md#what-the-compiler-produces)).

## One exported component per file

`greeting_panel.ui` exports `GreetingPanel`: a saved file whose components include no name
matching its stem is rejected with `E0805`, because the build could not name the pair it
expects. The stem is what the build rule, the generated C++ facade and the preview all address,
so one file exports one component.

A file may declare more than one component in exactly one case: the exported component is a
derived component and the others are the same-file bases it inherits from
([inheritance](#inheritance)):

```text
// wide.ui — the exported component is Wide, because wide.ui names it
component Panel {
    Column {
        @children
    }
}

component Wide : Panel {
    Label { text: "Wide"; }
}
```

Only the exported component is built and reachable. The base beside it is flattened into the
exported facade: the build compiles no separate pair for it, another file cannot instantiate it,
and C++ sees only the exported facade. Declaration order does not decide the export — the name
does — so the base may come first, as above.

An imported file is stricter: it declares exactly one component (`E0804`) named after itself
(`E0805`), because importing brings one facade into scope. A file that declares a base and its
derived component is therefore a composition root and cannot be imported. If two independent
components both need to be reused, give each its own file and import them; a further component
that is not the exported component's base is not built, so split it into its own file.

## Using a component as an element

The component a file exports is itself an element type — a file that imports it can instantiate
it anywhere an element can:

```text
// greeting.ui
component Greeting {
    in property<string> title;
    signal dismissed();

    Column { Label { text: title; } }
}
```

```text
// app.ui
import "greeting.ui";

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
  elements named `Label` are *that* component, not the built-in one. Imported components resolve
  next, and the catalog is the last resort. The same shadow rule the type resolver follows
  applies to elements.
- **A base is inherited, not instantiated.** A same-file base has no pair the build compiles,
  so naming it as an element fails at link time. Use it after `:` in a component declaration
  and instantiate the exported component instead.
- **Composition may cross a file boundary.** A file imports another file's component with a
  quoted path, resolved relative to the importing file. The path uses `/` on every platform,
  because it names a portable source path:
  ```text
  import "panels/side_panel.ui";

  component Inspector {
      SidePanel {
          title: "Parameters";
      }
  }
  ```
- **An imported file declares one component named after itself.** `side_panel.ui` declares
  `SidePanel`, because that source generates one facade pair. A root is allowed a base beside
  its export; an imported file is not, so two independently reusable components live in two
  files.
- **Inheritance stays same-file.** An imported component may be instantiated but not used as a
  base, because the derived member set is flattened while its own file is checked.
- **An instance takes no children.** A composed instance builds the component's own tree;
  content written on it is rejected. Content is what
  [inheritance](#inheritance) supplies alone.
- **A graph may not instantiate itself.** A component that contains a chain back to itself is
  reported as a cycle, and an import cycle is reported at the file graph.

On the C++ side, each instance is a member that constructs the imported facade with both engines
the window owns, and the host binds against that facade's generated accessors. The build helper
compiles every imported file into the same target and orders its generated headers first; see
[Wiring the build](../tooling.md#the-cmake-helpers). The preview host and the dev runtime have no
generated facade, so they expand the imported tree and apply the instance values before building
it. An asset written in the imported file resolves beside that file, not beside the importing one,
so a panel can keep its images in its own directory.

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
// wide.ui — Wide is the exported component
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

`wide.ui` exports `Wide`, the derived component; `Panel` is the base declared beside it and is
not exported itself ([one exported component per file](#one-exported-component-per-file)).

The rules:

- **One level deep.** The base must be a component this file declares and must itself derive
  from nothing. A deeper chain is refused rather than silently merged.
- **The derived component is the export.** The file must be named after the derived component,
  because that is the only component the build compiles. A file named after the base would
  export the base and leave the derived component unusable.
- **The derived body is content, not a tree.** It carries elements — not a root — and they
  are spliced into a *clone* of the base's tree at the `@children` marker, in the order the
  body wrote them. The base's own body is untouched.
- **One slot.** A base declares at most one `@children`. A slot inside the derived content is
  refused: only a base declares the content point.
- **A base used directly shows no content point.** The marker is stripped from every checked
  tree, so a base that is itself the exported component builds exactly what it declares.
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
