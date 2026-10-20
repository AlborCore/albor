# Transparent MCP Automation Host — Implementation Plan

> **Working document.** This is the review copy of the implementation plan. It is intentionally untracked and **must not be committed** with the implementation.
>
> Status: planning only. No repository source, build, test, documentation, or issue changes have been made yet; this file is the only new artifact. Implementation starts only after this plan is approved.

## 1. Outcome

An external application enables embedded MCP automation by build configuration and link selection, not by application code:

- configure `ALBOR_AUTOMATION=ON` together with `ALBOR_DEV=ON` or `ALBOR_TEST=ON`;
- link `albor-devrt-auto` in Dev, or `albor-automation-auto` in Test;
- keep using `Application::instance()` and `Application::create_window()` as before;
- write no registry, allow-list, socket, token, gateway, timer, or `serve_one()` code.

The automation host is a self-installing translation unit inside a separately linked target. It attaches to the existing `dev::bind_root` / `dev::unbind_root` seam, starts on the first real UI tree, serves MCP on the local transport, pumps itself from the UI thread, and stops when the last bound tree goes away.

The application source is identical in Release, where the automation targets do not exist and the link branch is false.

## 2. Hard Constraints

- **No `Application` API changes.** `enable_mcp`, `disable_mcp`, and `mcp_endpoint` remain rejected.
- **No application-side wiring.** The host owns the registry, protocol surface, transport, token, discovery file, and pump.
- **Link-time exclusion.** Release carries no automation host, no MCP adapter, and no transport. A Release reference to one of their symbols fails the link (AD-04, AD-10).
- **No runtime off switch.** When the transparent host is linked, MCP is on. The only way to omit it is not to link it.
- **MCP is an adapter only.** It reaches the application through `AutomationSession` / `AutomationRegistry`, never through `Application`, `CommandRegistry`, or `UiNode` directly (AD-20).
- **Explicit identity is the action allow-list.** A node or command is addressable only when the author assigned an explicit `id:`; command enablement and visibility still apply (AD-11, §18.1).
- **Layout invariance.** No runtime class layout or virtual interface changes between configurations. The new host behavior lives in separate, link-selected translation units (AD-02, §2.3).
- **No platform or Skia types in public headers.** The host header is internal to `automation/`; the application never includes it (§3.5, AD-05).
- **Follow the source-file convention.** Portable host sources are `.cpp` / `.h`; any file that reaches a platform API carries the platform infix.

## 3. Decisions Recorded

1. **`ALBOR_AUTOMATION` is independent of `ALBOR_DEV` and `ALBOR_TEST`.** It may be paired with either, or both. It is still forbidden in Release.
2. **Automation and `devrt` are independent build concepts.** A Dev build may have `devrt` without automation; a Test build has automation without `devrt`.
3. **The transparent host is owned by `automation/`, not `devrt/` or `inspector/`.** `devrt-auto` includes it as an umbrella; Test links it directly. `inspector/` is the scene inspector and is not the MCP server.
4. **The default surface is full.** The host registers the five semantic commands and the six command commands, and explicitly permits all of them. `AutomationRegistry` remains deny-by-default as a primitive; the host is the component that opts into the standard surface.
5. **No per-application narrowing API is added in this change.** The effective capability boundary is the application's explicit `id:` set plus command enablement/visibility. A host that needs a narrower protocol surface remains a manual integration over `albor-automation`; a build-level deny list is a possible follow-up, not part of the transparent path.
6. **One active root.** The most recently bound tree is active. Unbinding it falls back to the next most recently bound tree; the host stops only when no bound tree remains.
7. **MCP-only on the transparent path.** The transport's default raw automation protocol remains available for manually wired hosts and tests. The transparent host installs an MCP connection factory.
8. **The MCP protocol version stays `2024-11-05` in this change.** The adapter already speaks it; updating to a newer revision is a separate client-compatibility task.
9. **Endpoint discovery is a file contract.** The host publishes `host.json` beside the socket and token under a per-process temporary directory.

## 4. External Application Contract

### 4.1 CMake

Set the framework options before `FetchContent_MakeAvailable` or `add_subdirectory`:

```cmake
set(ALBOR_DEV        ON  CACHE BOOL "" FORCE)
set(ALBOR_TEST       OFF CACHE BOOL "" FORCE)
set(ALBOR_AUTOMATION ON  CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(albor)

if(ALBOR_DEV)
    target_link_libraries(my-app PRIVATE albor-devrt-auto)
elseif(ALBOR_TEST AND ALBOR_AUTOMATION)
    target_link_libraries(my-app PRIVATE albor-automation-auto)
endif()
```

