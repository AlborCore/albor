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

5. **The palette is derived from a few accents.** `ThemeBuilder` turns four accents and a scheme
   into every entry of every role, so an application picks an accent rather than writing out
   sixteen literals. A hover lifts toward the text colour and a press sinks, and the two schemes
   differ, which is what makes them two themes rather than one. Every role gained a state taking
   setter, which is what the builder writes through (§6.1).

6. **The plate is drawn with the themed radius.** `Component::paint_background` draws a rounded
   rectangle when the component reports a radius and a hard one when it does not, so a themed
   control gets the shape the theme specifies and an unthemed one keeps the rectangle it has
   always drawn (§6.2).

### Still open

7. **Nothing chooses a theme for an application.** `ThemeBuilder` derives a theme and a widget
   accepts one, but there is no place that installs a theme across a tree, so an application with
   twenty widgets still calls `set_theme` twenty times. The obvious shape is a walk that hands one
   theme to every widget in a subtree, which belongs on `UiNode` or on the pipeline rather than on
   the application, and it is not written yet (§6.1).

### The replay hazard, and how it is closed

`InputController` holds the hovered nodes as raw pointers, because a tree owns its nodes and the
controller borrows them. Removing a hovered node and letting it be destroyed therefore left the
chain pointing at freed memory, and the next pointer move dispatched a leave into it.

**The tree now announces a detachment before it happens.** `UiNode::remove_child` and
`UiNode::set_visible(false)` call `notify_detaching`, which climbs to the root and hands the
observer the node **while it is still alive and still in the tree**. `InputController` installs
that observer on `attach`, replays the exits for the node and every descendant above it in the
chain, and cuts them from the chain before the tree destroys anything.

The order is what makes it safe, and it is the reason the notification exists at all rather than
the application calling `refresh_hover()` afterwards: after the fact, the node is gone and cannot
be told anything.

`refresh_hover()` stays, for the case the tree cannot report: an application that changes a
node's bounds or its own hit rule under the pointer. The controller uses its remembered point to
rebuild the chain against the tree as it now stands, and it invents no hover when nothing was
hovered (§8.3).

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
- The replay: a hovered node that is removed and destroyed is told its exit while it is still
  alive, a move afterwards reaches nothing gone, hiding a hovered node replays its exit without
  any application call, removing a node that was never hovered tells nothing, and a deeply
  nested subtree exits its deepest node first (`HoverChecks`).
- `refresh_hover()` covers what the tree cannot report: it replays the exits after a hidden
  subtree leaves nothing hovered, and a controller that never saw a pointer invents nothing.
- A derived theme has four distinct plate states, a hover lifts while a press sinks, the light
  and dark themes differ, a changed accent moves the plate, a derived button draws the derived
  colour, and a themed button draws a rounded plate while an unthemed one draws a hard one
  (`ThemeBuilderChecks`, `ThemeChecks`).

- Enter and leave are synthesized exactly at boundary crossings, asserted on a two
  widget column.
- A widget's recorded paint differs between normal, hovered, pressed and disabled.
- A theme override changes a widget's visual without changing the widget.
- No theme installed: every widget renders exactly as today.
- The existing input and widget suites keep passing, and
  `bash build/check-format.sh` passes on every changed file.
