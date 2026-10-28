#!/bin/bash

# build-scope check for the codegen (AGENTS.md, §14.3): a ui body edit must
# rebuild only that component's generated cpp, and an interface change must
# rebuild the generated header and its includers. An imported ui file follows
# the same boundary across the file edge. The product observed is the
# albor-scope-fixture static library under tests/scope.
#
# usage:
#   bash build/check-build-scope.sh
#
# the script edits the scope fixtures, and restores them from backup copies on
# every exit path

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the fixtures the check edits, which are restored from backup copies every time
FIXTURE_UI="tests/scope/scope_widget.ui"
FIXTURE_PANEL="tests/scope/scope_panel.ui"
FIXTURE_TARGET="albor-scope-fixture"

# the backups are bare copies, because a fixture may be untracked and uncommitted
BACKUP="$(mktemp "${TMPDIR:-/tmp}/albor-scope-ui.XXXXXX")"
PANEL_BACKUP="$(mktemp "${TMPDIR:-/tmp}/albor-scope-panel.XXXXXX")"
cp "$FIXTURE_UI" "$BACKUP"
cp "$FIXTURE_PANEL" "$PANEL_BACKUP"

# a configured build tree is required, because the check measures recompiles
if [ ! -f .build-debug/CMakeCache.txt ]; then
    echo "error: no debug build tree, configure it with 'cmake --preset debug'" >&2
    exit 1
fi

# a failed run must not leak the edits, so the trap restores the fixture
restore_fixture() {
    cp "$BACKUP" "$FIXTURE_UI"
    cp "$PANEL_BACKUP" "$FIXTURE_PANEL"
    rm -f "${FIXTURE_UI}.bak" "${FIXTURE_PANEL}.bak"
}
trap restore_fixture EXIT

# edit one fixture in place, because gnu sed and bsd sed disagree on the empty backup suffix
edit_fixture() {
    sed -i.bak "$1" "$2"
    rm -f "${2}.bak"
}

# the object units that a build compiles in one run, which the assertions measure
recompiled() {
    # msvc writes backslashes and .obj, while clang writes slashes and .o
    cmake --build --preset debug -j8 --target "$FIXTURE_TARGET" 2>&1 \
        | sed -e 's/\r$//' -e 's/\x1b\[[0-9;]*m//g' -e 's#\\#/#g' -e 's/\.obj$/.o/' \
        | awk '/Building CXX object/{n=split($NF,a,"/"); print a[n]}' \
        | sort -u
}

# the first build generates the facade and compiles both units, so the state is warm
recompiled > /dev/null

# a body edit must compile only the generated unit, because the header is unchanged
edit_fixture 's/spacing: 21px/spacing: 23px/' "$FIXTURE_UI"
if [ "$(recompiled)" != "scope_widget.cpp.o" ]; then
    echo "error: a ui body edit rebuilt more than the component unit" >&2
    exit 1
fi

# the fixture is restored before the interface edit, which starts from the committed file
restore_fixture

# an interface edit must compile the generated unit and its includer, because the header changed
edit_fixture '/in property<string> user_name: "World";/a\
    in property<string> scope_label: "live";
' "$FIXTURE_UI"
if [ "$(recompiled)" != "$(printf 'scope_fixture.cpp.o\nscope_widget.cpp.o')" ]; then
    echo "error: a ui interface edit rebuilt the wrong set" >&2
    exit 1
fi

# the fixture is restored before the imported body edit, which starts from the committed files
restore_fixture

# the restoration is built first, so its recompiles are not attributed to the imported edit
recompiled > /dev/null

# an imported body edit must compile only the imported unit, because its header is unchanged
edit_fixture 's/spacing: 2px/spacing: 6px/' "$FIXTURE_PANEL"
if [ "$(recompiled)" != "scope_panel.cpp.o" ]; then
    echo "error: an imported ui body edit rebuilt more than the imported unit" >&2
    exit 1
fi

# the fixture is restored before the imported interface edit, because it changes both generated units
restore_fixture

# the restoration is built first, so its recompiles are not attributed to the imported edit
recompiled > /dev/null

# an imported interface edit must compile the imported unit and the importer, because its header changed
edit_fixture '/in property<string> label_text: "Panel";/a\
    in property<string> caption: "Shared";
' "$FIXTURE_PANEL"
if [ "$(recompiled)" != "$(printf 'scope_panel.cpp.o\nscope_widget.cpp.o')" ]; then
    echo "error: an imported ui interface edit rebuilt the wrong set" >&2
    exit 1
fi

# the fixtures are restored, and a final build regenerates the facades from the committed files
restore_fixture
if ! cmake --build --preset debug -j8 --target "$FIXTURE_TARGET" > /dev/null 2>&1; then
    echo "error: the final build after restoring the fixtures failed" >&2
    exit 1
fi
trap - EXIT
rm -f "$BACKUP" "$PANEL_BACKUP"

echo "build-scope: ok (body: scope_widget.cpp.o; interface: scope_fixture.cpp.o + scope_widget.cpp.o; import body: scope_panel.cpp.o; import interface: scope_panel.cpp.o + scope_widget.cpp.o)"