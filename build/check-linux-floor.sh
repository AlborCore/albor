#!/usr/bin/env bash
# the Linux artifacts must link on the RHEL/Alma 9 baseline: glibc 2.34, libstdc++ 3.4.29 and CXXABI 1.3.13
# a symbol version above the floor is a latent runtime failure on the baseline, so the check rejects it (AD-16, §5.3)

set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# the caller names the build tree or individual artifacts, because the check runs per configuration in CI
targets=("$@")
if [ "${#targets[@]}" -eq 0 ]; then
    targets=("${ALBOR_BUILD_DIR:-$root_dir/.build-release}")
fi

# readelf carries the version sections the check reads, and binutils is part of the Linux build host (AD-16)
if ! command -v readelf >/dev/null 2>&1; then
    echo "check-linux-floor: readelf is not present, so the Linux symbol floor cannot be checked" >&2
    exit 2
fi

# the objects, archives, shared libraries and executables of the build tree carry the version needs
files=()
for target in "${targets[@]}"; do
    if [ ! -e "$target" ]; then
        echo "check-linux-floor: '$target' does not exist" >&2
        exit 2
    fi
    if [ -d "$target" ]; then
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find "$target" -type f \( -name '*.a' -o -name '*.o' -o -name '*.so' -o -name '*.so.*' -o -perm -u+x \) -print0)
    else
        files+=("$target")
    fi
done

if [ "${#files[@]}" -eq 0 ]; then
    echo "check-linux-floor: no ELF artifacts under '${targets[*]}'" >&2
    exit 2
fi

# a version is above the floor when its numeric tuple is, which awk compares without the distro's tools
exceeds_floor() {
    local version="$1"
    awk -v version="$version" '
        BEGIN {
            split(version, part, ".")
            if (part[1] > 2 || (part[1] == 2 && part[2] >= 35))
                exit 0
            exit 1
        }'
}

exceeds_libstdcxx_floor() {
    local version="$1"
    awk -v version="$version" '
        BEGIN {
            split(version, part, ".")
            if (part[1] > 3 || (part[1] == 3 && part[2] > 4) || (part[1] == 3 && part[2] == 4 && part[3] >= 30))
                exit 0
            exit 1
        }'
}

exceeds_cxxabi_floor() {
    local version="$1"
    awk -v version="$version" '
        BEGIN {
            split(version, part, ".")
            if (part[1] > 1 || (part[1] == 1 && part[2] > 3) || (part[1] == 1 && part[2] == 3 && part[3] >= 14))
                exit 0
            exit 1
        }'
}

checked=0
failed=0
for file in "${files[@]}"; do
    # a file without an ELF header or an archive of ELF objects carries no version needs
    versions="$(readelf --version-info "$file" 2>/dev/null | sed -n 's/.*Name: \(GLIBC[A-Z]*_[0-9][0-9.]*\|CXXABI_[0-9][0-9.]*\).*/\1/p' | sort -u)" || continue
    if [ -z "$versions" ]; then
        continue
    fi
    checked=$((checked + 1))
    while IFS= read -r version; do
        case "$version" in
        GLIBC_*)
            if exceeds_floor "${version#GLIBC_}"; then
                echo "check-linux-floor: $file references $version, above the Alma 9 floor GLIBC_2.34" >&2
                failed=1
            fi
            ;;
        GLIBCXX_*)
            if exceeds_libstdcxx_floor "${version#GLIBCXX_}"; then
                echo "check-linux-floor: $file references $version, above the Alma 9 floor GLIBCXX_3.4.29" >&2
                failed=1
            fi
            ;;
        CXXABI_*)
            if exceeds_cxxabi_floor "${version#CXXABI_}"; then
                echo "check-linux-floor: $file references $version, above the Alma 9 floor CXXABI_1.3.13" >&2
                failed=1
            fi
            ;;
        esac
    done <<<"$versions"
done

if [ "$failed" -ne 0 ]; then
    echo "check-linux-floor: the artifacts are above the Alma 9 baseline; build on the baseline or lower the reference" >&2
    exit 1
fi

echo "check-linux-floor: $checked artifacts stay within GLIBC_2.34, GLIBCXX_3.4.29 and CXXABI_1.3.13"
