#!/bin/bash

# clang-format linter for the C++ sources under prototypes/ and tests/
# exits non-zero when any file violates the project .clang-format

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# generated sources that clang-format must not reformat
EXCLUDED=()

# collect the tracked and the not yet committed C++ sources, dropping the generated files
files=()
while IFS= read -r file; do
    [ -z "$file" ] && continue
    files+=("$file")
done < <(git ls-files --cached --others --exclude-standard 'prototypes/**' 'tests/**' | grep -E '\.(cpp|h|c\+\+|mm)$' | { if [ ${#EXCLUDED[@]} -gt 0 ]; then grep -Fxv -e "${EXCLUDED[0]}"; else cat; fi; })

if [ ${#files[@]} -eq 0 ]; then
    echo "error: no C++ source files found" >&2
    exit 1
fi

# clang-format's --dry-run --Werror prints each violation and exits non-zero
clang-format --dry-run --Werror "${files[@]}"
