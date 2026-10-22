# The .ui language

A `.ui` file is a declarative description of one component tree: state declarations at the
top, a root element below them, children nested by indentation the author writes. The file
**compiles to native C++** — there is no interpreter, no property-by-name reflection and no
runtime evaluator in a production build. What the compiler emits is ordinary C++ that links
into your application.

## Anatomy of a file

```text
// greeting_panel.ui
component GreetingPanel {
    in property<string> user_name: "World";      // a declaration: state with a default
    inout property<int> click_count: 0;

    signal run_clicked();                        // a declaration: something the host can hear

    Column {                                     // the root element
        spacing: 12px;

        Label {
            text: @tr("Hello, ") + user_name;    // an assignment with a source: a binding
        }

        Button {
            id: increment_button;                // explicit identity
            text: @tr("Increment");
            on_clicked: {                        // a handler: what this component does
                click_count = click_count + 1;
                run_clicked();
            }
        }
    }
}
```

Reading top to bottom:

1. **Component declaration** — `component Name { ... }`. A file may declare several
   components; each one compiles to its own facade class.
2. **Property declarations** — `in` and `inout` properties with their types and optional
   defaults ([Properties](properties.md)).
3. **Signal declarations** — `signal name(parameters);`, raised to whoever listens
   ([Signals and handlers](handlers.md)).
4. **The root element** — exactly one element that is the tree the component builds. Every
   other construct is nested inside it.

## What the compiler produces

The component's name becomes the generated class; the file's name becomes the generated
unit. `greeting_panel.ui` containing `GreetingPanel` emits:

```text
build/generated/<target>/greeting_panel.h    // the concrete facade
build/generated/<target>/greeting_panel.cpp  // the implementation
```

The header exposes exactly what the declaration promised — property accessors
(`set_user_name`, `click_count`), signal slots (`set_on_run_clicked`) and construction with
the two engines the window owns. The implementation holds the tree, the bindings and the
handlers as static functions over declared dependencies: no `std::function` per binding, no
per-binding heap allocation.

Editing the `.ui` file rebuilds only that component's generated unit and what includes it.
Changing a declared interface rebuilds the header and its includers; changing a body rebuilds
the `.cpp`.

!!! info "Which names may a file declare?"
    A component name the file declares **shadows** a native or manifest type of the same
    spelling — file first, catalog second. The same rule applies inside
    [inheritance](components.md#inheritance).

## The forms of the language

| form | page |
| --- | --- |
| components, composition, inheritance, identity | [Components](components.md) |
| property and signal declarations, directions, defaults | [Properties](properties.md) |
| literals, operators, conversions, functions, globals | [Expressions and bindings](expressions.md) |
| `struct`, `enum`, records, `vector<T>`, value sets | [Types](types.md) |
| font family, weight, fallback, embedded registration | [Fonts](fonts.md) |
| signals, `on_` blocks, the four handler statements | [Signals and handlers](handlers.md) |
| `if`, `visible`, `Repeat` | [Control flow](control-flow.md) |

## The boundary a handler may not cross

A handler body is a **closed grammar of four statement forms**, and it reaches only members
of *this* component. It cannot call an arbitrary method, read another component's property or
run a loop. The forms are listed on
[Signals and handlers](handlers.md#the-four-statement-forms); the design reason is the same
one the rest of the language follows — what compiles is small enough to be fully checked and
compiled away.

!!! note "Conformance"
    The same `.ui` corpus runs through the compiled output and through the development IR
    evaluator in tests, and both must produce the same observable behavior. What is
    documented here is what both routes agree on.
