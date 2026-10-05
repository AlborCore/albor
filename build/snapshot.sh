#!/bin/bash

# snapshot the working tree to a directory outside the repository, so that an
# operation that can fail has a state to recover from
#
# usage:
#   bash build/snapshot.sh <label>   # snapshot the tree under that label
#   bash build/restore.sh <label>    # restore that snapshot over the tree
#   bash build/snapshot.sh --list    # list the snapshots that exist
#
# why a snapshot instead of a commit: a commit records what the tree should
# look like, while a snapshot records what it does look like right now. A
# destructive operation can also rewrite git state or destroy untracked files,
# so the recovery point must not live inside the thing being protected.
#
# snapshots are written to $ALBOR_SNAPSHOT_DIR, which defaults to
# ~/albor-snapshots, so that a git reset or a rm -rf cannot reach them

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# the recovery point must not live inside the repository it protects
SNAPSHOT_DIR="${ALBOR_SNAPSHOT_DIR:-$HOME/albor-snapshots}"

# directories that are build output rather than source, and are reproducible
# from the build system, so they are never worth snapshotting
EXCLUDE_FROM_SNAPSHOT=(--exclude-vcs --exclude='.build-*' --exclude='.cache' --exclude='.DS_Store' --exclude='compile_commands.json')

snapshot_create() {
    local label="$1"
    local stamp
    stamp="$(date +%Y%m%d-%H%M%S)"
    local target="$SNAPSHOT_DIR/${stamp}-${label}"

    mkdir -p "$target" || return 1

    # the tree is copied out of the repository by reading it through git, so
    # that the snapshot holds the same file set the repository tracks plus the
    # new files, and never the ignored build output; --deleted is passed so
    # that a path recorded in the index but already removed from disk is
    # skipped rather than failing the whole archive, because a half written
    # snapshot is worse than no snapshot
    if ! (cd "$ROOT" && git ls-files --cached --others --exclude-standard --deleted | while IFS= read -r path; do
            [ -f "$path" ] && printf '%s\n' "$path"
        done | tar -czf "$target/tree.tgz" "${EXCLUDE_FROM_SNAPSHOT[@]}" --files-from -); then
        echo "error: the snapshot archive could not be written" >&2
        rm -rf "$target"
        return 1
    fi

    # the recorded head identifies what the snapshot was taken from, so a
    # restored tree can be compared against the history it came from
    git -C "$ROOT" rev-parse HEAD > "$target/head.txt" 2>/dev/null || true
    git -C "$ROOT" log --oneline -5 > "$target/history.txt" 2>/dev/null || true

    # a manifest of the recovered paths makes it possible to see what a snapshot
    # covers without unpacking it, and to detect a truncated archive
    tar -tzf "$target/tree.tgz" > "$target/files.txt" || return 1

    echo "$target" > "$SNAPSHOT_DIR/LATEST"
    echo "snapshot: $target"
    echo "          $(wc -l < "$target/files.txt" | tr -d ' ') files, head $(cat "$target/head.txt" 2>/dev/null || echo unknown)"
}

snapshot_restore() {
    local label="$1"

    # an exact label resolves to one snapshot, a prefix to the newest match, and
    # the special name LATEST to whatever was taken most recently
    local target
    if [ "$label" = "LATEST" ] || [ "$label" = "latest" ]; then
        if [ ! -f "$SNAPSHOT_DIR/LATEST" ]; then
            echo "error: no snapshot has been taken yet" >&2
            return 1
        fi
        target="$(cat "$SNAPSHOT_DIR/LATEST")"
    else
        # the newest match for the prefix, because several labels may share a name
        target="$(ls -1dt "$SNAPSHOT_DIR"/*"$label" 2>/dev/null | head -1)"
        if [ -z "$target" ]; then
            echo "error: no snapshot matches '$label'" >&2
            echo "  available:" >&2
            ls -1d "$SNAPSHOT_DIR"/*/ 2>/dev/null | sed 's/^/    /' >&2
            return 1
        fi
    fi

    if [ ! -f "$target/tree.tgz" ]; then
        echo "error: $target has no archive, it is not a snapshot" >&2
        return 1
    fi

    echo "restoring: $target"
    if [ -f "$target/history.txt" ]; then
        echo "--- history at snapshot time:"
        sed 's/^/    /' "$target/history.txt"
    fi

    # the archive is unpacked over the working tree, which restores a file that
    # was deleted and overwrites a file that was damaged, without touching the
    # history, so the recovery itself needs no undo
    if ! (cd "$ROOT" && tar -xzf "$target/tree.tgz"); then
        echo "error: the archive could not be unpacked, the tree was left as it was" >&2
        return 1
    fi

    echo "restored: $target"
    echo "--- review with: git status --short && git diff"
}

snapshot_list() {
    if [ ! -d "$SNAPSHOT_DIR" ]; then
        echo "no snapshots, $SNAPSHOT_DIR does not exist"
        return 0
    fi

    local found=0
    for snapshot in $(ls -1dt "$SNAPSHOT_DIR"/*/ 2>/dev/null); do
        found=1
        local stamp head count size
        stamp="$(basename "$snapshot")"

        # a snapshot made outside this script may lack the metadata files, so
        # each of them is read only when it is actually there
        head="unknown"
        [ -f "$snapshot/head.txt" ] && head="$(cut -c1-8 < "$snapshot/head.txt")"

        count="?"
        [ -f "$snapshot/files.txt" ] && count="$(wc -l < "$snapshot/files.txt" | tr -d ' ')"

        size="?"
        [ -f "$snapshot/tree.tgz" ] && size="$(du -sh "$snapshot/tree.tgz" | cut -f1)"

        echo "$stamp  head=$head  files=$count  size=$size"
    done

    if [ "$found" = "0" ]; then
        echo "no snapshots in $SNAPSHOT_DIR"
    fi
}

mkdir -p "$SNAPSHOT_DIR" || {
    echo "error: $SNAPSHOT_DIR could not be created" >&2
    exit 1
}

case "${1:-}" in
    --list | -l | list)
        snapshot_list
        ;;
    "" | --help | -h)
        echo "usage:"
        echo "  bash build/snapshot.sh <label>    snapshot the tree as <label>"
        echo "  bash build/snapshot.sh --list     list the snapshots that exist"
        echo "  bash build/restore.sh <label>     restore a snapshot, or LATEST"
        echo
        echo "snapshots live in $SNAPSHOT_DIR"
        echo "override with ALBOR_SNAPSHOT_DIR=<path>"
        ;;
    *)
        snapshot_create "$*"
        ;;
esac