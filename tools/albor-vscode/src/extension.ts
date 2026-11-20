import * as vscode from "vscode";
import { LanguageClient, TransportKind, type LanguageClientOptions, type ServerOptions } from "vscode-languageclient/node";

import { manifest_paths } from "./manifests";

// the executable a bare setting names, which is what a developer installs on PATH (§12.2)
const DEFAULT_EXECUTABLE = "albor-lsp";

// the language id the server tests already use, which is what the document selector matches (§12.2)
const LANGUAGE_ID = "albor";

// the longest a handshake may take, which bounds a server that spawns but never answers initialize (§12.2)
const START_TIMEOUT = 15000;

// the client the extension host owns, which is stopped when the extension deactivates (§12.2)
let client: LanguageClient | undefined;

// the watcher that reports a manifest edit, which the server reloads its catalog from (§11.2, §12.2)
let manifest_watcher: vscode.FileSystemWatcher | undefined;

// the restarts are serialized, because two servers over one workspace would publish twice (§12.2)
let restarts: Promise<void> = Promise.resolve();

// whether the extension is shutting down, which stops a queued restart from spawning a server after teardown (§12.2)
let stopping = false;

// build the stdio server description, because the language client launches the binary as a child process (§12.2)
function server_options(executable: string): ServerOptions {
    return { command: executable, args: [], transport: TransportKind.stdio };
}

// start one client over the current settings, which is what activation and a settings change both need (§12.2)
async function start_client(): Promise<void> {
    // a start after deactivation would spawn a server no later code stops, so it is refused (§12.2)
    if (stopping)
        return;
    // the settings are read at start, because the executable and the manifests are fixed for the session (§12.2)
    const configuration = vscode.workspace.getConfiguration("albor");
    // the executable names the binary, and a bare name is resolved through PATH by the spawn (§12.2)
    const executable = configuration.get<string>("lsp.executable", DEFAULT_EXECUTABLE);
    // a relative manifest resolves against the first folder, because initialize is workspace scoped (§12.2)
    const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    // the manifests are the catalog paths initialize carries, which resolve into elements and records (§11.2, §12.2)
    const manifests = manifest_paths(configuration.get<string[]>("manifests", []), workspace);
    // the selector names a saved or an untitled buffer, because the server accepts both (§12.2)
    const document_selector = [
        { scheme: "file", language: LANGUAGE_ID },
        { scheme: "untitled", language: LANGUAGE_ID }
    ];
    // the initialize options are how a client supplies the manifest catalog, which the server resolves (§11.2, AD-17)
    const initialization_options = { manifests };
    // a saved manifest edit reaches the server as a watched file change, which is how its catalog reloads (§11.2, §12.2)
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.ui-manifest");
    // a previous watcher is dropped, because a restart must report to the client this attempt starts (§12.2)
    manifest_watcher?.dispose();
    // the watcher is kept, so stopping the client disposes the one that reports to it (§12.2)
    manifest_watcher = watcher;
    // the client owns diagnostics, completion and hover, and forwards manifest edits to the server (§12.2)
    const client_options: LanguageClientOptions = { documentSelector: document_selector, initializationOptions: initialization_options, synchronize: { fileEvents: watcher } };
    // the starting client is kept locally, because a later restart must not be the one a stale timeout kills (§12.2)
    const starting = new LanguageClient(LANGUAGE_ID, "Albor", server_options(executable), client_options);
    // the client is stored before it starts, because deactivate has to stop it even while it connects (§12.2)
    client = starting;
    // the timer bounds the handshake, because a broken executable may read stdin forever (§12.2)
    let timer: ReturnType<typeof setTimeout> | undefined;
    const timeout = new Promise<never>((_, reject) => {
        timer = setTimeout(() => {
            // a process that never answers is killed, because the next attempt must own the channel alone (§12.2)
            starting.serverProcess?.kill();
            reject(new Error(`the server did not answer initialize within ${START_TIMEOUT / 1000} seconds`));
        }, START_TIMEOUT);
    });
    try {
        // the handshake races the timeout, because the start resolves only after the initialize reply (§12.2)
        await Promise.race([starting.start(), timeout]);
    } catch (error) {
        // a rejected handshake leaves the spawned process alive, so it is killed before the client is forgotten (§12.2)
        starting.serverProcess?.kill();
        // the watcher belongs to the failed attempt, so it is disposed before a later start creates its own (§12.2)
        watcher.dispose();
        // the module reference is cleared only when this attempt still owns it, because a restart may have moved on (§12.2)
        if (manifest_watcher === watcher)
            manifest_watcher = undefined;
        // the current client is cleared only when this attempt still owns it, because a restart may have moved on (§12.2)
        if (client === starting)
            client = undefined;
        // the failure reaches activate, which reports it against the setting the user can fix (§12.2)
        throw error;
    } finally {
        // the timer is cleared once the handshake settled, because it must not fire after the client is running (§12.2)
        if (timer !== undefined)
            clearTimeout(timer);
    }
}

