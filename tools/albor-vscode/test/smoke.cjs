"use strict";

const assert = require("node:assert/strict");
const { spawn } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

// the executable is named by the environment, because the test drives the binary the build just produced (§12.2)
const EXECUTABLE = process.env.ALBOR_LSP || "albor-lsp";

// the longest any single exchange may take, which bounds a missing reply or notification (§12.2)
const EXCHANGE_TIMEOUT = 10000;

// one json-rpc connection over the server's stdio, which is the transport the extension uses (§12.2)
class Connection
{
    constructor(child) {
        this.child = child;
        this.buffer = Buffer.alloc(0);
        this.next_id = 1;
        this.pending = new Map();
        this.notifications = [];
        this.waiters = [];
        this.stderr = "";
        this.failure = undefined;
        child.stdout.on("data", (chunk) => this.read(chunk));
        child.stderr.on("data", (chunk) => {
            this.stderr += chunk.toString();
        });
        child.on("error", (error) => this.fail(error));
        child.on("exit", (code, signal) => {
            // a server that leaves with an exchange unanswered is a failure the test should name (§12.2)
            if (this.pending.size > 0 || this.waiters.length > 0)
                this.fail(new Error(`albor-lsp exited with code ${code} and signal ${signal}${this.stderr.length > 0 ? `: ${this.stderr.trim()}` : ""}`));
        });
    }

    fail(error) {
        this.failure = error;
        for (const entry of this.pending.values()) {
            clearTimeout(entry.timer);
            entry.reject(error);
        }
        this.pending.clear();
        for (const waiter of this.waiters) {
            clearTimeout(waiter.timer);
            waiter.reject(error);
        }
        this.waiters = [];
    }

    send(message) {
        if (this.failure !== undefined)
            throw this.failure;
        const body = Buffer.from(JSON.stringify(message), "utf8");
        this.child.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
        this.child.stdin.write(body);
    }

    request(method, params) {
        const id = this.next_id++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`timed out waiting for ${method}`));
            }, EXCHANGE_TIMEOUT);
            this.pending.set(id, { method, resolve, reject, timer });
            try {
                this.send({ jsonrpc: "2.0", id, method, params });
            } catch (error) {
                clearTimeout(timer);
                this.pending.delete(id);
                reject(error);
            }
        });
    }

    notify(method, params) {
        this.send({ jsonrpc: "2.0", method, params });
    }

    read(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        for (;;) {
            const header_end = this.buffer.indexOf("\r\n\r\n");
            if (header_end < 0)
                return;
            const header = this.buffer.subarray(0, header_end).toString("ascii");
            const match = /content-length:\s*(\d+)/i.exec(header);
            if (match === null) {
                this.fail(new Error("albor-lsp sent a frame without a content-length header"));
                return;
            }
            const length = Number.parseInt(match[1], 10);
            const body_start = header_end + 4;
            if (this.buffer.length < body_start + length)
                return;
            const body = this.buffer.subarray(body_start, body_start + length).toString("utf8");
            this.buffer = this.buffer.subarray(body_start + length);
            let message;
            try {
                message = JSON.parse(body);
            } catch (error) {
                this.fail(new Error(`albor-lsp sent invalid json: ${body}`));
                return;
            }
            this.dispatch(message);
        }
    }

    dispatch(message) {
        // a reply carries the identifier of the request, which is what a pending entry is keyed by (§12.2)
        if (Object.prototype.hasOwnProperty.call(message, "id") && (Object.prototype.hasOwnProperty.call(message, "result") || Object.prototype.hasOwnProperty.call(message, "error"))) {
            const entry = this.pending.get(message.id);
            if (entry === undefined)
                return;
            this.pending.delete(message.id);
            clearTimeout(entry.timer);
            if (message.error !== undefined)
                entry.reject(new Error(`${entry.method}: ${message.error.message}`));
            else
                entry.resolve(message.result);
            return;
        }
        // a notification is kept for a waiter or for the next wait, because it has no identifier (§12.2)
        if (typeof message.method === "string") {
            const index = this.waiters.findIndex((waiter) => waiter.method === message.method);
            if (index >= 0) {
                const [waiter] = this.waiters.splice(index, 1);
                clearTimeout(waiter.timer);
                waiter.resolve(message);
                return;
            }
            this.notifications.push(message);
        }
    }

    wait_for_notification(method) {
        const index = this.notifications.findIndex((notification) => notification.method === method);
        if (index >= 0)
            return Promise.resolve(this.notifications.splice(index, 1)[0]);
        if (this.failure !== undefined)
            return Promise.reject(this.failure);
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.waiters = this.waiters.filter((waiter) => waiter.timer !== timer);
                reject(new Error(`timed out waiting for ${method}`));
            }, EXCHANGE_TIMEOUT);
            this.waiters.push({ method, resolve, reject, timer });
        });
    }

    wait_for_exit() {
        // an exit that already happened is reported rather than waited for, because the event is gone (§12.2)
        if (this.child.exitCode !== null)
            return Promise.resolve(this.child.exitCode);
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error("timed out waiting for albor-lsp to exit")), EXCHANGE_TIMEOUT);
            this.child.once("exit", (code) => {
                clearTimeout(timer);
                resolve(code);
            });
            this.child.once("error", (error) => {
                clearTimeout(timer);
                reject(error);
            });
        });
    }

    dispose() {
        const error = new Error("the smoke test finished");
        for (const entry of this.pending.values()) {
            clearTimeout(entry.timer);
            entry.reject(error);
        }
        this.pending.clear();
        for (const waiter of this.waiters) {
            clearTimeout(waiter.timer);
            waiter.reject(error);
        }
        this.waiters = [];
    }
}

