"use strict";

const assert = require("node:assert/strict");
const path = require("node:path");

const { manifest_paths } = require("../out/manifests.js");

// the workspace path a relative setting resolves against, which is what the first folder supplies (§12.2)
const workspace = path.join(path.sep, "work", "app");

// a blank entry is dropped, because a setting the user cleared names no file (§11.2)
assert.deepEqual(manifest_paths(["", "chart.ui-manifest"], workspace), [path.join(workspace, "chart.ui-manifest")]);

// a relative entry joins the workspace, which is where the build reads it (§11.2)
assert.deepEqual(manifest_paths(["assets/chart.ui-manifest"], workspace), [path.join(workspace, "assets", "chart.ui-manifest")]);

// an absolute entry is passed through, because the user named the file directly (§11.2)
const absolute = path.join(workspace, "absolute.ui-manifest");
assert.deepEqual(manifest_paths([absolute], workspace), [absolute]);

// a window without a folder keeps a relative entry as written, because there is no folder to join (§12.2)
assert.deepEqual(manifest_paths(["chart.ui-manifest"], undefined), ["chart.ui-manifest"]);

process.stdout.write("manifests: blank, relative, absolute and workspace-less paths passed\n");
