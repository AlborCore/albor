# Menu, MenuItem, MenuSeparator, MenuBar and ToolBar

The in-tree menu and strip widgets, which present shared `Command` objects rather than holding
their own labels, enablement or handlers. A command is declared once in the component body and
every consumer binds the same pointer:

```text
component Editor {
    command save "Save";

    MenuBar {
        MenuItem { text: "Save"; command: save; shortcut: "Cmd+S"; }
    }

    ToolBar {
        Button { text: "Save"; command: save; }
    }
}
```

## MenuItem

| property | type | notes |
| --- | --- | --- |
| `text` | `string` | the label used before a bound command publishes one |
| `command` | `command` | the shared action, which supplies the label, visibility and enablement |
| `shortcut` | `string` | the accelerator spelling a `ShortcutTable` produced |
| `disabled` | `bool` | the state while no command is bound |

A menu item invokes its command through `InvocationSource::Menu`, exposes an `activate` semantic
action, and is a focus stop only while it is visible and enabled. With no command, `text` and
`disabled` stand alone.

## MenuSeparator

A rule between groups of items. It has no properties, no action and no focus stop.

## Menu

An ordered vertical set of `MenuItem` and `MenuSeparator` children.

| property | type | notes |
| --- | --- | --- |
| `padding` | `float` | inset between the menu edge and its rows |
| `spacing` | `float` | gap between two visible rows |
| `background` | `color` | the menu face |
| `corner_radius` | `float` | the face radius |

A hidden child takes no layout space. A disabled item stays in place and is skipped by the arrow
traversal. Up and down arrows move between visible enabled rows; Escape drops the row focus.

## MenuBar and ToolBar

Horizontal strips that host controls. Both expose `padding`, `spacing`, `background` and
`corner_radius`, place visible children left to right, and announce a semantic group. `MenuBar`
is the shell's top strip; `ToolBar` is the same container for ordinary controls.

## Shortcut display

The item presents the one spelling the accelerator table owns:

```cpp
const ShortcutTable& table = registry.shortcuts();
MenuItem& item = menu.add_item(*save, &table, Shortcut{.key = Key::S, .modifiers = Modifiers{.command = true}});
// item.shortcut_text() == table.display(...)
```

## Commands in `.ui`

`command name "Label";` declares one `Command` in the component body. The generated facade owns
it, exposes `command_name()`, constructs it before the tree, and emits `set_command` for every
`command:` binding. A `command:` name that no declaration provides is an error; a declaration
that no element binds is a warning (`W0405`).