- In Dev, `albor-devrt-auto` carries both the reload extension and, when `ALBOR_AUTOMATION` is on, the automation host.
- In Test, `albor-automation-auto` carries the host without `devrt`.
- In Release, neither branch links, and no symbol from the host is referenced.

### 4.2 Application source

The application continues to use the documented facade:

```cpp
const albor::WindowHandle handle =
    albor::Application::instance().create_window(options, panel);
```

Required properties of the host's transparent path:

- the application uses the process-wide `Application::instance()`, because that is where the host reaches the existing timer scheduler and command registry;
- the window is created with a real root tree, as `create_window(options, panel)` does; a raw hand-drawn window binds no tree and starts no host;
- no application source includes `automation/` or references an automation symbol.

A harness that constructs a private `Application` object is not the transparent path. It keeps the manual automation integration, because the host has no Application API to discover a non-singleton instance.

### 4.3 What the application does not do

- It does not construct `AutomationRegistry`, `AutomationSession`, `McpServer`, or `AutomationTransport`.
- It does not create a socket, directory, token, or discovery file.
- It does not schedule `serve_one()`.
- It does not choose commands, allow-lists, roles, or endpoints.
- It has no runtime on/off switch.

### 4.4 Client-side discovery

The host publishes a per-process directory. On macOS and Linux the primary base is `$TMPDIR` (Linux prefers `$XDG_RUNTIME_DIR` when set); on Windows, the directory would be under `%LOCALAPPDATA%` when a transport exists:

```text
<base>/albor-mcp/<pid>/
  mcp.sock        # local transport
  token           # authentication token, 0600
  host.json       # discovery descriptor, 0600
```

`host.json` is written after the transport listens:

```json
{
  "pid": 12345,
  "process": "my-app",
  "socket": "/var/folders/.../T/albor-mcp/12345/mcp.sock",
  "token": "/var/folders/.../T/albor-mcp/12345/token",
  "protocol": "mcp",
  "protocolVersion": "2024-11-05",
  "started": 1760000000000
}
```

- Directory permissions are `0700`; socket, token, and descriptor are `0600`.
- A client scans for `host.json`, checks that the pid is alive, reads `token`, and passes the token in the MCP `initialize` request:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "_meta": { "albor/token": "<contents of token>" },
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": { "name": "client", "version": "1" }
  }
}
```

- The raw transport's `!auth <token>` line remains the manual-host protocol. The transparent host does not accept it as an MCP request.
- If the socket path would exceed the platform limit, the host falls back to a short `/tmp/albor-<uid>/<pid>/` directory. The discovery file always names the actual socket and token paths.
- A stale directory from a crashed process is ignored by checking the pid. The host removes its own directory on stop; a future bridge may clean dead entries.

## 5. Build Graph

### 5.1 Options

| `ALBOR_DEV` | `ALBOR_TEST` | `ALBOR_AUTOMATION` | Result |
| :---: | :---: | :---: | :--- |
| OFF | OFF | OFF | Release |
| OFF | OFF | ON | configure error (Release may not carry automation) |
| ON | OFF | OFF | Dev with `devrt`, no automation |
| ON | OFF | ON | Dev with `devrt` and the transparent automation host |
| OFF | ON | OFF | Test extras without automation |
| OFF | ON | ON | Test with the transparent automation host, no `devrt` |
| ON | ON | ON | coverage: `devrt` and the automation host |

Presets already set `ALBOR_AUTOMATION=ON` for `debug`, `test`, and `coverage`. The option description changes to "Build the automation core and embedded host (Dev/Test only, AD-10)".

`ALBOR_TEST` no longer implies automation by itself; its description names only the headless backend and software renderer. The Test preset still sets both flags.

### 5.2 Targets

```text
albor-automation            existing core: protocol, session, registry, transports, MCP adapter
albor-automation-host       new static archive: host state, connection wiring, auto-install
albor-automation-auto       new INTERFACE: whole-archive albor-automation-host
albor-devrt                 existing reload extension, unchanged dependencies
albor-devrt-auto            existing INTERFACE: whole-archive albor-devrt
                            + albor-automation-auto when ALBOR_AUTOMATION