// read the extension manifest, because the declarative contract is what an installed client reads (§12.2)
function manifest() {
    return JSON.parse(fs.readFileSync(path.join(__dirname, "..", "package.json"), "utf8"));
}

async function main() {
    // the language contribution is what makes a ui file reach the client, so it is asserted before the server runs (§12.2)
    const extension = manifest();
    assert.equal(extension.contributes.languages[0].id, "albor");
    assert.ok(extension.contributes.languages[0].extensions.includes(".ui"));
    // both settings are what the issue asks to be configurable, and their names are the contract the client reads (§12.2)
    assert.ok(extension.contributes.configuration.properties["albor.lsp.executable"] !== undefined);
    assert.ok(extension.contributes.configuration.properties["albor.manifests"] !== undefined);
    // the entry point is what the extension host loads, and the compile that ran before this test produced it (§12.2)
    assert.ok(fs.existsSync(path.join(__dirname, "..", extension.main)));
    // the language activation starts the client when a ui file opens, which is what the contribution promises (§12.2)
    assert.ok(extension.activationEvents.includes("onLanguage:albor"));
    // the document is a saved file whose name matches its component, which is the shape the build accepts (§11.1)
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "albor-vscode-smoke-"));
    const source = path.join(directory, "smoke.ui");
    const text = "component Smoke {\n    Column {\n        Label { colour: \"red\"; }\n    }\n}\n";
    fs.writeFileSync(source, text);
    const uri = pathToFileURL(source).href;
    // the language client appends --stdio for TransportKind.stdio, so the smoke test spawns the same command line (§12.2)
    const child = spawn(EXECUTABLE, ["--stdio"], { stdio: ["pipe", "pipe", "pipe"] });
    const connection = new Connection(child);
    try {
        // the handshake names the manifest list the extension passes, and asserts the capabilities it relies on (§12.2)
        const initialized = await connection.request("initialize", { processId: process.pid, rootUri: null, capabilities: {}, initializationOptions: { manifests: [] } });
        assert.equal(initialized.capabilities.textDocumentSync, 1);
        assert.ok(initialized.capabilities.completionProvider !== undefined);
        assert.equal(initialized.capabilities.hoverProvider, true);
        connection.notify("initialized", {});
        // the open notification is what an editor sends, and its publication is what fills the problems panel (§12.2)
        connection.notify("textDocument/didOpen", { textDocument: { uri, languageId: "albor", version: 1, text } });
        const publication = await connection.wait_for_notification("textDocument/publishDiagnostics");
        assert.equal(publication.params.uri, uri);
        assert.ok(publication.params.diagnostics.length >= 1);
        const diagnostic = publication.params.diagnostics.find((entry) => entry.code === "E0411");
        assert.ok(diagnostic !== undefined);
        assert.equal(diagnostic.source, "albor");
        assert.equal(diagnostic.severity, 1);
        // a full text change is what the client sends on every edit, and a fixed property must clear the publication (§12.2)
        const fixed = "component Smoke {\n    Column {\n        Label { text: \"ok\"; }\n    }\n}\n";
        connection.notify("textDocument/didChange", { textDocument: { uri, version: 2 }, contentChanges: [{ text: fixed }] });
        const cleared = await connection.wait_for_notification("textDocument/publishDiagnostics");
        assert.equal(cleared.params.uri, uri);
        assert.equal(cleared.params.diagnostics.length, 0);
        // completion inside the column body offers the builtin elements, which is what the client presents (§12.2)
        const completion = await connection.request("textDocument/completion", { textDocument: { uri }, position: { line: 2, character: 8 } });
        assert.ok(Array.isArray(completion));
        const labels = completion.map((item) => item.label);
        assert.ok(labels.includes("Label"));
        assert.ok(labels.includes("Column"));
        // hover on the element name describes it, which is the second editor feature the issue asks for (§12.2)
        const hover = await connection.request("textDocument/hover", { textDocument: { uri }, position: { line: 2, character: 10 } });
        assert.ok(hover !== null && hover.contents !== undefined);
        assert.ok(hover.contents.value.includes("Label"));
        // shutdown and exit are the protocol's close sequence, which proves the process ends cleanly (§12.2)
        await connection.request("shutdown", null);
        connection.notify("exit", {});
        const code = await connection.wait_for_exit();
        assert.equal(code, 0);
        process.stdout.write(`smoke: initialize, diagnostics, completion, hover and shutdown passed against ${EXECUTABLE}\n`);
    } catch (error) {
        // the server's stderr is the reason a launch or a frame failed, so it is printed before the test gives up (§12.2)
        if (connection.stderr.length > 0)
            process.stderr.write(connection.stderr);
        throw error;
    } finally {
        connection.dispose();
        if (child.exitCode === null && child.signalCode === null)
            child.kill();
        fs.rmSync(directory, { recursive: true, force: true });
    }
}

main().catch((error) => {
    process.stderr.write(`smoke: ${error.message}\n`);
    process.exit(1);
});
