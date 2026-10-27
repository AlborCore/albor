# Commands

A **command** is a shared action object: one object that owns an action's label, its
enablement, its visibility and the action itself — observed by every consumer rather than
copied into each of them. A toolbar button, a menu item, a keyboard shortcut and an
automation client all bind the *same* object, so none of them can disagree about whether
"Save" is available right now.

## The problem it solves

Without a shared object, every surface that offers an action holds its own copy of the
state: the button decides for itself whether it is enabled, the shortcut decides again, the
menu a third time. They drift. A command inverts that: the action is one thing, state
changes publish once, and every consumer follows in one pass.

## In `.ui`

A component receives a command through an `in property<command>` slot, and two forms attach
it to a control or fire it:

```text
component Editor {
    in property<command> save;
    in property<command> open_recent;

    Row {
        Button { text: @tr("Save"); action: save; }         // bind: the command drives the control
        Button {
            text: @tr("Save as");
            on_clicked: { save.invoke(); }                  // fire from a handler
        }
    }
}
```

- **`action: save;`** binds the command to the control. The control's label, disabled state
  and click target follow the command — a disabled command is a disabled button, with no
  second bookkeeping.
- **`save.invoke();`** fires the command from a handler, with no argument and recording no
  property write. `invoke` is the only command method the language exposes.

[Button](../components/button.md), [Slider](../components/slider.md) and
[CheckBox](../components/checkbox.md) each carry an `action` property.

## In C++

The application declares its commands once, at startup, and keeps them alive:

```cpp
#include "commands/command_registry.h"

albor::CommandRegistry& commands = albor::Application::instance().commands();

// declare hands back the object; the caller keeps it alive
std::unique_ptr<albor::Command> save = commands.declare("file.save", "Save");
albor::Command& save_command = *save;

save_command.set_action([](const albor::CommandInvocation&) {
    // the action itself
});

// enablement is a published state, observed by every bound consumer
save_command.set_enabled(document.is_dirty());
save_command.set_visible(in_editor_mode);
```

A consumer — widget, menu row, shortcut handler — binds a plain observer and never caches
the state:

```cpp
button->set_action(save_command);       // the button follows the command's published state
```

Invoking returns why it did or did nothing:

```cpp
const albor::InvocationOutcome outcome = save_command.invoke();
if (outcome != albor::InvocationOutcome::Invoked) {
    // refusal is a first-class outcome — Disabled, Hidden, NoSelection or NoAction
}
```

## The rules

Four rules are load-bearing:

1. **Enablement is observed, not copied.** `set_enabled(false)` publishes to every bound
   consumer in one pass; three consumers cannot disagree.
2. **Refusal is first-class.** `invoke` reports why it did nothing. There is no "invoke
   anyway", which is what lets an automation client observe exactly the enablement a user
   would.
3. **One owner.** The application owns its commands; the registry indexes them by their
   author-assigned id and keeps none alive. A command leaves the index through its own
   destructor.
4. **Identity is explicit.** The id the author assigned is the only selector automation may
   use — the same identity rule the whole framework follows.

## Undo and shortcuts

A command may carry an undo entry, so one invocation contributes one entry to the shared
stack, and the registry owns the two history commands it stands on. The shortcut table lives
on the registry too: one table, one resolution order, the same object a menu shows.

An entry whose closure captures a widget carries an `UndoOwner` token. Destroying the widget
revokes the token and removes its entries, so a document-wide stack cannot undo into a field
that no longer exists; a folded group skips a step whose owner is gone. A field binds with
`TextField::bind_undo_stack` and holds the stack's lifetime token too, so a field that outlives
its stack neither calls it nor records into it. Dead entries are pruned before the depth cap is
enforced, so revoked history never displaces live history.

!!! note "Why not just a function pointer"
    A function pointer carries no state. The point of a command is precisely the state —
    *is this available now, should it be shown, what is its label* — and publishing that
    once is the entire design.