```

- `albor-automation-host` links `albor-automation`, `albor-application`, `albor-commands`, `albor-semantics`, `albor-ui`, and `albor-core`.
- `albor-automation-auto` is the Test-facing and direct-Dev umbrella.
- `albor-devrt-auto` remains the one-link Dev choice. It includes the automation umbrella only when `ALBOR_AUTOMATION` is on.
- `albor-automation` remains the primitive that tests and manual hosts link. It does not auto-start anything.

The new host target links `albor-application`, which is defined later in the root `CMakeLists.txt`. This is already the pattern used by other modules (`devrt` links `albor-ui` before it is added), so target resolution happens at generate time. No root subdirectory reorder is required unless a build check shows otherwise.

### 5.3 File placement

```text
automation/
  automation_connection.h        # connection interface and factory
  automation_transport.*         # existing, gains the connection factory and output buffering
  mcp_connection.h/.cpp          # MCP protocol over the transport seam
  mcp_server.*                   # existing, gains route authentication and command parameter names
  host/
    automation_host.h            # internal host state, not included by applications
    automation_host.cpp          # binding stack, start/stop, registry, timer, descriptor
    automation_host_auto.cpp     # self-installing root observer
    host_paths.h                 # portable host-path contract
    host_paths.macos.mm          # pid, process name, temporary base
    host_paths.linux.c++         # equivalent, for the planned Linux transport
    host_paths.win32.c++         # equivalent, for the future named-pipe transport
```

The core `automation/CMakeLists.txt` changes its source glob from recursive to non-recursive, then adds `add_subdirectory(host)`. The host subdirectory mirrors the platform-extension glob for `.macos.mm`, `.linux.c++`, and `.win32.c++`.

## 6. Core Bind Seam

`core/dev_hook.h` currently stores one bind/unbind pair. Both `devrt` and the automation host need to observe the same tree, and static-initialization order between two whole-archive targets is unspecified. Overwriting the single pair would make one feature silently lose the tree.

Change the seam to a small fixed observer set:

```cpp
using RootBindFunction = void (*)(void* root, void* engine, void* text);
using RootUnbindFunction = void (*)(void* root);

// replace the set, preserving the existing test/reset behavior; null/null clears
void install_root_binder(RootBindFunction bind, RootUnbindFunction unbind);

// append one observer; false when the fixed capacity is exhausted
[[nodiscard]] bool add_root_binding(RootBindFunction bind, RootUnbindFunction unbind);

[[nodiscard]] bool binder_present();
void bind_root(void* root, void* engine, void* text);
void unbind_root(void* root);
```

Implementation rules:

- use a fixed `std::array` of eight entries and an atomic count; registration happens at load time, bind/unbind on the UI thread;
- `install_root_binder` remains the replace/reset operation used by `dev_hook` and `main_window` tests;
- `devrt` changes its installer from `install_root_binder` to `add_root_binding`;
- the automation host also uses `add_root_binding`;
- `bind_root` calls every observer in registration order; `unbind_root` does the same;
- no class layout, vtable, or `Application` API changes.

This is the mechanism that lets a coverage build run `devrt` and automation together, and it keeps each feature's view of the tree independent.

## 7. Host Lifecycle

### 7.1 State

The host is a process-lifetime singleton owned by the auto-install translation unit. It holds:

- a stack of `{root, engine, text}` bindings;
- a `std::unique_ptr<AutomationRegistry>` for the active server;
- a `std::unique_ptr<AutomationTransport>`;
- a `SemanticTree` scratch object used by the semantic snapshot;
- a repeating `TimerHandle`;
- the endpoint paths and the active flag.

The singleton is allocated once and never destroyed by a static destructor. The application's window teardown performs the normal `stop()`; process exit is the fallback.

### 7.2 Bind

`dev::bind_root` is called by `WindowStage` only for a window with a real tree. The host:

1. appends the binding if the same root is not already present;
2. makes that tree the active root;
3. starts the server if it is not running.

If the server is already running, a new binding only changes which tree is active. The registry, endpoint, token, and connected sessions stay alive.

### 7.3 Unbind

`dev::unbind_root` is called before the tree is destroyed. The host removes the matching binding.

- If it removed the active root and another binding remains, the next most recent binding becomes active; the server stays up.
- If no binding remains, the host stops and removes its endpoint.
- An unbind for an unknown root is ignored, so closing one window cannot release another window's binding.

### 7.4 Start

On the transition from zero to one binding, the host:

1. builds a fresh `AutomationRegistry`;
2. registers `register_semantic_commands` with a snapshot that rebuilds a `SemanticTree` from the active root on demand;
3. registers `register_command_commands` with a snapshot of `Application::instance().commands()`;
4. permits each name returned by `semantic_command_names()` and `command_command_names()` when the registry knows it;
5. calls `create_transport(registry)`;
6. installs an MCP connection factory;
7. builds the per-process directory and endpoint;
8. calls `transport->listen(endpoint)`;
9. writes `host.json`;
10. schedules a repeating timer on `Application::instance().timers()`.

The timer interval is 8 ms. At normal display pacing it runs once per pump, which is enough for interactive automation and keeps all work on the UI thread.

The semantic snapshot rebuilds on each semantic command, not on every frame. The command snapshot is always the live application registry.

### 7.5 Pump

The timer callback calls `transport->serve_one()` and nothing else. `serve_one()` accepts at most one client, reads pending lines, dispatches through the connection, and writes replies. The host does not hold a frame hook and does not call `serve_one()` from application code.

A command may close a window. The host sets a `serving` flag around `serve_one()`; an unbind that arrives during a command marks a pending stop instead of destroying the transport under the active call. After `serve_one()` returns, the host completes the pending stop. This prevents a command that closes the last window from turning into a use-after-free.

### 7.6 Stop

On the last unbind, or when startup fails after partial allocation:

1. cancel the timer if scheduled;
2. stop and destroy the transport;
3. remove `host.json`, the token, the socket, and the directory;
4. destroy the registry and connection state;
5. mark the host inactive.

The stop is immediate when the host is not serving. When it is serving, the same steps run after the current `serve_one()` returns.

A startup failure does not discard the bindings. The next bind retries the start. The application continues normally.

### 7.7 Failure behavior

The host fails soft and logs to `stderr`:

- `create_transport` returns null on a platform with no transport (currently Windows): log once, serve nothing;
- directory, permission, path-length, listen, or descriptor failure: log the reason, clean up, leave the application running;
- a client that disconnects or sends malformed input is dropped without affecting the application;
- transport accept/serve errors are logged and retried on the next timer tick.

Failure never turns into an application exception or a failed window creation.

## 8. Transport, Connection, and MCP Seam

### 8.1 Connection interface

The transport currently owns `AutomationSession` and hard-wires raw protocol handling. Add a per-connection seam:

```cpp
class AutomationConnection
{
public:
    virtual ~AutomationConnection() = default;
    // return the reply line, or nullopt for a notification that takes no reply
    virtual std::optional<std::string> handle(std::string_view line) = 0;
};

