# Defect D7: No style module, and no interaction states beyond the disabled fade

Draft issue report. Context and the sibling defects live in `RENDERING-DEFECTS.md`.

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

4. **Interaction contract.** A widget with no theme falls back to its current
   hardcoded values, so no existing test changes behaviour until a theme is installed.

## Dependencies and notes

- Hover and pressed visuals want rounded plates and radius strokes, which the current
  scene cannot express (D4). The scene plan in `RENDERING-DEFECTS.md` restores radius
  on the command, so the state visuals land on top of it rather than fighting it.
- The enter/leave synthesis must preserve the input dispatcher's existing routing
  order, so press targeting and hover targeting agree on which node is under the
  pointer.

## Verification

- Enter and leave are synthesized exactly at boundary crossings, asserted on a two
  widget column.
- A widget's recorded paint differs between normal, hovered, pressed and disabled.
- A theme override changes a widget's visual without changing the widget.
- No theme installed: every widget renders exactly as today.
- The existing input and widget suites keep passing, and
  `bash build/check-format.sh` passes on every changed file.
