# Automation and MCP

Albor exposes a running application to test tooling through a local automation
layer. A client addresses nodes by their author-assigned `id:`, reads typed values
and layout bounds, synthesizes pointer and key input, invokes semantic actions and
application commands, and captures the presented frame.

The automation core is linked only in Dev and Test configurations. The MCP server
is a thin adapter over it: every tool call becomes one automation command, and
there is no second path into the runtime (BDG-04).

!!! note "Dev/Test only"
    A Release build contains no automation symbols. A Release link that references
    `albor-automation` fails at link time by design. The host is built when
    `ALBOR_AUTOMATION` is on, which requires a Dev or Test configuration.

## Build and link

The automation core is a separate target. An application opts in explicitly:

```cmake
if(ALBOR_AUTOMATION)
    target_link_libraries(my_app PRIVATE albor-automation-auto)
endif()
```

`albor-automation-auto` is a whole-archive umbrella. It pulls the automation core,
the host, and the load-time installer, so linking it is the whole integration: the
host starts on the first UI tree that binds and stops when the last one goes away.
An application never calls an install function.

The framework's own build enables it for the `Test` and `Debug` presets through
`ALBOR_TEST` and `ALBOR_AUTOMATION`. See [Consuming Albor](consuming.md) for the
configuration table.

## Discovery

The host publishes one directory per process.

| platform | directory |
| --- | --- |
| macOS | `$TMPDIR/albor-mcp/<pid>/` |
| Linux | `$XDG_RUNTIME_DIR/albor-mcp/<pid>/` (or `/tmp/albor-mcp/<pid>/`) |
| Windows | `%LOCALAPPDATA%\albor-mcp\<pid>\` |

If the Unix socket path would be 100 bytes or longer (the portable socket budget),
the directory falls back to `/tmp/albor-<uid>/<pid>/`. The directory is created
with owner-only permissions.

It contains three files:

| file | contents |
| --- | --- |
| `host.json` | the descriptor a client scans for |
| `token` | the 32-character session token, without a trailing newline |
| `mcp.sock` (Unix) | the local socket the client connects to |

On Windows the endpoint is a named pipe named by the descriptor, for example
`\\.\pipe\albor-mcp-<session>-<pid>`. There is no `mcp.sock` file there.

A descriptor looks like this:

```json
{
  "pid": 4242,
  "process": "form-albor",
  "socket": "/var/folders/.../albor-mcp/4242/mcp.sock",
  "token": "/var/folders/.../albor-mcp/4242/token",
  "protocol": "mcp",
  "protocolVersion": "2024-11-05",
  "started": 1731690000000
}
```

`pid` and `started` let a client reject a stale directory whose process is gone.
`socket` is the endpoint to connect to; on Windows it is the pipe name. `token` is
the file whose contents are presented during the MCP handshake. `protocol` is
always `mcp` for the embedded host, and `protocolVersion` is the version the
adapter speaks.

A client that scans `host.json` should verify the process is alive, read the token
file, and connect to `socket`. The host removes the directory on a clean stop.

### Which window is addressed

The host observes every main window. The **active** window is the most recently
bound one: a newly opened window becomes active, and closing it falls back to the
previous window.

- `semantic.*` and `input.*` read the active tree.
- `command.*` reads the application's command registry, which is shared by the
  windows.
- `capture.screenshot` reads the active window's last presented frame, which can
  be a hand-drawn window without a semantic tree.
- A popup belongs to its owner and does not replace the active main window.

## MCP transport

The embedded host speaks newline-delimited JSON-RPC 2.0 over the local socket or
named pipe. One connection is served at a time. The protocol version is
`2024-11-05`; the server advertises tools only.

### Handshake

The token is presented in `params._meta` under `albor/token`. A request without a
valid token is refused with error code `-32000` before any command runs.

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": { "name": "my-test", "version": "1" },
    "_meta": { "albor/token": "<contents of the token file>" }
  }
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": { "tools": { "listChanged": false } },
    "serverInfo": { "name": "albor", "version": "1" }
  }
}
```

A wrong, missing or malformed token answers:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": { "code": -32000, "message": "authentication failed" }
}
```

Every request after the handshake requires the same authenticated connection. The
token is per process run; a new host mints a new token.

### Listing tools

```json
{ "jsonrpc": "2.0", "id": 2, "method": "tools/list" }
```

The answer lists only the commands the layer's allow-list permits. The standard
host permits the 21 commands documented on this page. Each tool carries a name, a
generic description, and an `inputSchema`:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {
        "name": "semantic.find",
        "description": "run semantic.find through the albor automation layer",
        "inputSchema": {
          "type": "object",
          "properties": {
            "id": {
              "type": "string",
              "description": "the id this command takes"
            }
          },
          "additionalProperties": false
        }
      }
    ]
  }
}
```

The real list contains all 21 tools. The schema's property names and enums are
authoritative for the MCP boundary; this page documents what each command does,
which arguments are required, and what the answer means.