using ConnectionFactory =
    std::function<std::unique_ptr<AutomationConnection>(std::string_view token)>;

class AutomationTransport
{
public:
    // ...
    virtual void set_connection_factory(ConnectionFactory factory) = 0;
};
```

- The factory receives the token the transport published.
- The default factory builds the raw `SessionConnection`, so existing manual hosts and tests keep their wire behavior.
- The transport creates one connection per accepted client and destroys it when the client closes.
- Notifications return `nullopt`; the transport writes nothing.
- `serve_one()` processes every complete line already buffered before returning.

### 8.2 Raw connection

`SessionConnection` owns `AutomationSession(registry, token)`. Its `handle()` calls `session.handle(line)` and encodes the result exactly as the transport does today. This keeps the existing `automation_transport` tests and manual hosts working.

### 8.3 MCP connection

`McpConnection` owns an `AutomationSession` and an `McpServer`. Its route is:

- `permitted_commands` returns `registry.allowed_commands()`;
- `run` calls `session.handle(line)`, so every tool call still traverses the automation layer;
- `authenticate` accepts the token presented during `initialize` and calls `session.authenticate`;
- `authenticated` reports `session.authenticated()`.

`McpServer` gains two optional route callbacks:

```cpp
std::function<bool(std::string_view token)> authenticate;
std::function<bool()> authenticated;
```

- On `initialize`, if `authenticate` is set, the server extracts `params._meta["albor/token"]` and calls it. A missing, malformed, or wrong token returns JSON-RPC `-32000` with a generic authentication message.
- On every other request, if `authenticated` is set and reports false, the server returns the same generic error. Notifications still take no reply and reach nothing.
- Tests that build an `McpServer` without callbacks keep the current unauthenticated behavior.
- The server never compares or logs the expected token itself; the session owns that.

### 8.4 MCP command parameters

The tool schema currently names parameters only for the semantic pack. With the full surface, add:

| command | parameter names |
| --- | --- |
| `command.list` | none |
| `command.get` | `id` |
| `command.invoke` | `id`, `argument` (optional) |
| `command.can_undo` | none |
| `command.undo` | none |
| `command.redo` | none |

Without this, an MCP client's named `command.invoke` call arrives with no `id` and is refused.

### 8.5 Transport hardening

MCP payloads can be large enough that one non-blocking write is partial, and a peer that disconnects mid-write can raise `SIGPIPE`. Harden the Unix transport while adding the seam:

- queue replies in a per-client output buffer and flush until `EAGAIN`, retaining the remainder for the next `serve_one()`;
- process multiple complete input lines per read;
- set `SO_NOSIGPIPE` on macOS accepted sockets; use `send(..., MSG_NOSIGNAL)` where that is the platform spelling;
- close the client on a write error and clear the buffer;
- keep the existing same-user credential check.

The transport must still return `false` only for listener-level failure; a dead client is not a server failure.

## 9. Default Surface and Policy

The host explicitly allows exactly these registered commands:

- semantic: `semantic.tree`, `semantic.find`, `semantic.invoke`, `semantic.actions`, `semantic.set_focused`;
- command: `command.list`, `command.get`, `command.invoke`, `command.can_undo`, `command.undo`, `command.redo`.

`AutomationRegistry` remains deny-by-default: `allow()` is still the primitive, and the host calls it for each standard name the registry knows. A command that is not registered cannot be listed or called.

The application's real capability list is its `id:` set:

- a node without an explicit id is not a selector target;
- a command without an explicit id is not a selector target;
- a disabled or invisible command remains disabled or invisible to automation, exactly as it is to the user;
- `semantic.tree` reports the live tree, but invocation and selection still require identity.

Consequences accepted by this plan:

- the default surface includes `command.undo` and `command.redo`; the local same-user token is the security boundary, not the command list;
- the token is a local capability check, not an isolation boundary against another process running as the same user; the host is a development/test tool;
- there is no runtime narrow/disable request;
- a stricter host is a manual integration over `albor-automation` and is outside the transparent path. A build-time `deny`/`only` option is a follow-up if a real application needs it.

## 10. File-by-File Work

### 10.1 Core

`core/dev_hook.h`, `core/dev_hook.cpp`
- add the fixed observer set and `add_root_binding`;
- keep `install_root_binder` as replace/reset;
- update comments and tests for multiple observers.

`devrt/devrt.cpp`
- change `install_root_binder(&bind_root, &unbind_root)` to `add_root_binding`.

### 10.2 Automation core

`automation/automation_connection.h`
- new connection interface and factory typedef.

`automation/automation_transport.h`, `automation/automation_transport.cpp`
- add `set_connection_factory`;
- move raw session handling into `SessionConnection`;
- keep `create_transport(registry)` and the default raw behavior.

`automation/unix_socket.macos.mm`
- store the connection factory;
- create a connection per client;
- add output buffering, multi-line reads, and `SIGPIPE` protection;
- keep the same-user check, token publication, listen, serve, and stop behavior;
- implement `set_connection_factory`.

`automation/mcp_connection.h`, `automation/mcp_connection.cpp`
- new MCP connection over `McpServer` and `AutomationSession`;
- authentication callbacks and route wiring.

`automation/mcp_server.h`, `automation/mcp_server.cpp`
- add optional `authenticate` / `authenticated` route callbacks;
- extract `_meta["albor/token"]` on `initialize`;
- enforce authentication on subsequent requests;
- add `command.*` parameter names.

`automation/CMakeLists.txt`
- change the core source glob to non-recursive;
- add `add_subdirectory(host)`.

### 10.3 Automation host

`automation/host/automation_host.h`
- internal declaration of host state and test-visible path accessors, if needed;
- not included by application code.

`automation/host/automation_host.cpp`
- binding stack, start/stop, registry, semantic snapshot, command snapshot, timer, descriptor.

`automation/host/automation_host_auto.cpp`
- self-installing `dev::add_root_binding(&bind_root, &unbind_root)`.

`automation/host/host_paths.h`
`automation/host/host_paths.macos.mm`
`automation/host/host_paths.linux.c++`
`automation/host/host_paths.win32.c++`
- per-process base directory, pid, process name, socket/token/descriptor paths;
- primary `$TMPDIR` / `$XDG_RUNTIME_DIR` layout and the short-path fallback;
- permission application and cleanup helpers.

`automation/host/CMakeLists.txt`
- build `albor-automation-host` from the host sources and platform sources;
- link `albor-automation`, `albor-application`, `albor-commands`, `albor-semantics`, `albor-ui`, `albor-core`;
- define `albor-automation-auto` as the whole-archive interface target.

### 10.4 Dev umbrella and root build

`devrt/CMakeLists.txt`
- link `albor-automation-auto` into `albor-devrt-auto` when `ALBOR_AUTOMATION`.

`CMakeLists.txt`
- update option descriptions for `ALBOR_TEST` and `ALBOR_AUTOMATION`;
- keep the Release fatal check;
- no subdirectory reorder expected.

### 10.5 Examples

`examples/greeting/`, `examples/form/`
- remove the manual automation registry, transport, endpoint, token, allow-list, and `serve_one()` code;
- keep the accessibility bridge and semantic-tree publish path;
- rename the remaining class from `Drivable` to `AccessibilityBridge` so it no longer claims automation;
- delete `drivability_stub.cpp` and the `ALBOR_AUTOMATION` source conditional;
- link `albor-devrt-auto` when `ALBOR_DEV`, or `albor-automation-auto` when only `ALBOR_AUTOMATION` is on;
- update the form's frame hook to refresh accessibility through the window-scoped hook rather than a global frame handler;
- remove the printed manual endpoint; the host prints its own.

This makes the examples reference consumers of the transparent host and proves that no application automation source is needed.

## 11. Tests

### 11.1 Unit tests

`tests/dev_hook.test.cpp`
- two observers both receive bind and unbind;
- `install_root_binder` still replaces/reset;
- `binder_present` reflects any observer;
- capacity failure is reported.

`tests/conditional/mcp_server.test.cpp`
- `initialize` with a valid token succeeds when the auth callbacks are set;
- missing and wrong tokens fail with `-32000`;
- `tools/list` and `tools/call` are refused before authentication;
- `command.invoke` with a named `id` reaches the layer with the expected positional wire line;
- `command.get` parameter placement;
- no-callback routes keep the current behavior.

`tests/conditional/mcp_connection.test.cpp` (new)
- initialize with the correct token, then tools/list and tools/call;
- wrong token, missing `_meta`, and pre-initialize tool calls;
- notification produces no reply;
- refused command returns the automation layer's refusal.

`tests/conditional/automation_transport.test.cpp`
- existing raw-protocol tests still pass through the default connection;
- a transport configured with `McpConnection` accepts initialize, tools/list, and tools/call over the real socket;
- a large reply is delivered completely across multiple pumps;
- a client disconnect does not terminate the server.

### 11.2 Host integration test

Add a separate binary, not a suite in `albor-tests`, because the main test binary links plain `albor-automation` and must not auto-start a host for every window:

```text
tests/conditional/automation_host.test.cpp -> albor-automation-host-tests
```

Build it only where the automation transport exists (currently Apple), with:

- `albor-automation-auto`;
- `albor-platform-headless-auto`;
- `albor-render-software-auto`;
- `albor-text-harfbuzz-auto`;
- `GTest::gtest_main`.

The test:

1. uses `Application::instance()` and a headless window with a root `UiNode` carrying `id: root`;
2. declares a command with an explicit id and an action that mutates test state;
3. creates the window, which binds the tree and starts the host;
4. reads `$TMPDIR/albor-mcp/<pid>/host.json` and the token;
5. connects to the socket and sends MCP `initialize` with `_meta["albor/token"]`;
6. sends `notifications/initialized` and asserts no reply is required;
7. calls `tools/list` and asserts all eleven standard tools are present;
8. calls `semantic.tree` and asserts the `root` id is reported;
9. calls `command.invoke` by named `id` and asserts the action ran on the UI thread;
10. calls `tools/list` before authentication in a second connection, after closing the first, and asserts refusal;
11. closes the window, pumps, and asserts the endpoint directory is gone and the socket is refused;
12. creates a second window and asserts a new host starts with a new token;
13. invokes a command that closes the active window and asserts the application survives and the host stops cleanly.

The test pumps `Application::pump()` while a client thread performs the socket conversation, matching the existing transport tests' pattern.

### 11.3 Test rules

- Tests are self-contained; each test arranges its own application/window/socket state.
- No `#ifdef` changes the runtime class layout.
- The host auto-install is linked only in the dedicated test binary; `albor-tests` keeps plain automation.
- Do not add a test helper shared across suites; the only shared object is the test double.
- Do not execute the suites during implementation unless explicitly requested; build them and run `bash build/check-format.sh`.

