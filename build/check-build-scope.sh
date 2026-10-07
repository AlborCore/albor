#!/bin/bash

# build-scope check for the codegen (AGENTS.md, §14.3): a ui body edit must
# rebuild only that component's generated cpp, and an interface change must
# rebuild the generated header and its includers. The product observed is the
# albor-scope-fixture static library under tests/scope.
#
# usage:
#   bash build/check-build-scope.sh
#
# the script edits tests/scope/scope_widget.ui, and restores it from a backup
# copy on every exit path

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the fixture the check edits, which is restored from a backup copy every time
FIXTURE_UI="tests/scope/scope_widget.ui"
FIXTURE_TARGET="albor-scope-fixture"

# the backup is a bare copy, because the fixture may be untracked and uncommitted
BACKUP="$(mktemp "${TMPDIR:-/tmp}/albor-scope-ui.XXXXXX")"
cp "$FIXTURE_UI" "$BACKUP"

# refuse a dirty tracked fixture, because the check would clobber the edits
if git ls-files --error-unmatch -- "$FIXTURE_UI" > /dev/null 2>&1 && ! git diff --quiet -- "$FIXTURE_UI"; then
    echo "error: $FIXTURE_UI is dirty, commit or stash it before running the check" >&2
    exit 1
fi

# a configured build tree is required, because the check measures recompiles
if [ ! -f .build-debug/CMakeCache.txt ]; then
    echo "error: no debug build tree, configure it with 'cmake --preset debug'" >&2
    exit 1
fi

# a failed run must not leak the edits, so the trap restores the fixture
restore_fixture() {
    cp "$BACKUP" "$FIXTURE_UI"
}
trap restore_fixture EXIT

# the object units that a build compiles in one run, which the assertions measure
recompiled() {
    cmake --build --preset debug -j8 --target "$FIXTURE_TARGET" 2>&1 \
        | sed 's/\x1b\[[0-9;]*m//g' \
        | awk '/Building CXX object/{n=split($NF,a,"/"); print a[n]}' \
        | sort -u
}

# the first build generates the facade and compiles both units, so the state is warm
recompiled > /dev/null

# a body edit must compile only the generated unit, because the header is unchanged
sed -i '' 's/spacing: 21px/spacing: 23px/' "$FIXTURE_UI"
if [ "$(recompiled)" != "scope_widget.cpp.o" ]; then
    echo "error: a ui body edit rebuilt more than the component unit" >&2
    exit 1
fi

# the fixture is restored before the interface edit, which starts from the committed file
restore_fixture

# an interface edit must compile the generated unit and its includer, because the header changed
sed -i '' '/in property<string> user_name: "World";/a\
    in property<string> scope_label: "live";
' "$FIXTURE_UI"
if [ "$(recompiled)" != "$(printf 'scope_fixture.cpp.o\nscope_widget.cpp.o')" ]; then
    echo "error: a ui interface edit rebuilt the wrong set" >&2
    exit 1
fi

# the fixture is restored, and a final build regenerates the facade from the committed file
restore_fixture
cmake --build --preset debug -j8 --target "$FIXTURE_TARGET" > /dev/null 2>&1 || true
trap - EXIT
rm -f "$BACKUP"

echo "build-scope: ok (body: scope_widget.cpp.o; interface: scope_fixture.cpp.o + scope_widget.cpp.o)"