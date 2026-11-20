#!/usr/bin/env bash
set -euo pipefail

# chocolatey returns success when its feed is down, so the executable decides the result
install_choco() {
    local package="$1"
    shift
    for binary in "$@"; do
        if command -v "$binary" >/dev/null 2>&1; then
            return 0
        fi
    done
    # the hosted feed throws transient 503s, so the install gets two more attempts
    for attempt in 1 2 3; do
        if choco install "$package" --no-progress -y > /dev/null 2>&1; then
            for binary in "$@"; do
                if command -v "$binary" >/dev/null 2>&1; then
                    return 0
                fi
            done
        fi
        sleep 10
    done
    return 1
}

if ! install_choco ccache ccache; then
    # the pinned upstream build keeps a feed outage from blocking a windows compile
    version=4.11.3
    archive="ccache-${version}-windows-x86_64.zip"
    curl -fsSL "https://github.com/ccache/ccache/releases/download/v${version}/${archive}" -o "$archive"
    unzip -q -o "$archive" -d ccache-download
    bin="ccache-download/ccache-${version}-windows-x86_64/ccache.exe"
    [ -x "$bin" ] || { echo "ccache ${version} could not be installed" >&2; exit 1; }
    echo "$(pwd)/$(dirname "$bin")" >> "$GITHUB_PATH"
fi

# msvc coverage is collected from the pdb, which needs its own tool on the windows coverage job
if [ "${1:-}" = "--coverage" ] && ! install_choco opencppcoverage OpenCppCoverage opencppcoverage; then
    echo "opencppcoverage could not be installed" >&2
    exit 1
fi