## 12. Documentation Updates

### 12.1 `albor-design-v4.md`

Revision Log
- add a 4.4 entry for the transparent automation host: link-time selection, default surface, MCP authentication, discovery contract, and the corrected section references.

§2, boundary 9
- state that automation is local and authenticated, that the host explicitly allow-lists the standard protocol commands, and that application actions remain gated by explicit `id:` and command enablement;
- remove the implication that every application must hand-build its own command allow-list.

§3.2
- change the `automation/` layout comment from "Automation core, socket transport, test runner" to "Automation core, local transport, embedded MCP host (Dev/Test only)".

§10.3
- document `ALBOR_AUTOMATION` as independent of Dev/Test and forbidden in Release;
- document `albor-automation-auto` and the `albor-devrt-auto` inclusion;
- document the zero-code host lifecycle on the bind seam;
- document the default full standard surface and the explicit-`id:` action boundary;
- document the MCP initialize token in `_meta`, the `host.json` contract, and the no-runtime-off rule;
- state that `inspector/` is not the MCP adapter.

§12.1
- replace "four free functions" with the multi-observer bind seam;
- state that `devrt` and the automation host register independently, so a coverage build runs both;
- clarify that `devrt` is Dev-only while automation is independent and Test-first.

§13
- update AD-10's settled wording to "excluded at link time; local authenticated transport; standard command surface explicitly allow-listed by the embedded host; application actions bounded by explicit id and enablement";
- leave AD-20's routing decision intact.