### Calling a tool

`params.name` is the command name. `params.arguments` is either an object keyed by
parameter name or an array in the declared order. Every value is a JSON **string**,
because the layer wire carries strings: quote numbers and booleans
(`"0.5"`, `"0"`, `"1"`).

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "semantic.find",
    "arguments": { "id": "form_press" }
  }
}
```

Named arguments must follow the declared order and cannot skip an earlier one. To
omit a trailing optional argument (for example `input.pointer`'s `x` and `y`), omit
it and everything after it. A non-string value, a gap in the order, or a malformed
argument object is refused with `-32602`.

A successful call returns one content block and `isError: false`:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [{ "type": "text", "text": "button press me" }],
    "isError": false
  }
}
```

A layer refusal returns a JSON-RPC error with the layer's own reason:

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "error": { "code": -32000, "message": "no node with id: form_missing" }
}
```

The error codes are:

| code | meaning |
| --- | --- |
| `-32000` | authentication failed, or the automation layer refused the command |
| `-32601` | the method is not `initialize`, `tools/list` or `tools/call` |
| `-32602` | the request is not valid JSON, has no method, or its arguments cannot be placed |

A JSON-RPC notification (a request without an `id`) is accepted and produces no
reply. The adapter has no server-initiated notifications.

### Content blocks

The MCP revision used here has text, image and resource content blocks. The layer's
typed value kinds map as follows:

| layer kind | MCP block |
| --- | --- |
| `text` | `{"type":"text","text":"..."}` |
| `number` | `{"type":"text","text":"0.500000"}` |
| `boolean` | `{"type":"text","text":"yes"}` |
| `json` | `{"type":"text","text":"{\"x\":16,\"y\":16,\"width\":110,\"height\":16}"}` |
| `image` | `{"type":"image","data":"<base64 png>","mimeType":"image/png"}` |
| `binary` | `{"type":"resource","resource":{"uri":"albor://capture.screenshot/1","mimeType":"application/octet-stream","blob":"<base64>"}}` |

The scalar kinds are carried as text because this MCP revision has no number or
boolean content block. The command reference below states the kind each command
answers; a client that must distinguish a number from text can use the layer
directly (see [The raw automation wire](#the-raw-automation-wire)) or a command
that names the value.

## Command reference

Twenty-one tools are available:

| family | commands |
| --- | --- |
| semantic | `semantic.tree`, `semantic.find`, `semantic.value`, `semantic.bounds`, `semantic.read`, `semantic.actions`, `semantic.invoke`, `semantic.set_value`, `semantic.set_focused` |
| input | `input.type`, `input.click`, `input.pointer`, `input.key`, `input.wheel` |
| capture | `capture.screenshot` |
| command | `command.list`, `command.get`, `command.invoke`, `command.can_undo`, `command.undo`, `command.redo` |

All selectors are author-assigned `id:` values. A name, a role or a tree position
is never a selector (BDG-05). An unidentified node appears in `semantic.tree` with
the id `-` and cannot be addressed by any other command.

### semantic.tree

Dumps the semantic tree of the active window, one line per node, in depth-first
order.

- **Arguments:** none.
- **Answer:** `text`. Each line is:

  ```text
  <role> <id> <name>[ value=<text>][ writable][ selection=<start>:<end>][ read_only][ multiline][ expanded][ indeterminate][ disabled][ selected][ focused]
  ```

  `<id>` is `-` for an unidentified node. The optional tokens appear in the order
  shown, and only when the state holds. Names and values escape `\`, newline,
  carriage return and tab as `\\`, `\n`, `\r` and `\t`.
- **Roles:** `none`, `button`, `checkbox`, `heading`, `label`, `slider`,
  `progress_bar`, `tab_view`, `tab_item`, `radio_group`, `radio_button`,
  `numeric_stepper`, `canvas`, `textfield`, `image`, `listitem`, `list`, `menu_item`,
  `combo_box`, `group`, `scroll_view`, `window`.
- **Refused when:** the host supplied no semantic snapshot.

Request:

```json
{ "jsonrpc": "2.0", "id": 10, "method": "tools/call", "params": { "name": "semantic.tree", "arguments": {} } }
```

Response (abridged):

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "group - window\ngroup - \nlabel form_title Widget Form\nbutton form_press press me\ncheckbox form_armed form_armed\n"
      }
    ],
    "isError": false
  }
}
```

The tree is rebuilt from the live UI tree for every command, so a write or an
input is visible to the next `semantic.tree` after the frame settles.

### semantic.find

Resolves one identity and reports what it is.

- **Arguments:** `id` (required).
- **Answer:** `text`, `"<role> <name>"`.
- **Refused when:** the host supplied no snapshot, `id` is missing, or no node
  carries the id.

Request:

```json
{ "jsonrpc": "2.0", "id": 11, "method": "tools/call", "params": { "name": "semantic.find", "arguments": { "id": "form_press" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 11, "result": { "content": [{ "type": "text", "text": "button press me" }], "isError": false } }
```

