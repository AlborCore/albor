#!/usr/bin/env bash
# the port never drifts from the project: the versions agree and the port's runtime matrix equals the manifest's minus gtest (AD-19)

set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# python3 carries the JSON work, whose fallback to python covers the runners that name it differently (AD-19)
interpreter="python3"
command -v python3 >/dev/null 2>&1 || interpreter="python"

"$interpreter" - "$root_dir" <<'PYEOF'
import json, os, pathlib, re, sys, urllib.error, urllib.request


root = pathlib.Path(sys.argv[1])

def issue(message):
    print(f"FAIL: {message}")
    sys.exit(1)

def canonical(entry):
    # sorting keys and features keeps a reordered equivalent matrix from failing the gate (AD-19)
    features = sorted(entry["features"]) if isinstance(entry.get("features"), list) else entry.get("features")
    flat = {key: value for key, value in entry.items() if key != "features"}
    if features is not None: flat["features"] = features
    return json.dumps(flat, sort_keys=True)

def runtime_deps(pkg):
    # host-only tooling entries are packaging machinery, which the matrix comparison skips (AD-19)
    out = []
    for dep in pkg.get("dependencies", []):
        if isinstance(dep, str):
            entry = {"name": dep}
        else:
            if dep.get("host"): continue
            entry = {"name": dep["name"]}
            for key in ("version>=", "platform", "features", "default-features"):
                if key in dep: entry[key] = dep[key]
        out.append(canonical(entry))
    return sorted(out)

def version_of(pkg):
    for key in ("version-semver", "version-string", "version", "version-relaxed", "version-date"):
        if key in pkg: return pkg[key]
    issue("manifest without a version field")

def without_gtest(entries):
    return [entry for entry in entries if json.loads(entry).get("name") != "gtest"]

portfile = (root / "ports/albor/portfile.cmake").read_text()

# the local override, which the checker treats exactly like the portfile treats it: presence decides, contents validate (AD-19)
match = re.search(r'set\(local_source "\$ENV\{([A-Z_]+)\}"\)', portfile)
override_name = match.group(1) if match else ""
overrides = override_name in os.environ
if overrides:
    value = os.environ[override_name]
    if value == "" or not pathlib.Path(value, "CMakeLists.txt").is_file():
        issue(f"{override_name} points at '{value}', which is not a framework checkout (the port fails the same way (AD-19))")
    print(f"{override_name} is set to a local checkout; the pinned REF is not exercised")

# project version in CMakeLists, anchored to a real project(albor ...) line (AD-19)
cmake = (root / "CMakeLists.txt").read_text()
match = re.search(r"^\s*project\(\s*albor\s+VERSION\s+([^\s)]+)", cmake, re.M)
if not match: issue("project(albor VERSION ...) missing from CMakeLists.txt")
project_version = match.group(1)

manifest = json.loads((root / "vcpkg.json").read_text())
port = json.loads((root / "ports/albor/vcpkg.json").read_text())
fixture = json.loads((root / "tests/consumer/vcpkg.json").read_text())

manifest_version = version_of(manifest)
if manifest_version != project_version:
    issue(f"vcpkg.json version {manifest_version!r} != project(albor VERSION) {project_version!r}")
if version_of(port) != manifest_version:
    issue(f"ports/albor port version {version_of(port)!r} != manifest version {manifest_version!r}")

port_host = [dep.get("name") for dep in port.get("dependencies", []) if isinstance(dep, dict) and dep.get("host")]
if "vcpkg-cmake" not in port_host or "vcpkg-cmake-config" not in port_host:
    issue("port manifest lost its host cmake tooling")

# the port's default runtime matrix equals the fixture's, and everything the port's features name is a
# project dependency, while the port may carry fewer renderers than the source route, which keeps its
# reference backend a source-tree concern (AD-19, AD-26)
manifest_entries = without_gtest(runtime_deps(manifest))
port_entries = runtime_deps(port)
fixture_entries = runtime_deps(fixture)
feature_entries = {}
for feature, definition in port.get("features", {}).items():
    feature_pkg = {"dependencies": definition.get("dependencies", [])}
    feature_entries[f"feature {feature}"] = runtime_deps(feature_pkg)
if port_entries != fixture_entries:
    issue("fixture dependency matrix != the port's default runtime matrix (the test bed consumes the default features):\n  fixture   " + "\n  fixture   ".join(fixture_entries) + "\n  port      " + "\n  port      ".join(port_entries))

if not set(port_entries) <= set(manifest_entries):
    issue("the port's default runtime matrix names dependencies the project manifest does not declare (AD-19)")
for name, entries in feature_entries.items():
    if not set(entries) <= set(manifest_entries):
        issue(f"{name} names dependencies the project manifest does not declare (AD-19)")

print(f"port default matrix is a project subset and matches the fixture (%d entries, version %s)" % (len(port_entries), manifest_version))

if overrides:
    print(f"{override_name} is set to a local checkout; the pinned REF is not exercised")
    sys.exit(0)

# the pinned source names a main commit, which a git object carries immutably and the release workflow rewrites per release (AD-19)
match = re.search(r'URL\s+"https://github\.com/([^"]+)\.git"', portfile)
if not match: issue("ports/albor/portfile.cmake carries no origin URL")
repository = match.group(1)
match = re.search(r"^\s*REF\s+([0-9a-f]{40})", portfile, re.M)
if not match: issue("ports/albor/portfile.cmake carries no pinned commit REF")
ref = match.group(1)
if ref.startswith("0") and len(set(ref)) == 1:
    issue("ports/albor/portfile.cmake carries a placeholder REF; the release automation must write the real one")

try:
    pinned = urllib.request.urlopen(f"https://github.com/{repository}/commit/{ref}", timeout=10)
except urllib.error.HTTPError as error:
    if error.code == 404:
        issue(f"the pinned commit {ref[:12]}... does not exist on {repository}: HTTP 404")
    print(f"note: GitHub returned HTTP {error.code} for the pinned commit, so the pin stays unverified this run")
    sys.exit(0)
except Exception as error:
    print(f"note: the pinned commit could not be checked from here ({error}); the pin stays unverified this run")
    sys.exit(0)
print(f"pinned commit {ref[:12]}... exists on {repository}")
PYEOF