§14, Phase 5
- add the transparent host and MCP-over-local-transport to the automation deliverable.

§16
- change the automation compatibility row's requirement to the automation host, a sample application, and an MCP client;
- state that the reference session is Appendix B.

§18.2 and §18.7
- replace every missing `§19.6` reference with `§2.3` / AD-04;
- replace every missing `§19.7` reference with `§10.3` / AD-20;
- revise the AD-20 bullet so the MCP tool list is derived from the standard allow-list and application identity, not "gains nothing by default".

Appendix B (new)
- add the reference automation session used by issue #8: build a sample application with Dev + automation, launch it, discover `host.json`, read the token, initialize MCP, list tools, query the semantic tree, invoke an action, assert the observable result, close the window, and assert endpoint cleanup.
- AGENTS.md and issue #8 already refer to Appendix B; adding it removes the dangling reference.

### 12.2 `AGENTS.md`

- remove the `§19.6` and `§19.7` references in the automation and MCP boundaries and point to §2.3 / AD-04 and §10.3 / AD-20;
- update the automation boundary bullet: link-selected host, no `Application` API or runtime switch, default standard surface, explicit `id:` bounds actions;
- update the build description: `ALBOR_AUTOMATION` is independent of `ALBOR_DEV` / `ALBOR_TEST`;
- update the repository-layout note: `devrt/` is Dev-only; `automation/` runs in Dev or Test; `inspector/` is not MCP;
- update the automation compatibility test reference from "Appendix B" to the new Appendix B / §16;
- add the host integration binary to the testing notes.

