# Properties and bindings

Albor's state model is a typed property graph with **eager-push** evaluation. A property is
a value with an identity; a binding is a declared dependency from a property to the
expressions that read it; when a property changes, everything that reads it recomputes
immediately — in microseconds, not on the next poll.

## In the language

```text
component Meter {
    in property<float> level;        // state, declared
    inout property<int> ticks: 0;

    Column {
        Label { text: string(level * 100.0) + "%"; }      // a binding: reads level
        Canvas { width: 380px; height: 20px; }
        Button {
            text: @tr("Tick");
            on_clicked: { ticks = ticks + 1; }            // a handler writes inout
        }
    }
}
```

An element assignment whose value is an expression becomes a **binding**: the compiler
records which properties the expression reads, and when one of them changes the binding
re-evaluates and writes the result through the element's setter. A literal assignment
(`text: "press me";`) sets the property once at construction.

The dependency list is not a runtime introspection — it is **declared at compile time**,
because the compiler walked the expression. That is what makes the evaluation cheap: each
generated binding is a static function plus an array of property ids.

## The evaluation model

- **Eager push.** A `set` propagates dirtiness along the dependency graph in a topological
  pass and re-evaluates affected bindings immediately. There is no pull phase, no dirty-flag
  sweep at frame start, and no stale window.
- **Cycles are refused.** A static dependency cycle is rejected by the `.ui` compiler. A
  dynamic cycle that slips past — through an imperative write, for instance — is halted by
  an execution-pass budget (64 iterations) and flagged.
- **Layout invalidation follows the write.** A binding whose value can change a measured
  size invalidates layout; one that only changes paint invalidates repaint. A colour change
  costs a repaint, not a relayout.

## What compiles

Every binding and every handler compiles to a **plain static function** over the component's
own context, with its dependencies as declared `PropertyId` arrays. No `std::function` per
binding, no per-binding heap allocation. This is the convention the whole generated codebase
follows, and it is why a tree with a thousand bindings is unremarkable.

## In C++

The same model is available directly, without any `.ui` file. This is how hand-built
components — and the runtime parts of an application — declare state:

```cpp
class CounterPanel : public albor::Component {
public:
    CounterPanel(albor::BindingEngine* engine, albor::TextEngine* text_engine);

    albor::Property<int> count;       // registered with the component
    albor::Property<std::string> label;
    albor::Property<albor::Color> accent;
};

CounterPanel::CounterPanel(albor::BindingEngine* engine, albor::TextEngine* text_engine) :
    albor::Component(engine, "counter_panel"), m_text_engine(text_engine) {
    register_property(count);
    register_property(label);
    register_property(accent);

    // a binding: recomputed eagerly when count changes, dependencies declared as ids
    label.bind([this] { return "count " + std::to_string(count.get()); }, {count.id()});

    accent.bind([this] { return pick_accent(count.get()); }, {count.id()});
}
```

Reading is `property.get()`; writing is `property.set(value)` or `operator=`. An imperative
`set` **overrides** any declarative binding on that property; `rebind()` restores it.

## Property directions, in both worlds

| direction | who writes | who reads |
| --- | --- | --- |
| `in` | the host, or a parent's binding | the component |
| `inout` | the component's handlers — and the host, through the facade | everyone |

An `in` property is not assignable from a handler because a binding owns it; two writers
would fight every frame. See [Properties](../ui-language/properties.md#directions).

## Crossing threads

UI objects live on the UI thread. Application data reaches the UI as an **immutable
snapshot** — `Snapshot<T>`, which is `std::shared_ptr<const T>`:

```cpp
// a worker thread owns its data and never touches a UI object
albor::Observable<Snapshot<WaveformData>> data;

// publish from anywhere, safely: post_set is the thread-safe path
data.post_set(std::make_shared<const WaveformData>(std::move(next)));

// the UI thread reads it through a binding, with no lock
```

- `Observable<T>::post_set` posts an update task to the UI event loop — this is the only
  thread-safe mutation path. `set` is UI-thread only.
- The snapshot is immutable by construction: whoever holds it holds the same const value, so
  no frame can observe a half-updated model.
- A `.ui` property of type `Snapshot<T>` is how a manifest-declared native component receives
  one ([Types](../ui-language/types.md#snapshot)).

## Where this shows up in generated code

A compiled component's bindings and handlers are exactly the shape above: static functions,
declared dependencies, one context pointer. The facade's setters are the only write entry
points, which is what lets a host (`panel->set_click_count(3)`) participate in the same graph
a handler writes to — one property, one owner, one propagation.
