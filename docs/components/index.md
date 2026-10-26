# Components

The `.ui` language ships twenty elements: eleven containers that arrange children, and nine
leaves that draw or edit. Every element also carries the three common properties below, and
every element may carry an `id`.

## Catalogue

| element | kind | one-line purpose |
| --- | --- | --- |
| [Column](column.md) | container | a vertical stack with spacing and padding |
| [Row](row.md) | container | a horizontal stack with spacing |
| [Padding](padding.md) | container | insets its child |
| [Stack](stack.md) | container | overlays its children |
| [Grid](grid.md) | container | equal cells in a grid |
| [ScrollView](scroll-view.md) | container | a scrollable viewport over one content child |
| [ListBox](list-box.md) | container | a virtualized list of rows under one selection |
| [TabView](tab-view.md) | container | one page at a time under a tab strip |
| [Tab](tab-view.md#tab) | container | one page inside a TabView, labelled by the strip |
| [TabTitle](tab-view.md#tabtitle) | container | the icon or composed run a tab draws in the strip |
| [RadioGroup](radio-group.md) | container | one choice among radio buttons in a row or column |
| [Label](label.md) | leaf | a run of text |
| [Button](button.md) | leaf | a push control with a click signal |
| [TextField](text-field.md) | leaf | an editable text field |
| [Slider](slider.md) | leaf | a continuous value the user drags |
| [ProgressBar](progress-bar.md) | leaf | a read-only fraction of task progress |
| [CheckBox](checkbox.md) | leaf | a truth value the user toggles |
| [RadioButton](radio-group.md#radiobutton) | leaf | one choice inside a RadioGroup |
| [NumericStepper](numeric-stepper.md) | leaf | a bounded number with increment and decrement |
| [Canvas](canvas.md) | leaf | a drawing surface with a closed primitive vocabulary |

## Common properties

Every element exposes these three, in addition to its own:

| property | type | effect |
| --- | --- | --- |
| `horizontal_align` | `align` | `Align.start`, `Align.center`, `Align.end`, `Align.stretch` on the horizontal screen axis |
| `vertical_align` | `align` | the same set on the vertical screen axis |
| `visible` | `bool` | hide in place, keeping identity, bindings and state ([control flow](../ui-language/control-flow.md#visible-hide-in-place)) |

The alignment axes are screen axes — horizontal is always horizontal — and neither one is
the parent's main axis. `stretch` is the spacer idiom that absorbs leftover space; the
patterns live under [Layout](../concepts/layout.md#patterns).

## Identity

```text
Button { id: save_button; text: @tr("Save"); }
```

`id` is the element's authoritative identity — what automation, accessibility and semantic
keys address it by ([Components](../ui-language/components.md#identity)).

## Units

Length properties (`spacing`, `padding`, `font_size`, a viewport size, a canvas extent)
accept `px` — logical pixels that scale with display density — and `phx` — physical device
pixels. `px` is the default choice; see [Layout](../concepts/layout.md#units).

## Assignment is a binding

```text
Label { text: greeting; }            // binds to the property
Button { text: "press me"; }         // a literal sets once at construction
```

Every assignment with a source is an eager binding whose dependencies are declared at
compile time ([Expressions and bindings](../ui-language/expressions.md#assignment-with-a-source-the-binding-form)).