### 12.3 `docs/tooling.md`

- change the hot-reload paragraph: `devrt` is Dev-only, not Dev and Test; Test automation does not require `devrt`;
- replace "`inspector/` is the MCP server" with "`automation/` contains the embedded MCP host";
- document the transparent host, default surface, explicit-id boundary, discovery paths, token metadata, and no-runtime-off rule;
- add the automation compatibility suite to "What the test suite covers".

### 12.4 `docs/status.md`

- change the `devrt/` state from "Dev/Test only" to "Dev only";
- change the automation row's module from `automation/`, `inspector/` to `automation/`;
- update the build-configuration table:
  - Debug: `ALBOR_DEV`, `ALBOR_AUTOMATION` - `devrt` and automation host;
  - Release: none;
  - Test: `ALBOR_TEST`, `ALBOR_AUTOMATION` - automation host and software renderer, no `devrt`.
- mention that `ALBOR_TEST` alone does not create automation unless `ALBOR_AUTOMATION` is on.

### 12.5 `docs/getting-started.md`

- add `albor-automation-auto` to the link table;
- update `albor-devrt-auto` to "dev runtime and, with `ALBOR_AUTOMATION`, the embedded MCP host";
- add a short consumer example that sets the three options before `FetchContent_MakeAvailable` and links the umbrella;
- state that the application source does not change and no automation header is included.

### 12.6 `README.md`

- update "Linking the dev runtime" so `albor-devrt-auto` is described as hot swap plus the automation host when enabled;
- replace the "four null-checked hook calls" wording with the observer seam;
- update the implemented-features row to name the embedded MCP host.

### 12.7 Examples documentation

- update example comments that mention manual drivability, endpoint prints, and application-served automation;
- say that the examples link the umbrella and write no automation code.

## 13. GitHub Issues

### 13.1 Issue #8 — automation compatibility suite

Add a "Transparent host baseline" section to the issue body:

- the sample application runs the embedded host through `albor-devrt-auto` / `albor-automation-auto`, with no `Drivable` wiring;
- the reference test discovers `host.json`, reads the token, initializes MCP with `_meta["albor/token"]`, and drives the app over the local transport;
- the test asserts the default eleven-tool surface, semantic lookup/invoke, command lookup/invoke, token refusal, and endpoint cleanup;
- keep the existing requirements for layout bounds, input, capture, and a real sample application;
- name `albor-form` as the intended sample once it migrates to the transparent host.

### 13.2 Issue #25 — Python harness

Update the issue from "raw automation protocol client" to "MCP client over the local transport":

