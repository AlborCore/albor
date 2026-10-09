# Defect D5: Widgets never render their children

Context and the sibling defects live in `RENDERING-DEFECTS.md`. **Fixed**: widgets record their children and paint through the content hook.

## Summary

Every widget overrides all three tree passes and none of the overrides services
children. A subtree attached to a widget with `add_child` is never measured, never
arranged and never recorded, so it is completely inert: it costs memory and produces no
pixels, no bounds and no semantics. The same override pattern leaves
`Component::paint_background` and `Component::background` as dead code, because no
widget ever reaches the component render path.

## Defect

The five widgets override `measure`, `arrange` and `render`, and every override is
self-contained:

| widget | measure | arrange | render |
| --- | --- | --- | --- |
| `Button` | `widgets/button.cpp:24-39` (label only) | `widgets/button.cpp:41-48` (no children) | `widgets/button.cpp:50-65` (no children) |
| `Label` | `widgets/label.cpp:16-29` | `widgets/label.cpp:31-38` | `widgets/label.cpp:40-48` |
| `CheckBox` | `widgets/checkbox.cpp:14-22` | `widgets/checkbox.cpp:24-31` | `widgets/checkbox.cpp:33-44` |
| `Slider` | `widgets/slider.cpp:25-33` | `widgets/slider.cpp:35-42` | `widgets/slider.cpp:44-63` |
| `Canvas` | `widgets/canvas.cpp:8-16` | `widgets/canvas.cpp:18-25` | `widgets/canvas.cpp:27-35` |

The composite machinery they bypass:

- `Component::render` paints the background and then recurses into children
  (`ui/component.cpp:45-50`).
- `UiNode::render` recurses into visible children (`ui/ui_node.cpp:118-126`).
- `Component::paint_background` and `Component::background` (`ui/component.cpp:60-71`)
  are reachable only through `Component::render`, which no widget calls.

The consequences:

1. **Children are invisible.** Each widget's `render` records only its own content and
   never walks `m_children`. Each widget's `arrange` assigns its own bounds and never
   assigns child bounds, so a child keeps its default empty bounds. Each widget's
   `measure` measures only the widget's own content (label, intrinsic size) and never
   asks a child.
2. **The plate is drawn through a duplicated path.** `Button::render` draws its own
   plate (`widgets/button.cpp:57`) although the base already carries the exact hook for
   it: `Button::background` returns the plate colour (`widgets/button.cpp:194-197`) and
   `paint_background` would draw it. The base path is dead, the derived path duplicates
   it.

## Cause

The widgets were written as leaves: each overrides the full contract and none defers
the composite parts. The class hierarchy permits composition (`Widget` extends
`Component`, which extends `UiNode`, and `add_child` is inherited), so leaf-only
implementations silently break the contract instead of refusing it.

## Plan

1. Introduce a content hook in the component render path:
   `Component::render` becomes `paint_background(context)`, then a new virtual
   `paint_content(RenderContext&)` (empty by default), then the child recursion of
   `UiNode::render`. The background stays under the content and the children stay on
   top of both, which is the order §6.1 already prescribes.
2. The widgets stop overriding `render`. Each widget moves its content into
   `paint_content`: Button draws the label, Label the text run, CheckBox the fill and
   outline, Slider the track and thumb, Canvas the application draw callback.
3. The button plate moves to the existing `paint_background`/`background` pair, which
   revives the dead code and removes the duplicated plate path. The disabled fade
   arithmetic moves into the `background` override.
4. The widgets that keep custom `measure`/`arrange` must chain to the base for
   children, so a widget's subtree is measured and arranged like any other composite
   (§8.2). Widgets that are deliberately leaves refuse children instead: `add_child`
   on them is a programming error, reported rather than stored.

## Verification

- A widget with a child records the child's primitives after the widget's own content,
  in the same frame.
- A child of a widget carries arranged bounds after the build, not its default empty
  bounds.
- The button plate is recorded exactly once per frame, through the background path.
- The existing widget suites keep passing, since every current widget is a leaf in
  practice.
- `bash build/check-format.sh` passes on every changed file.
