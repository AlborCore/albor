# Defect D7: No style module, and no interaction states beyond the disabled fade

Context and the sibling defects live in `RENDERING-DEFECTS.md`. **Mostly fixed**: the theme, the
synthesized transitions and the control state are done for the button. What is left is stated below.

## Summary

The module layout names `style/` for themes, palettes and styling definitions
(`albor-design-v4.md:166`) and the component diagram lists themes and brushes
(`albor-design-v4.md:112`), but nothing implements it. Widget colours and control
sizes are hardcoded literals per widget, each widget carries its own private colour
setters, and the input path produces no pointer enter or leave, so a widget cannot
know when the pointer is over it. The only interaction driven visual in the framework
is the button disabled fade.

## Defect

1. **No style module exists.** There is no theme, no palette, no style token, and no
   brush type. Widgets cannot resolve a colour by role.

2. **Visual constants are hardcoded per widget.**
   - colours: `widgets/button.h:72` (plate), `widgets/checkbox.h:63,66` (fill,
     outline), `widgets/slider.h:85,88` (track, thumb), `widgets/label.h:61`
     (foreground);
   - sizing: `widgets/button.cpp:11-14` (minimum plate extent),
     `widgets/checkbox.cpp:6,9` (box size, outline width),
     `widgets/slider.cpp:8-20` (track extent, thickness, thumb size, step).

3. **No enter or leave events.** `EventKind` carries pointer down, move, up and key
   down. The input controller routes everything else by position
   (`input/input_controller.cpp:85`) and synthesizes no transitions, so a widget sees
   moves but never learns when the pointer entered or left it. A hover visual is
   therefore not implementable without input changes.

4. **No state machine.** Widgets carry no hovered or pressed state. The disabled fade
   is inline arithmetic that halves the plate colour (`widgets/button.cpp:55`), not a
   style lookup.

## Cause

Widgets predate the style layer and the input event set was built for routing, not for
interaction feedback. Without a theme the setters are the only configuration surface,
and each widget invented its own.

## Plan

### Done

1. **Build `style/`.** A theme maps style roles to values: background, foreground,
   accent, and their disabled, hovered and pressed variants, plus control sizing
   tokens (minimum extents, corner radius, stroke widths). Widgets resolve paints from
   the theme by role through the widget base; the existing per-widget setters stay as
   explicit overrides and win over the theme.

2. **Synthesize enter and leave.** The input dispatcher derives
   `PointerEnter`/`PointerLeave` from pointer moves plus the hit test it already runs
   (§8.3), so widgets receive transitions at content boundaries rather than raw moves.
   When the tree changes under the pointer — a subtree is removed or hidden while
   hovered or pressed — the dispatcher replays the exit events before the change is
   routed, so no widget keeps a stale hovered or pressed state pointing at content
   that no longer exists.

3. **Give controls a state.** A control state of normal, hovered, pressed, disabled,
   driven by the enter/leave and press events, with pressed held from pointer down
   until pointer up or leave. Widgets select their paint from the theme by state, so
   the button fade becomes a disabled theme entry instead of inline arithmetic.

   The precedence is disabled, then pressed, then hovered, then normal, and the
   states are `ControlState` in `style/theme.h`. `Widget::handle_hover_transitions`
   runs before a widget's own handling, so a press is a state and an activation at
   once. A drag that leaves the bounds releases the press, and a drag that stays
   inside keeps it, because the drag belongs to the control (§8.3).

4. **Interaction contract.** A widget with no theme falls back to its current
   hardcoded values, so no existing test changes behaviour until a theme is installed.

### Still open

5. **The other controls resolve through the theme, but their own metrics are still
   literals.** The button reads its minimum from the theme; the check box, the slider and the
   label still carry `BOX_SIZE`, `OUTLINE_WIDTH`, `SLIDER_WIDTH`, `THUMB_SIZE` and `STEP` in
   their `.cpp` files. `ControlMetrics` already holds those values, so the remaining work is
   mechanical: read them through `theme()` the way the button does. It is left because each one
   needs its own paint test to prove the fallback is unchanged, and doing all four in one pass
   would make a regression in any of them hard to read.

6. **No control other than the button draws a state.** The state machine lives on the widget
   base and every widget inherits it, so hover and press work for a check box or a slider
   already; what they do not do yet is resolve their paint from the palette by state, which is
   the same mechanical work as item 5.

### The invariant the replay rests on

`InputController` holds the hovered nodes as raw pointers, because a tree owns its nodes and the
controller borrows them. **An application that removes or hides a subtree while the pointer is
inside it must call `refresh_hover()`**, which replays the exits and rebuilds the chain against
the tree as it now stands. Without that call the chain keeps a pointer to a node that may have
been destroyed, and the next pointer move dispatches a leave into freed memory.

This is a real hazard rather than a nicety, and the framework does not yet have a way to detect
the mutation from inside the input module: the tree mutation paths (`UiNode::remove_child`,
`set_visible`) reach no input observer. The honest fix is a change notification on the tree, which
is a larger change than this defect, so the obligation is recorded here and covered by the three
`HoverChecks` refresh tests until that notification exists (§8.3).

## Dependencies and notes

- Hover and pressed visuals want rounded plates and radius strokes, which the current
  scene cannot express (D4). The scene plan in `RENDERING-DEFECTS.md` restores radius
  on the command, so the state visuals land on top of it rather than fighting it.
- The enter/leave synthesis must preserve the input dispatcher's existing routing
  order, so press targeting and hover targeting agree on which node is under the
  pointer.

## Verification

What is covered by tests now:

- A theme carries a palette and metrics, an override replaces one entry, and a state
  override replaces one state without moving the rest (`ThemeChecks`).
- A widget has no theme until one is installed; with none it draws its own plate, and the
  fallback metrics are the literals it used before, so no existing test changes behaviour.
- A theme override changes the paint without touching the widget, and a themed button
  measures from the themed minimum.
- Normal, hovered and pressed are three different plates; a disabled button reports disabled
  over a hover; a drag outside releases a held press and a drag inside keeps it
  (`ControlStateChecks`).
- Enter and leave are synthesized exactly at boundary crossings: a first move enters, a second
  move inside does not, a move off leaves, crossing siblings leaves then enters, a child leaving
  keeps its root hovered, a press learns what it landed on, a hidden node is skipped, and a key
  press synthesizes nothing (`HoverChecks`).
- The replay: hiding a hovered subtree replays its exit and leaves nothing hovered, and a
  controller that never saw a pointer invents nothing.

- Enter and leave are synthesized exactly at boundary crossings, asserted on a two
  widget column.
- A widget's recorded paint differs between normal, hovered, pressed and disabled.
- A theme override changes a widget's visual without changing the widget.
- No theme installed: every widget renders exactly as today.
- The existing input and widget suites keep passing, and
  `bash build/check-format.sh` passes on every changed file.