- discover `$TMPDIR/albor-mcp/<pid>/host.json`;
- read the token file and inject it into MCP `initialize.params._meta["albor/token"]`;
- speak newline-delimited MCP JSON over the Unix socket;
- assert tools/list, semantic drive, command drive, auth refusal, and shutdown cleanup;
- optionally use the same client as the core of a stdio-to-Unix-socket bridge for general MCP clients.

### 13.3 Issue hygiene

- Do not close #8 or #25 with this change alone; this plan adds the host and its focused integration test, while the full cross-cutting suites remain their scope.
- If the implementation discovers a real need for a narrower surface, open a separate issue for a build-time `deny`/`only` policy rather than adding a runtime switch.

## 14. Implementation Order

1. `bash build/snapshot.sh pre-automation-host` (AGENTS.md recovery point).
2. Core bind seam: `core/dev_hook.*`, `devrt/devrt.cpp`, `dev_hook` tests.
3. Transport seam and hardening: connection interface, default raw connection, Unix socket refactor, large-write/SIGPIPE tests.
4. MCP authentication and command parameters: `McpRoute`, `McpServer`, `McpConnection`, tests.
5. Host: state, paths, startup/stop, descriptor, auto-install, CMake targets.
6. Umbrella wiring: `albor-automation-auto`, `albor-devrt-auto`, option descriptions.
7. Host integration binary and test.
8. Example migration to the transparent host and accessibility-only bridge.
9. Documentation and issue updates.
10. Build verification and formatting.

## 15. Acceptance Criteria

- A fresh external Dev application sets `ALBOR_DEV=ON`, `ALBOR_AUTOMATION=ON`, links `albor-devrt-auto`, and writes no automation code; its MCP host starts when its first window is created.
- `host.json`, `token`, and `mcp.sock` exist with the stated names, locations, and permissions.
- An MCP client authenticates with the token from `initialize.params._meta["albor/token"]`, lists all eleven standard tools, drives the semantic tree, and invokes an application command by explicit id.
- A wrong or missing token is refused; a tool call before initialization is refused.
- Closing the last tree removes the endpoint directory; opening another tree starts a new host with a new token.
- An application that links `albor-devrt-auto` with `ALBOR_AUTOMATION=OFF` still gets hot reload and no automation symbols.
- A Test application links `albor-automation-auto` and gets the host without `devrt`.
- A Release configure with `ALBOR_AUTOMATION=ON` fails; a Release build has no host, MCP, transport, or automation source in the link.
- `albor-tests` continues to link plain automation and never auto-starts a host.
- `core/dev_hook` supports `devrt` and automation side by side in the coverage configuration.
- No `Application` API, class layout, vtable, or inline method changes between configurations.
- Debug, Test, and Release configure and build without warnings.
- `bash build/check-format.sh` passes on every changed C++ file; every new comment block follows the one-line rule.
- The design document, AGENTS.md, tooling, status, getting-started, README, examples, and issues #8 / #25 are updated as listed.

## 16. Risks and Review Points

- **Singleton limitation.** The transparent host uses `Application::instance()`. Applications that construct a private `Application` must keep the manual integration. This is documented rather than hidden.
- **Default surface includes undo/redo.** The token is the local security boundary, and application actions are still gated by explicit ids. If this is unacceptable for a specific app, a build-level narrowing policy is the follow-up.
- **Transport availability.** Windows has no named-pipe transport yet, so the host fails soft and serves nothing there. macOS is the reference implementation; Linux remains planned.
- **Large MCP payloads.** The current transport writes once and can truncate; the output buffer is a required part of this change, not an optional cleanup.
- **MCP protocol version.** Keeping `2024-11-05` avoids scope creep, but the reference client must accept it. A later version bump is separate.
- **CMake forward references.** The host target links `albor-application` before it is added. The existing tree already does this for other modules; if a configure check rejects it, the fallback is to move `add_subdirectory(application)` earlier or create the host target after application, not to add an application dependency back into automation.
- **Examples lose manual automation.** The accessibility bridge remains; the manual Drivable path disappears from the examples. The manual path is still covered by automation tests and documented for strict hosts.
- **Discovery cleanup after a crash.** `host.json` can remain stale. Clients must check the pid; the host cleans its own directory on normal stop.
- **No per-app narrowing in v1.** Confirm this is acceptable before implementation. The recommended default is the full standard surface with explicit-id action gating.

## 17. Approval Gate

Implementation does not start until the user reviews this document and approves:

1. the target ownership and build graph (`albor-automation-auto`, `albor-devrt-auto`);
2. the full default surface with no runtime off switch or per-app narrowing in v1;
3. the host lifecycle and fallback-to-next-root behavior;
4. the MCP authentication and discovery contract;
5. the example migration and documentation/issue scope.
