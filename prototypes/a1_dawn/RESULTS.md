# Prototype A1-Dawn — results

Thresholds were committed to [`plan.md`](plan.md) before any run. Prototype code: not linked by any runtime module
(AGENTS.md).

## Status

**Incomplete.** The dependency integrates and links; the renderer and the workloads were not implemented, so **no latency
figure in this file is a measurement**. What follows is what was actually established, and what was not.

## Dependency: established

Dawn builds and links on the reference machine.

| Fact | Evidence |
| --- | --- |
| The `dawn` port builds on macOS x64 with the `metal` feature | `dawn[core,metal]:x64-osx@20260624.223603` installed |
| CMake integration works as documented | `find_package(Dawn CONFIG REQUIRED)` → `dawn::webgpu_dawn` |
| Headers install | `include/dawn/{webgpu.h,webgpu_cpp.h,native/,dawn_proc_table.h}` |
| Static library installs | `debug/lib/libwebgpu_dawn.a` (710 MB), `lib/libwebgpu_dawn.a` (20 MB) |

**Wall time of the successful port build: 757 s (12.6 min).**

## The build-time threshold was not evaluated

The plan recorded "Clean build time ≤ 3.0 minutes", measured with **ccache cold**. That precondition was **not met**:

- The first attempt failed partway through, leaving tint's objects in ccache.
- The measured 757 s run therefore started with a warm cache for part of the port.

So 757 s is **not** a clean-build figure, and it is **not comparable** to any Skia figure, because no Skia clean build was
measured on this machine. The only Skia number in this repository is an 11 min warm rebuild, which is a different
measurement of a different thing.

**Disposition: threshold not evaluated, precondition not met.** The budget is left exactly as recorded (§28.4); it is not
restated and it is not reported as a pass or a fail. Getting a real answer needs a cold build of both ports, which is
several hours of machine time and is deferred rather than approximated.

For scale only, and explicitly **not** as a comparison: the Dawn debug static library is 710 MB against Skia's 670 MB,
both for the `x64-osx` debug triplet.

## The brief's API shape does not match this Dawn

The task specifies implementing `wgpu::Surface` and `wgpu::SwapChain`. In Dawn 20260624 the surface path has moved on, so
the implementation has to be written differently. Verified from the installed client header:

| The brief assumes | What this Dawn actually exposes |
| --- | --- |
| `wgpu::SwapChain` as the presentation object | no `SwapChain` object; `Surface::Configure`, `Surface::GetCurrentTexture(SurfaceTexture*)`, `Surface::Present()` |
| a swapchain configured with width/height/format | `Surface::Configure(const SurfaceConfiguration*)`, and the surface is created from `SurfaceDescriptor` with `SurfaceSourceMetalLayer { void* layer; }` chained in |
| synchronous adapter/device acquisition | `Instance::RequestAdapter(...)` and `Adapter::RequestDevice(...)` are callback-mode calls returning a `Future` |

The `SwapChain` *symbol type* still appears 38 times in the header (state enums, statuses), which is exactly why a
quick grep suggests the old API is present. The presentation object is not.

## Defect in this prototype's own process, recorded because it cost a build

The first port build failed with `fatal error: 'absl/container/flat_hash_set.h' file not found`. The cause was **not**
Dawn: the build had been invoked as a bare `vcpkg install --x-manifest-root=.` from the repository root, which leaves
`VCPKG_INSTALLED_DIR` unset and makes vcpkg default to a stray `./vcpkg_installed` at the repo root. That directory held a
partial abseil, and Dawn compiled against it. The correct invocation is `cmake --preset release`, which sets
`VCPKG_INSTALLED_DIR` to the build tree's own `vcpkg_installed`. vcpkg removed the stray directory when the build failed, so
nothing was left behind — but the rule is worth keeping: **never invoke `vcpkg install` against the manifest root without
`VCPKG_INSTALLED_DIR`; go through the CMake preset.**

## What was not done

- **No renderer.** There is no `prototypes/a1_dawn/` implementation: no surface, no pipeline, no instanced quad batch.
- **No W1, W2 or W3.** Zero latency samples exist, so there is no p50, p95 or p99 to report for recording, present
  interval, hover query or draw-call count.
- **The draw-call claim is untested.** The premise worth testing is that WebGPU's 32-bit instance index allows one
  `Draw` call for 10⁶ instanced quads, where Skia's 16-bit vertex indexing cost 220 draw calls per frame. Nothing here
  confirms or refutes it.
- **AD-05 / AD-06 not exercised.** With no code written, "zero Dawn headers in a public interface header" is vacuously
  true and therefore not a finding.

## Verification of the tree as it stands

| Check | Result |
| --- | --- |
| `build/check-format.sh` | exit 0 |
| Release build | 0 errors, 0 warnings |
| Debug build | 0 errors, 0 warnings |
| 33 gtest unit tests | 33 passed |
| `plan.md` thresholds committed before the run | yes, `b538f68` |