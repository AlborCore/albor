import * as path from "node:path";

// resolve every configured manifest against the workspace, because the server reads absolute paths (§11.2, §12.2)
export function manifest_paths(configured: readonly string[], workspace: string | undefined): string[] {
    // an empty entry is a setting the user left blank, which names no manifest (§11.2)
    return configured.filter((entry) => entry.length > 0).map((entry) => {
        // an absolute path is used as written, because the user named the file directly (§11.2)
        if (path.isAbsolute(entry) || workspace === undefined)
            return entry;
        // a relative path resolves beside the workspace folder, which is where the build reads it (§11.2)
        return path.join(workspace, entry);
    });
}
