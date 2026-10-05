#!/bin/bash

# restore the working tree from a snapshot taken by build/snapshot.sh
#
# usage:
#   bash build/restore.sh <label>    restore the newest snapshot matching <label>
#   bash build/restore.sh LATEST     restore whatever was snapshotted most recently
#
# the restore unpacks the archive over the working tree, so a file that was
# deleted comes back and a file that was damaged is overwritten, while the git
# history is left untouched and therefore needs no undo of its own

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# the recovery points live outside the repository, so that nothing the restore
# touches can also destroy them
SNAPSHOT_DIR="${ALBOR_SNAPSHOT_DIR:-$HOME/albor-snapshots}"

label="${1:-}"

if [ -z "$label" ]; then
    echo "usage:"
    echo "  bash build/restore.sh <label>    restore the newest snapshot matching <label>"
    echo "  bash build/restore.sh LATEST     restore the most recent snapshot"
    echo
    echo "snapshots live in $SNAPSHOT_DIR"
    echo "list them with: bash build/snapshot.sh --list"
    exit 1
fi

# LATEST resolves to whatever was taken most recently, and any other label
# resolves to the newest directory in the snapshot store that carries it
if [ "$label" = "LATEST" ] || [ "$label" = "latest" ]; then
    if [ ! -f "$SNAPSHOT_DIR/LATEST" ]; then
        echo "error: no snapshot has been taken yet" >&2
        exit 1
    fi
    target="$(cat "$SNAPSHOT_DIR/LATEST")"
else
    # the newest match wins, because several snapshots may share a label
    target="$(ls -1dt "$SNAPSHOT_DIR"/*"$label" 2>/dev/null | head -1)"

    if [ -z "$target" ]; then
        echo "error: no snapshot matches '$label'" >&2
        echo "  available:" >&2
        ls -1dt "$SNAPSHOT_DIR"/*/ 2>/dev/null | sed 's/^/    /' >&2
        exit 1
    fi
fi

if [ ! -f "$target/tree.tgz" ]; then
    echo "error: $target holds no archive, it is not a snapshot" >&2
    exit 1
fi

# the archive is verified before anything is written, so a truncated or corrupt
# snapshot fails while the working tree is still intact
if ! tar -tzf "$target/tree.tgz" > /dev/null 2>&1; then
    echo "error: the archive of $target is corrupt, nothing was restored" >&2
    exit 1
fi

echo "restoring: $target"

# the history at snapshot time is printed, so a restore can be compared against
# the state the snapshot was taken from
if [ -f "$target/history.txt" ]; then
    echo "--- history at snapshot time:"
    sed 's/^/    /' "$target/history.txt"
fi

# the archive is unpacked over the working tree, which is what repairs both a
# deleted and a damaged file, and does not need the repository to be clean first
if ! (cd "$ROOT" && tar -xzf "$target/tree.tgz"); then
    echo "error: the archive could not be unpacked, review the tree by hand" >&2
    exit 1
fi

echo "restored: $target"
echo "--- review with: git status --short && git diff"