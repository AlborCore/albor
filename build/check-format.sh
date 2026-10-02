#!/usr/bin/env bash
# check-format.sh - verify that every C++ source in the repository matches .clang-format

set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
style_file="$root_dir/.clang-format"
diff_file="$(mktemp)"
status=0
count=0

trap 'rm -f "$diff_file"' EXIT

# collect the C++ files tracked in the repository, skipping build directories
sources="$(cd "$root_dir" && git ls-files --cached --others --exclude-standard -- '*.cpp' '*.h')"

if [ -z "$sources" ]; then
    echo "check-format: no C++ sources found"
    exit 0
fi

while IFS= read -r source; do
    if [ -z "$source" ]; then
        continue
    fi

    count=$((count + 1))

    # compare the file with its formatted form, one file at a time so the report names the offender
    if ! diff -u "$root_dir/$source" <(clang-format --style=file:"$style_file" "$root_dir/$source") > "$diff_file"; then
        echo "check-format: $source is not formatted"
        cat "$diff_file"
        status=1
    fi
done <<EOF
$sources
EOF

if [ "$status" -eq 0 ]; then
    echo "check-format: $count files OK"
fi

exit "$status"