// report a failed start, because a wrong executable path is a setting the user can correct (§12.2)
function report_failure(error: unknown): void {
    // the message names the setting, because that is what the user has to change (§12.2)
    void vscode.window.showErrorMessage(`Albor: cannot start albor-lsp. Check "albor.lsp.executable". ${String(error)}`);
}

// stop the running client and forget it, which is what a restart and deactivation both begin with (§12.2)
async function stop_client(): Promise<void> {
    // the watcher is disposed first, because it reports to the client that is about to stop (§12.2)
    manifest_watcher?.dispose();
    // the next start creates the watcher its own client needs (§12.2)
    manifest_watcher = undefined;
    // the reference is captured first, because a start may replace it while the stop is still running (§12.2)
    const running = client;
    // the reference is cleared before the stop, because the next start owns a fresh client (§12.2)
    client = undefined;
    // a client that never started has nothing to stop (§12.2)
    if (running === undefined)
        return;
    try {
        // the protocol shutdown closes the channel, which is what a restart has to do first (§12.2)
        await running.stop();
    } catch (error) {
        // a client that never reached the running state refuses to stop, so its process is terminated directly (§12.2)
        running.serverProcess?.kill();
        // the failure is reported, because a hidden one would leave the settings change unexplained (§12.2)
        report_failure(error);
    }
}

// start a client after stopping the old one, because the executable and manifests are read at start (§12.2)
async function restart_client(): Promise<void> {
    // the old channel is closed first, because two servers over one workspace would publish twice (§12.2)
    await stop_client();
    // a restart that was queued before deactivation stops here, because no later code would stop its server (§12.2)
    if (stopping)
        return;
    try {
        // the new settings are applied by a fresh handshake, which is the only way they take effect (§12.2)
        await start_client();
    } catch (error) {
        // a failed restart is reported rather than thrown, because the extension host stays alive with no server (§12.2)
        report_failure(error);
    }
}

// queue one restart, because a second settings change must not overlap the first one (§12.2)
function schedule_restart(): void {
    // a restart requested during deactivation is dropped, because its server would outlive the host (§12.2)
    if (stopping)
        return;
    // the restart is appended to the previous one, so a stop and a start never interleave (§12.2)
    restarts = restarts.then(() => restart_client());
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    // a reactivated extension starts a fresh lifecycle, because deactivate may have run before it (§12.2)
    stopping = false;
    try {
        // the first start is attempted, because opening a ui file should not need a reload after the build (§12.2)
        await start_client();
    } catch (error) {
        // a missing binary is reported in the editor, because the setting is how the user fixes it (§12.2)
        report_failure(error);
    }
    // a change to either setting restarts the server, because both are read once at start (§12.2)
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((event) => {
        // an unrelated setting change leaves the running server alone, because it reads nothing else (§12.2)
        if (!event.affectsConfiguration("albor.lsp.executable") && !event.affectsConfiguration("albor.manifests"))
            return;
        // the restart applies the new executable or manifests to the open workspace (§12.2)
        schedule_restart();
    }));
}

export async function deactivate(): Promise<void> {
    // the flag is set first, because a queued restart must not spawn a server after teardown (§12.2)
    stopping = true;
    // the channel is closed with the extension, because a child process would outlive the host (§12.2)
    await stop_client();
}
