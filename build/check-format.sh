#!/bin/bash

# clang-format linter for the C++ sources under prototypes/ and tests/
# exits non-zero when any file violates the project .clang-format
# exits non-zero when .clang-format itself is not the pinned style file

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the digest of the style file this repository ships, so an environment that
# overwrites it with clang-format's own dump output fails the check instead of
# silently reformatting the whole tree against a different style
CLANG_FORMAT_SHA256="4f9e53ddf0d41a6c5e758cf5300fe9f0128a3d64f4550a99f82e522b95643260"

# hash the style file with whatever digest tool the host provides
clang_format_digest() {
    if command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        openssl dgst -sha256 "$1" | sed 's/.*= *//'
    fi
}

# reject a missing style file, because clang-format would then fall back to its built in style
if [ ! -f .clang-format ]; then
    echo "error: .clang-format is missing, restore it before checking the format" >&2
    exit 1
fi

# reject a style file that is not the pinned one
actual_digest="$(clang_format_digest .clang-format)"
if [ "$actual_digest" != "$CLANG_FORMAT_SHA256" ]; then
    echo "error: .clang-format does not match the pinned style file" >&2
    echo "  expected $CLANG_FORMAT_SHA256" >&2
    echo "  actual   $actual_digest" >&2
    echo "  restore it, and update CLANG_FORMAT_SHA256 only when the style change is intended" >&2
    exit 1
fi

# generated sources that clang-format must not reformat
EXCLUDED=()

# the regex below matches every extension family in this repository:
#   *.cpp   *.h            — core, tests
#   *.mm    (e.g. *.macos.mm)   — Objective-C++
#   *.c++   (e.g. *.linux.c++, *.win32.c++) — platform C++ extensions
# adding a new extension family requires updating the grep pattern on line ~51

# collect the tracked and the not yet committed C++ sources, dropping the generated files
files=()
while IFS= read -r file; do
    [ -z "$file" ] && continue
    files+=("$file")
done < <(git ls-files --cached --others --exclude-standard './**' | grep -E '\.(cpp|h|c\+\+|mm)$' | { if [ ${#EXCLUDED[@]} -gt 0 ]; then grep -Fxv -e "${EXCLUDED[0]}"; else cat; fi; })

if [ ${#files[@]} -eq 0 ]; then
    echo "error: no C++ source files found" >&2
    exit 1
fi

# clang-format's --dry-run --Werror prints each violation and exits non-zero
clang-format --dry-run --Werror "${files[@]}"