### semantic.value

Reads a node's primary value with its kind.

- **Arguments:** `id` (required).
- **Answer:**
  - `number` for a control that publishes a bounded range (Slider,
    NumericStepper). A non-integral number is spelled with six fractional digits
    (`0.500000`); an integral number has no fraction (`30`).
  - `boolean` `yes` / `no` for a checked / unchecked checkable control.
  - `text` for an indeterminate check (`indeterminate`), a text field (even when
    empty), or any node whose published value is non-empty. A menu item, for
    example, publishes its shortcut as the value.
- **Refused when:** the host supplied no snapshot, `id` is missing or extra, no
  node carries the id, the range value is not finite, or the node has no primary
  value (a button or a label, for example).

Number:

```json
{ "jsonrpc": "2.0", "id": 12, "method": "tools/call", "params": { "name": "semantic.value", "arguments": { "id": "form_level" } } }
```

```json
{ "jsonrpc": "2.0", "id": 12, "result": { "content": [{ "type": "text", "text": "0.500000" }], "isError": false } }
```

Boolean:

```json
{ "jsonrpc": "2.0", "id": 13, "method": "tools/call", "params": { "name": "semantic.value", "arguments": { "id": "form_armed" } } }
```

```json
{ "jsonrpc": "2.0", "id": 13, "result": { "content": [{ "type": "text", "text": "no" }], "isError": false } }
```

Text:

```json
{ "jsonrpc": "2.0", "id": 14, "method": "tools/call", "params": { "name": "semantic.value", "arguments": { "id": "form_name" } } }
```

```json
{ "jsonrpc": "2.0", "id": 14, "result": { "content": [{ "type": "text", "text": "Ada" }], "isError": false } }
```

