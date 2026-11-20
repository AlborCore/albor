#!/usr/bin/env bash
set -euo pipefail

# chocolatey returns success when its feed is down, so the binary decides the result
if command -v ccache >/dev/null 2>&1; then
    exit 0
fi

# the hosted feed throws transient 503s, so the install gets two more attempts
for attempt in 1 2 3; do
    if choco install ccache --no-progress -y > /dev/null 2>&1 && command -v ccache >/dev/null 2>&1; then
        exit 0
    fi
    sleep 10
done

# the pinned upstream build keeps a feed outage from blocking a windows compile
version=4.11.3
archive="ccache-${version}-windows-x86_64.zip"
curl -fsSL "https://github.com/ccache/ccache/releases/download/v${version}/${archive}" -o "$archive"
unzip -q -o "$archive" -d ccache-download
bin="ccache-download/ccache-${version}-windows-x86_64/ccache.exe"
[ -x "$bin" ] || { echo "ccache ${version} could not be installed" >&2; exit 1; }
echo "$(pwd)/$(dirname "$bin")" >> "$GITHUB_PATH"
