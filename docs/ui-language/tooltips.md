# Tooltips

Every element accepts the common [`tooltip`](../components/index.md#common-properties) property,
so a widget, a layout container and a component instance all carry hover text the same way.

```text
ToolBar {
    Button { id: save; text: @tr("Save"); tooltip: @tr("Saves the current document"); }

    Label { text: user_name; tooltip: @tr("The signed-in account"); }
}
```

A container's tooltip covers an unadorned descendant, so a `SidebarRow` can name the whole row
while its `Label` stays plain. A child with its own tooltip wins over its parent.

## Timing

The window arms a tooltip after 500 ms of hover. Once one has been shown, moving onto an adjacent
control opens the next after 50 ms, and the warm state survives a 250 ms gap. A shown tooltip hides
itself after ten seconds.

A press, the wheel, any key, the pointer leaving the window and a window that loses key status
dismiss it at once. The control that was pressed stays quiet until the pointer leaves it or names
another element.

## Presentation

The bubble hangs under the target and is centred on it. Near the bottom of the window it flips
above, and at either side it clamps inside the view rather than clipping the text. It takes no
pointer input, so the control underneath stays hovered.

## Accessibility

A control with no other name (an icon-only button) announces the tooltip as its accessible name.
A named control carries the tooltip as its accessible description, which the macOS and Windows
bridges expose as help text.

A rich subtree paints the bubble, but it is not text: set `tooltip` as well, because that string is
what assistive technology announces when the bubble is not a run.

## Theme changes

`MainWindowConfig::tooltip_style` installs the bubble when the window is created. An application
that swaps themes at runtime calls `stage->tooltips()->set_style(tooltip_style(theme))` after
`apply_theme`, which re-styles a visible bubble and every later one.

## Notes

- An empty `tooltip` means no tooltip of its own: the nearest ancestor that carries one applies.
- A disabled widget does not hit-test as itself, so its own tooltip does not appear. Put the
  tooltip on the containing row when a disabled action needs an explanation.

## C++

`UiNode::set_tooltip` carries the string form, which is also the accessible fallback. A rich subtree
is supplied through `set_tooltip_builder`, which returns the node the window presents instead of the
text bubble; when both are installed the builder wins.

```cpp
auto row = std::make_unique<SidebarRow>(&engine, "transient.sch");
row->set_tooltip("A transient sheet, not saved beside the project");
row->set_tooltip_builder([&] { return make_sheet_tip(&text, sheet); });
```