The generic state read (`focused`, `disabled`, `selected`, `expanded`, `check`,
`selection`, `role`, `name`, `range`) is not part of this command; it has its own
command, [`semantic.read`](#semanticread).

### semantic.read

Reads one typed semantic property of a node, which is what a test asserts against
without parsing the tree dump.

- **Arguments:** `id` and `property` (both required).
- **Answer:**
  - `boolean` `yes` / `no` for `focused`, `disabled`, `read_only`, `selected`,
    `expanded`, `indeterminate` and `multiline`.
  - `boolean` `yes` / `no` for `check`, and `text` `indeterminate` for the third
    check state, which has no boolean spelling. This is the same pair of kinds
    `semantic.value` answers for the same state.
  - `text` for `selection`, the half-open byte range spelled `start:end` (`0:0` when
    the caret is collapsed), for `role`, spelled as the dump spells it, and for
    `name`.
  - `json` for `range`, an object with `value`, `minimum`, `maximum` and `step`.
- **Refused when:** the host supplied no snapshot, `id` or `property` is missing,
  a third argument is present, no node carries the id, the property is unknown,
  `range` is read from a node that publishes none, `check` is read from a node
  that is not checkable, or one of the range numbers is not finite.

Boolean:

```json
{ "jsonrpc": "2.0", "id": 40, "method": "tools/call", "params": { "name": "semantic.read", "arguments": { "id": "form_press", "property": "focused" } } }
```

```json
{ "jsonrpc": "2.0", "id": 40, "result": { "content": [{ "type": "text", "text": "no" }], "isError": false } }
```

Selection:

```json
{ "jsonrpc": "2.0", "id": 41, "method": "tools/call", "params": { "name": "semantic.read", "arguments": { "id": "form_name", "property": "selection" } } }
```

```json
{ "jsonrpc": "2.0", "id": 41, "result": { "content": [{ "type": "text", "text": "1:4" }], "isError": false } }
```

Range:

```json
{ "jsonrpc": "2.0", "id": 42, "method": "tools/call", "params": { "name": "semantic.read", "arguments": { "id": "form_level", "property": "range" } } }
```

```json
{ "jsonrpc": "2.0", "id": 42, "result": { "content": [{ "type": "text", "text": "{\"maximum\":1,\"minimum\":0,\"step\":0.050000,\"value\":0.500000}" }], "isError": false } }
```

An unknown property is refused by name (`no such property: nope`), because a typo
must not read as a state, and `range` on a node without one is refused rather than
answered as zeros.

### semantic.bounds

Reads the rect the layout arranged for a node.

- **Arguments:** `id` (required).
- **Answer:** `json`, an object with `x`, `y`, `width` and `height` in logical
  window pixels. The coordinates are the same space pointer commands use.
- **Refused when:** the host supplied no snapshot, `id` is missing or extra, or no
  node carries the id.

Request:

```json
{ "jsonrpc": "2.0", "id": 15, "method": "tools/call", "params": { "name": "semantic.bounds", "arguments": { "id": "form_press" } } }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 15,
  "result": {
    "content": [{ "type": "text", "text": "{\"x\":16,\"y\":76,\"width\":388,\"height\":32}" }],
    "isError": false
  }
}
```

Bounds are a query, not a suffix on every `semantic.tree` line.

### semantic.actions

Lists the actions a node publishes.

- **Arguments:** `id` (required).
- **Answer:** `text`, the action names separated by one space; empty when the node
  exposes none.
- **Refused when:** the host supplied no snapshot, `id` is missing, or no node
  carries the id.

Request:

```json
{ "jsonrpc": "2.0", "id": 16, "method": "tools/call", "params": { "name": "semantic.actions", "arguments": { "id": "form_press" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 16, "result": { "content": [{ "type": "text", "text": "activate" }], "isError": false } }
```

### semantic.invoke

Runs one action a node publishes.

- **Arguments:** `id`, `action` (both required).
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no snapshot, `id` or `action` is missing, no
  node carries the id, the node has no action with that name, or the node is
  disabled. A disabled node's action is refused by the semantic node itself and
  reported as `no such action: <action>`.

Request:

```json
{ "jsonrpc": "2.0", "id": 17, "method": "tools/call", "params": { "name": "semantic.invoke", "arguments": { "id": "form_press", "action": "activate" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 17, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

An invocation is the semantic activation path, not a pointer press. Use
`input.click` when the pointer route itself is under test.

### semantic.set_value

Writes the text an editable node edits.

- **Arguments:** `id`, `value` (both required; `value` may be empty).
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no snapshot, `id` or `value` is missing,
  extra arguments are present, no node carries the id, the node has no writable
  value, or the node refuses the write (read-only, disabled, invalid input).

Request:

```json
{ "jsonrpc": "2.0", "id": 18, "method": "tools/call", "params": { "name": "semantic.set_value", "arguments": { "id": "form_name", "value": "Ada" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 18, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

The effect is the value the next `semantic.tree` or `semantic.value` reports after
the frame settles.

### semantic.set_focused

Records the focused state a semantic node reports.

- **Arguments:** `id`, `value` (`0` or `1`; both required).
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no snapshot, `id` or `value` is missing, no
  node carries the id, or `value` is not exactly `0` or `1`.

This command **records the semantic state**; it does not move the input focus.
`input.type` and `input.click` move the real focus. A client that wants a control
to receive keyboard input should click or type into it.

The embedded host rebuilds the semantic tree from the live UI tree for every
command, so a flag written this way is discarded before the next command. It is
useful with an app-supplied external tree (`AutomationHost::start_mcp` with a
`SemanticTree*`, or a unit-level session), not as a way to persist state through
the embedded host.

Request:

```json
{ "jsonrpc": "2.0", "id": 19, "method": "tools/call", "params": { "name": "semantic.set_focused", "arguments": { "id": "form_press", "value": "1" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 19, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

### input.type

Focuses a text field and delivers text through the real edit path.

- **Arguments:** `id`, `text` (both required; `text` may be empty).
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no input target, no window carries a tree,
  `id` or `text` is missing, extra arguments are present, no node carries the id,
  the node edits no text, the control is disabled or read-only, the node cannot
  take focus, or the window is modal-blocked.

The field is focused first, then one `TextInput` event is delivered per Unicode
code point. The edit goes through the same path as a keyboard, including the
change handler and the undo stack.

Request:

```json
{ "jsonrpc": "2.0", "id": 20, "method": "tools/call", "params": { "name": "input.type", "arguments": { "id": "form_name", "text": "Ada Lovelace" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 20, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

### input.click

Presses a control with the primary pointer button.

- **Arguments:** `id` (required).
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no input target, no window carries a tree,
  `id` is missing or extra, no node carries the id, the node has no point it
  accepts, the node is detached, the window is modal-blocked, or the window refuses
  the event.

The command aims at a point the node itself accepts. It uses the centre when the
whole arranged plate hits, and otherwise probes the edges and centre lines — a
checkbox's square is smaller than its arranged bounds, so its click lands on the
square. It delivers a pointer move, a left press and a left release at that point.
The real path runs: hover synthesis, hit testing, pointer focus policy, capture and
the control's own activation.

Request:

```json
{ "jsonrpc": "2.0", "id": 21, "method": "tools/call", "params": { "name": "input.click", "arguments": { "id": "form_press" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 21, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

### input.pointer

Delivers one raw pointer event.

- **Arguments:** `id`, `action`, optional `x`, optional `y`.
- **Actions:** `press`, `move`, `release`, `cancel`.
- **Answer:** `text`, empty on success.
- **Point:** when `x` and `y` are omitted, the point is the node's centre. When
  they are present, they are logical window coordinates and may lie outside the
  node; the id still names the window. `x` and `y` must be supplied together.
- **Refused when:** the host supplied no input target, no window carries a tree,
  `id` or `action` is missing, there are more than four arguments, the action is
  not one of the four, `x` is present without `y`, a coordinate is not a finite
  number, no node carries the id, a default point is needed but the node has no
  area, the node is detached, the window is modal-blocked, or the window refuses
  the event.

A drag is a press, one or more moves, and a release:

```json
{ "jsonrpc": "2.0", "id": 22, "method": "tools/call", "params": { "name": "input.pointer", "arguments": { "id": "form_level", "action": "press", "x": "80", "y": "240" } } }
```

```json
{ "jsonrpc": "2.0", "id": 23, "method": "tools/call", "params": { "name": "input.pointer", "arguments": { "id": "form_level", "action": "move", "x": "300", "y": "240" } } }
```

```json
{ "jsonrpc": "2.0", "id": 24, "method": "tools/call", "params": { "name": "input.pointer", "arguments": { "id": "form_level", "action": "release", "x": "300", "y": "240" } } }
```

Each of the three answers an empty text block:

```json
{ "jsonrpc": "2.0", "id": 22, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

A cancel ends a drag without a release. It may carry the point the gesture
reached, or use the node's centre:

```json
{ "jsonrpc": "2.0", "id": 25, "method": "tools/call", "params": { "name": "input.pointer", "arguments": { "id": "form_level", "action": "cancel" } } }
```

`press`, `move` and `release` use the left button. The right-button context-menu
gesture is not exposed as a tool; a test can reach the context menu through the
normal key or pointer path when a control publishes that gesture.

A client that needs a point reads `semantic.bounds` first:

```json
{ "jsonrpc": "2.0", "id": 26, "method": "tools/call", "params": { "name": "semantic.bounds", "arguments": { "id": "form_level" } } }
```

### input.wheel

Delivers one normalized wheel event to the window that owns the named node.

- **Arguments:** `id` (required), `delta` (required), optional `precision`, optional
  `x` and `y`.
- **Delta:** a finite signed number. Positive scrolls toward the start and negative
  scrolls toward the end, which is the convention every platform adapter and scroll
  container follows (§8.3).
- **Precision:** `line` (default) counts notches and moves one themed line extent;
  `point` counts surface distance and moves by the delta itself. A client may omit it
  to get a physical wheel notch.
- **Point:** when `x` and `y` are omitted, the point is the node's centre. When they
  are present, they are logical window coordinates and may lie outside the node;
  the id still names the window. `x` and `y` must be supplied together, and a named
  call that sets both must also name `precision`.
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no input target, no window carries a tree,
  `id` or `delta` is missing, `delta` is not a finite number, `precision` is not one
  of the two units, there are more than five arguments, `x` is present without `y`,
  no node carries the id, a default point is needed but the node has no area, the
  node is detached, the window is modal-blocked, or the window refuses the event.

The event goes through the same input path a physical wheel takes, so a scrollable
control under the point consumes it and an enclosing viewport sees a notch the inner
one could not use.

```json
{ "jsonrpc": "2.0", "id": 60, "method": "tools/call", "params": { "name": "input.wheel", "arguments": { "id": "form_list", "delta": "-1" } } }
```

A point-precision notch over a specific spot:

```json
{ "jsonrpc": "2.0", "id": 61, "method": "tools/call", "params": { "name": "input.wheel", "arguments": { "id": "form_list", "delta": "-40", "precision": "point", "x": "100", "y": "720" } } }
```

### input.key

Delivers a normalized key press to the active window.

- **Arguments:** `key` (required), `modifiers` (optional).
- **Answer:** `text`, empty on success.
- **Key names:** `tab`, `return`, `escape`, `space`, `delete`, `backspace`, `home`,
  `end`, `page_up`, `page_down`, `menu`, `left`, `right`, `up`, `down`, `a`–`z`,
  `0`–`9`, `f1`–`f12`.
- **Modifiers:** `shift`, `control`, `option`, `command`, joined with `+`; empty or
  absent means no modifier. Unknown names are refused.
- **Refused when:** the host supplied no input target, no window carries a tree,
  `key` is missing, extra arguments are present, the key or a modifier is unknown,
  the window is modal-blocked, or the window refuses the event.

The command sends a KeyDown and a KeyUp through the application's input path. The
input controller delivers the press to the focused control, focus traversal, dialog
keys and the accelerator table. It deliberately ignores a key release, because a
release carries no traversal and no activation, so the observable effect on the
tree is the press. Text entry still belongs to `input.type`.

Return on the focused button:

```json
{ "jsonrpc": "2.0", "id": 27, "method": "tools/call", "params": { "name": "input.key", "arguments": { "key": "return" } } }
```

```json
{ "jsonrpc": "2.0", "id": 27, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

Control S for an application accelerator:

```json
{ "jsonrpc": "2.0", "id": 28, "method": "tools/call", "params": { "name": "input.key", "arguments": { "key": "s", "modifiers": "control" } } }
```

Tab to move focus:

```json
{ "jsonrpc": "2.0", "id": 29, "method": "tools/call", "params": { "name": "input.key", "arguments": { "key": "tab" } } }
```

Shift and control together:

```json
{ "jsonrpc": "2.0", "id": 30, "method": "tools/call", "params": { "name": "input.key", "arguments": { "key": "a", "modifiers": "control+shift" } } }
```

### capture.screenshot

Reads the active window's last presented frame.

- **Arguments:** `format` (optional; `png` default or `raw`).
- **Answer:** `image` for `png`, `binary` for `raw`.
- **Refused when:** the host supplied no capture source, the format is neither
  `png` nor `raw`, no window is bound, the window has not presented a frame at its
  current size, or the PNG encoder fails.

The capture reads the last presented frame after the render executor settles. It
does not rebuild a frame. A mutation issued in the same transport pass is visible
only to a later capture.

PNG request:

```json
{ "jsonrpc": "2.0", "id": 31, "method": "tools/call", "params": { "name": "capture.screenshot", "arguments": {} } }
```

PNG response:

```json
{
  "jsonrpc": "2.0",
  "id": 31,
  "result": {
    "content": [{ "type": "image", "data": "<base64 png>", "mimeType": "image/png" }],
    "isError": false
  }
}
```

`png` un-premultiplies the surface, so it is lossy below alpha 255. Use `raw` for
pixel-exact comparisons.

Raw request:

```json
{ "jsonrpc": "2.0", "id": 32, "method": "tools/call", "params": { "name": "capture.screenshot", "arguments": { "format": "raw" } } }
```

Raw response:

```json
{
  "jsonrpc": "2.0",
  "id": 32,
  "result": {
    "content": [
      {
        "type": "resource",
        "resource": {
          "uri": "albor://capture.screenshot/1",
          "mimeType": "application/octet-stream",
          "blob": "<base64 ALBRSHOT frame>"
        }
      }
    ],
    "isError": false
  }
}
```

The raw payload is a 24-byte header followed by the pixels:

| offset | bytes | field |
| --- | --- | --- |
| 0 | 8 | ASCII `ALBRSHOT` |
| 8 | 4 | width, little-endian `uint32` |
| 12 | 4 | height, little-endian `uint32` |
| 16 | 4 | stride in bytes, little-endian `uint32` |
| 20 | 4 | pixel format, little-endian `uint32`; `1` is premultiplied RGBA8 |
| 24 | … | `height × stride` bytes |

### command.list

Dumps every command the application declared.

- **Arguments:** none.
- **Answer:** `text`, one line per command:

  ```text
  <id> <enabled|disabled> <visible|hidden> <label>
  ```

  An unnamed command appears with the id `-` and cannot be invoked by automation.
- **Refused when:** the host supplied no command registry.

Request:

```json
{ "jsonrpc": "2.0", "id": 33, "method": "tools/call", "params": { "name": "command.list", "arguments": {} } }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 33,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "file.save enabled visible Save\nfile.save_as disabled visible Save As\nedit.undo enabled visible Undo\n"
      }
    ],
    "isError": false
  }
}
```

### command.get

Reads one command's published state.

- **Arguments:** `id` (required).
- **Answer:** `text`, `"<enabled|disabled> <visible|hidden> <label>"`.
- **Refused when:** the host supplied no command registry, `id` is missing, or no
  command carries the id.

Request:

```json
{ "jsonrpc": "2.0", "id": 34, "method": "tools/call", "params": { "name": "command.get", "arguments": { "id": "file.save" } } }
```

Response:

```json
{ "jsonrpc": "2.0", "id": 34, "result": { "content": [{ "type": "text", "text": "enabled visible Save" }], "isError": false } }
```

### command.invoke

Runs a command through the same action a user invokes.

- **Arguments:** `id`, optional `argument` (a string).
- **Answer:** `text`, empty on success. A command that requires a selection answers
  the number of selected items as text.
- **Refused when:** the host supplied no command registry, `id` is missing, no
  command carries the id, or the command refuses: disabled, hidden, no selection,
  or no action.

Request:

```json
{ "jsonrpc": "2.0", "id": 35, "method": "tools/call", "params": { "name": "command.invoke", "arguments": { "id": "file.save" } } }
```

```json
{ "jsonrpc": "2.0", "id": 35, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

With a row argument:

```json
{ "jsonrpc": "2.0", "id": 36, "method": "tools/call", "params": { "name": "command.invoke", "arguments": { "id": "list.delete", "argument": "row-42" } } }
```

```json
{ "jsonrpc": "2.0", "id": 36, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

A command that requires a selection answers the number of selected items it ran over:

```json
{ "jsonrpc": "2.0", "id": 37, "method": "tools/call", "params": { "name": "command.invoke", "arguments": { "id": "edit.replace" } } }
```

```json
{ "jsonrpc": "2.0", "id": 37, "result": { "content": [{ "type": "text", "text": "5" }], "isError": false } }
```

The invocation source is `Automation`, so a handler can branch on where the
invocation came from. Enablement is the same object the UI presents; automation
cannot drive a state a user cannot.

### command.can_undo

- **Arguments:** none.
- **Answer:** `text`, `yes` or `no`.
- **Refused when:** the host supplied no command registry.

```json
{ "jsonrpc": "2.0", "id": 37, "method": "tools/call", "params": { "name": "command.can_undo", "arguments": {} } }
```

```json
{ "jsonrpc": "2.0", "id": 37, "result": { "content": [{ "type": "text", "text": "yes" }], "isError": false } }
```

### command.undo

- **Arguments:** none.
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no command registry, or there is nothing to
  undo.

```json
{ "jsonrpc": "2.0", "id": 38, "method": "tools/call", "params": { "name": "command.undo", "arguments": {} } }
```

```json
{ "jsonrpc": "2.0", "id": 38, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

### command.redo

- **Arguments:** none.
- **Answer:** `text`, empty on success.
- **Refused when:** the host supplied no command registry, or there is nothing to
  redo.

```json
{ "jsonrpc": "2.0", "id": 39, "method": "tools/call", "params": { "name": "command.redo", "arguments": {} } }
```

```json
{ "jsonrpc": "2.0", "id": 39, "result": { "content": [{ "type": "text", "text": "" }], "isError": false } }
```

## A complete session

The following transcript drives the form sample. It assumes the host is already
running and the client has read `host.json` and the token file.

Initialize:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"_meta":{"albor/token":"<token>"}}}
```

```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":"albor","version":"1"}}}
```

Find the button and read its geometry:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"semantic.find","arguments":{"id":"form_press"}}}
```

```json
{"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"button press me"}],"isError":false}}
```

```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"semantic.bounds","arguments":{"id":"form_press"}}}
```

```json
{"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"{\"x\":16,\"y\":76,\"width\":388,\"height\":32}"}],"isError":false}}
```

Read the sample's numbers, boolean and empty text field:

```json
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"semantic.value","arguments":{"id":"form_level"}}}
```

```json
{"jsonrpc":"2.0","id":4,"result":{"content":[{"type":"text","text":"0.500000"}],"isError":false}}
```

```json
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"semantic.value","arguments":{"id":"form_armed"}}}
```

```json
{"jsonrpc":"2.0","id":5,"result":{"content":[{"type":"text","text":"no"}],"isError":false}}
```

```json
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"semantic.value","arguments":{"id":"form_name"}}}
```

```json
{"jsonrpc":"2.0","id":6,"result":{"content":[{"type":"text","text":""}],"isError":false}}
```

Type into the field:

```json
{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"input.type","arguments":{"id":"form_name","text":"Ada Lovelace"}}}
```

```json
{"jsonrpc":"2.0","id":7,"result":{"content":[{"type":"text","text":""}],"isError":false}}
```

The typed read now returns the text the field holds:

```json
{"jsonrpc":"2.0","id":70,"method":"tools/call","params":{"name":"semantic.value","arguments":{"id":"form_name"}}}
```

```json
{"jsonrpc":"2.0","id":70,"result":{"content":[{"type":"text","text":"Ada Lovelace"}],"isError":false}}
```

Click the button, then press Return while it holds the focus:

```json
{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"input.click","arguments":{"id":"form_press"}}}
```

```json
{"jsonrpc":"2.0","id":8,"result":{"content":[{"type":"text","text":""}],"isError":false}}
```

```json
{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"input.key","arguments":{"key":"return"}}}
```

```json
{"jsonrpc":"2.0","id":9,"result":{"content":[{"type":"text","text":""}],"isError":false}}
```

The sample label now reports two activations:

```json
{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"semantic.find","arguments":{"id":"form_presses"}}}
```

```json
{"jsonrpc":"2.0","id":10,"result":{"content":[{"type":"text","text":"label presses 2"}],"isError":false}}
```

Drag the slider to three quarters across, using its bounds:

```json
{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"semantic.bounds","arguments":{"id":"form_level"}}}
```

```json
{"jsonrpc":"2.0","id":11,"result":{"content":[{"type":"text","text":"{\"x\":16,\"y\":240,\"width\":388,\"height\":20}"}],"isError":false}}
```

The track runs from `x=16` to `x=404`; the press starts at a quarter and the move ends at three quarters, on the vertical centre `y=240+10=250`:

```json
{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"input.pointer","arguments":{"id":"form_level","action":"press","x":"113","y":"250"}}}
```

```json
{"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"input.pointer","arguments":{"id":"form_level","action":"move","x":"307","y":"250"}}}
```

```json
{"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"input.pointer","arguments":{"id":"form_level","action":"release","x":"307","y":"250"}}}
```

```json
{"jsonrpc":"2.0","id":15,"method":"tools/call","params":{"name":"semantic.value","arguments":{"id":"form_level"}}}
```

```json
{"jsonrpc":"2.0","id":15,"result":{"content":[{"type":"text","text":"0.765027"}],"isError":false}}
```

The exact value depends on the theme's slider thumb extent; the assertion that
matters is that the drag moved it above 0.5. Capture the frame:

```json
{"jsonrpc":"2.0","id":16,"method":"tools/call","params":{"name":"capture.screenshot","arguments":{}}}
```

```json
{"jsonrpc":"2.0","id":16,"result":{"content":[{"type":"image","data":"<base64 png>","mimeType":"image/png"}],"isError":false}}
```

A refusal carries the layer's reason:

```json
{"jsonrpc":"2.0","id":17,"method":"tools/call","params":{"name":"semantic.find","arguments":{"id":"form_missing"}}}
```

```json
{"jsonrpc":"2.0","id":17,"error":{"code":-32000,"message":"no node with id: form_missing"}}
```

## The raw automation wire

The MCP adapter is the supported client protocol. The automation layer also has a
line protocol used by tests and in-process clients that own an `AutomationSession`;
the embedded host on `host.json` accepts MCP only, so a raw `!auth` line sent
there is not JSON-RPC and is refused with `-32602`. A raw connection is
authenticated once, then commands run one per line.

```text
!auth <token>
!ok <kind> <payload>
!refused <reason>
```

- A successful handshake answers `!ok text ` with an empty payload. A failed one
  answers `!refused authentication failed`.
- A command before the handshake answers `!refused authentication required`.
- The command line is the command name followed by its arguments separated by one
  space. An argument escapes `%` as `%25`, space as `%20`, tab as `%09`, carriage
  return as `%0D` and newline as `%0A`.
- `<kind>` is `text`, `number`, `boolean`, `json`, `image` or `binary`.
- `image` and `binary` payloads are base64. The other payloads escape `\`, newline
  and carriage return as `\\`, `\n` and `\r`.
- A refusal carries no payload and never reaches application code when the command
  is unknown or not allowed.

Examples:

```text
!auth 8fJ2...token...
!ok text 
semantic.find form_press
!ok text button press me
semantic.value form_level
!ok number 0.500000
semantic.bounds form_press
!ok json {"x":16,"y":76,"width":388,"height":32}
semantic.find form_missing
!refused no node with id: form_missing
```

The MCP adapter hides this framing: it reads a JSON-RPC request, renders one
command line, dispatches it through the authenticated session, and wraps the
answer in a content block. A tool call is therefore exactly one raw command.

## Security and limits

- **Local only.** The endpoint is a Unix domain socket or a named pipe. There is
  no network listener.
- **Per-session token.** The host mints a 32-character token per run, drawing every
  character from the system entropy source, and publishes it in an owner-only file.
  The token is not reused across runs, is compared in constant time, and a session
  with no token never authenticates.
- **Owner-only directory.** The host directory, descriptor, token and socket are
  readable by the owning user only. On Linux a connection is additionally checked
  against the peer's credentials and refused unless it comes from the same user.
  On Windows the named pipe is restricted to the current user's SID.
- **Bounded request lines.** A client that sends more than 1 MiB without a newline
  is disconnected on every platform, so one silent peer cannot grow the server's
  pending line without limit.
- **Allow-list.** Registration is not permission. The host explicitly permits the
  standard surface it wires; a command outside that list is refused before its
  handler is reached.
- **No Release path.** The automation core and host are excluded at link time from
  Release, so a production build has no socket, no token and no command surface.
- **One active window.** The newest bound main window owns the semantic and input
  surface. A popup does not replace its owner. A hand-drawn window can be captured
  but has no semantic tree.
- **Synchronous UI thread.** Commands run on the UI thread between frames. A
  command that closes a window is deferred, not run on the command's own stack.
  Writes and input become visible to the next frame.

## Troubleshooting

| symptom | likely cause |
| --- | --- |
| no `host.json` | the application was not built with `ALBOR_AUTOMATION`, no main window was created, or the host could not create its directory. Check the host's stderr line `serving <descriptor>`. |
| `authentication failed` | the token file is stale, or the client read the wrong host directory. Re-read `host.json` and `token`. |
| `no such command: <name>` | the command is not registered in this build. See the family tables. |
| `command not allowed: <name>` | the host did not permit the command. The standard surface is permitted automatically; an application-specific command is reached through `command.*`, not as its own tool. |
| `semantic.bounds` refuses an id | the id is not an author-assigned `id:` on the active window. Names, roles and positions are not selectors. |
| `semantic.value` refuses a node | the node has no primary value. A button or label has none; read its states with `semantic.read` instead. |
| `capture.screenshot` says the frame is not at its current size | the window has not presented since creation or resize. Pump the application and capture again. |
| `tools/list` answers `authentication failed` | the connection did not complete `initialize` with the token from the `token` file. |
| no `tools/list` reply | the client is not connected to the endpoint in `host.json`, or the host has no active window. |
| a click does nothing | the node is disabled, another node covers the point, or the window is modal-blocked. Read `semantic.bounds` and use `input.pointer` with a point on the visible control. |

## See also

- [Tooling](tooling.md) — the compiler, runtime, language server and preview host.
- [Commands](concepts/commands.md) — the shared action object behind `command.*`.
- [Properties and bindings](concepts/bindings.md) — the reactive model behind the
  values the tree dump publishes.
- [Status and platforms](status.md) — which configurations build the host.
