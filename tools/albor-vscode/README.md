# Albor for VS Code

A minimal VS Code client for `albor-lsp`, the Albor language server. It opens `.ui` files as
the `albor` language and launches the server over stdio with full document sync, so compiler
diagnostics, completion and hover appear in the editor.

## Requirements

Build `albor-lsp` from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug --target albor-lsp
```

The binary lands at `.build-debug/tools/albor-lsp/albor-lsp` on macOS and Linux, and at
`.build-debug/tools/albor-lsp/albor-lsp.exe` on Windows.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `albor.lsp.executable` | `albor-lsp` | Path to the server. A bare name is resolved through `PATH`; an absolute path is the reliable choice when VS Code is launched outside a shell. |
| `albor.manifests` | `[]` | `.ui-manifest` files whose native components and records the editor should resolve. Relative paths resolve against the first workspace folder. |

Changing either setting restarts the server.

The manifests are sent at `initialize` as `initializationOptions.manifests`. The language
server resolves them with the same manifest front end the build uses, so their records, native
components, properties and signals complete and hover in the editor. The extension also watches
`**/*.ui-manifest` and forwards a saved edit as `workspace/didChangeWatchedFiles`, which
rebuilds the catalog and recompiles the open roots without a restart.

## Run it from this source tree

```sh
cd tools/albor-vscode
npm install
npm run compile
```

Open `tools/albor-vscode` in VS Code and press **F5** (Run Extension). In the extension host,
open any `.ui` file. If `albor-lsp` is not on `PATH`, set `albor.lsp.executable` to the
absolute path of the binary built above.

## Test

The smoke test launches the real `albor-lsp` over stdio and asserts the same requests the
client sends: initialize, diagnostics, an edit that clears them, completion, hover and
shutdown.

```sh
cd tools/albor-vscode
ALBOR_LSP=/absolute/path/to/albor-lsp npm test
```

`npm test` compiles the TypeScript first, checks manifest path resolution, and runs the
smoke test, so a type error or a broken path rule fails the run. CI runs it against the
debug binary on macOS and Windows.

## Package

```sh
cd tools/albor-vscode
npm install
npx @vscode/vsce package
```

The result is `albor-vscode-0.1.0.vsix`; install it with **Extensions: Install from VSIX...**.
The extension is not published to the Marketplace.

## Layout

- `src/extension.ts` — the client lifecycle and the settings it reads.
- `src/manifests.ts` — resolves configured manifest paths against the workspace.
- `test/manifests.cjs` — the manifest path-resolution test.
- `test/smoke.cjs` — the protocol smoke test, with no npm dependencies.
- `language-configuration.json` — comments, brackets and auto-closing pairs.
