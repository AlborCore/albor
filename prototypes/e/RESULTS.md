# Prototype E — Results

Dev runtime and layout invariance (§2.3, §2.4, §17.2, §30, AD-02, AD-04, AD-11, AD-25). Thresholds were
recorded in `plan.md` before any run and are not weakened here. Reproduce with:

```sh
bash prototypes/e/measure_e.sh
```

## Machine

Intel Core i7-9750H @ 2.60 GHz, 6 cores / 12 threads, 16 GB RAM, macOS 26.6.2 (Darwin 25.6.0),
Apple Clang 21.0.0, C++23, libc++. Release = preset `release` (`ALBOR_DEV=OFF`); Dev = preset `debug`
(`ALBOR_DEV=ON`, links `albor-prototype-e-devrt`). Layout dumps are deterministic, so one run each
suffices; the conformance test was repeated 5× as `plan.md` requires.

## Summary

| # | Question | Threshold | Measured | Verdict |
| --- | --- | --- | --- | --- |
| T1 | Class sizes identical across configurations | zero byte difference | identical | **PASS** |
| T2 | Virtual interface identical across configurations | identical vtable dispatch | identical | **PASS** |
| T3 | Dev features attach only through the hook | Dev non-null, Release null | `non-null` / `null` | **PASS** |
| T4 | IR round-trip preserves every property | exact match | `MATCH [before-patch]` | **PASS** |
| T5 | `UiPatch` applied by `id:` matches the compiled path | exact match | `MATCH [after-patch]` | **PASS** |

All five pass, and the AD-04 link boundary holds independently: the Release binary contains **zero**
`DevSideTable` or `dev_hook_body` symbols.

## T1 / T2 — layout invariance

Both configurations built and their dumps diffed. Output is byte-identical apart from the one line that
is *supposed* to differ:

```
sizeof(UiNode)        = 16          sizeof(TextNode)      = 32
sizeof(ContainerNode) = 32          UiNode m_id offset    = 0
vtable type_name      = TextNode    vtable read_property  = 16.00
vtable write_property = ok          dev_hook              = null   (release) / non-null (dev)
```

- **Sizes identical** — `UiNode` 16, `TextNode` 32, `ContainerNode` 32 in both.
- **Field offsets identical** — the identity field sits at offset 0 in both.
- **Virtual interface identical** — all three slots are dispatched through a `UiNode*` base pointer and
  return the same values, so no slot moved or vanished. `write_property` round-trips through the base
  pointer and reads back exactly, which catches a slot that survived but reordered.
- **Sentinels asserted at compile time.** `layout_check.cpp` carries the Release-measured sizes as
  `static_assert`s (`== 16`, `== 32`, `== 32`), so any later change to a class layout is a **build
  failure**, not something only a diff would notice. This is the guard AD-02 actually needs, and it is
  now active rather than a placeholder.

The only difference between the configurations is the hook body pointer, and that is the intended
behaviour: the *slot* is part of the Release layout and is null there, while Dev installs a body into it.

## T3 — extension hook attaches only in Dev

| Configuration | `dev_hook` |
| --- | --- |
| Release | `null` |
| Dev | `non-null` |

The hook slot is declared in `node.h`, which is compiled identically in both configurations and contains
no `#ifdef` anywhere. The Dev-only work happens entirely in `node_dev.cpp`, which Release never links.
This is the AD-04 rule in the form §2.3 requires: the feature is excluded at **link time**, not by a
preprocessor branch that would change class layout.

### A defect that made this threshold vacuous, and the fix

The first run reported `dev_hook = null` in **both** configurations, and the layout dumps were
identical. That looks like a pass and is in fact the opposite: with the hook null in Dev, the test was
proving nothing, because the Dev build was not actually attaching anything.

The cause is a static-initialisation rule rather than a bug in the intent. `node_dev.cpp` installs the
hook from a `static const DevHookInstaller` whose constructor writes `UiNode::s_dev_hook`. That
installer sits in an anonymous namespace, so no symbol outside the translation unit can name it, and a
linker only extracts an object file from a static archive when something references a symbol inside it.
The whole file was therefore discarded, the constructor never ran, and `devrt` contributed nothing.
`nm` on the Dev binary confirmed it: one dev symbol, and it was the *undefined* reference to
`s_dev_hook` itself.

The fix gives the archive a symbol to resolve, and forces it to be linked:

- an externally visible anchor, `albor_prototype_e_devrt_link_anchor`, and
- `-Wl,-force_load` on the devrt archive in the Dev configuration only.

`dev_hook` is now `non-null` in Dev and still `null` in Release. The threshold is real.

This is worth recording as a class of defect rather than a one-off: **a dev-runtime extension can be
linked, appear in the build, and still be entirely inert**, and the only symptom is a test that passes
for the wrong reason. Any future check of this shape has to assert that the Dev side is *active*, not
merely that both sides agree.

## T4 / T5 — dual-engine conformance and patch application

`albor-prototype-e-conformance` builds a 16-node tree (4 `ContainerNode`, 12 `TextNode`), gives every
node four distinct property values so a defaults-coincidence false positive is impossible, serialises to
an `IrTree`, deserialises into a fresh node set, and compares **every** property slot with exact float
equality — no tolerance, because no arithmetic is involved.

- **T4, before patch:** `MATCH [before-patch]`, 16 nodes × 4 properties = 64 slots.
- **T5, after patch:** `SetProperty` on nodes 1, 7, 14 and `ReplaceBinding` on node 3, applied to both
  the original and the loaded tree, then re-compared: `MATCH [after-patch]`.

Repeated 5× in both configurations, identical output every run, exit 0 throughout.

Patches address nodes by explicit `id:` key rather than by position, which is the AD-11 requirement.

### What this does not establish

Two limits, stated so the result is not read as more than it is:

1. **`ReplaceBinding` is not really exercised.** In this slice it is applied as a `SetProperty` with the
   same shape, as the prototype notes. So the patch *plumbing* by `id:` is confirmed, but binding
   replacement — the thing that actually matters for live reload — is not. A binding-replacement test
   needs a real binding graph to replace, and this slice has none.
2. **The "compiled path" side is not an independent engine.** Both sides of the comparison use the same
   `read_property`/`write_property` implementation in `node.cpp`. This establishes that serialise →
   deserialise → patch preserves values, and that the same patch yields the same result on both trees.
   It does **not** establish agreement between two independently generated engines, which is what
   AD-25's dual-engine conformance ultimately requires.

## Defects found and fixed while measuring

1. **Placeholder sentinels.** `layout_check.cpp` shipped with `kSentinel*  = 0` and a comment saying
   values would be inserted "after the first Release run". That never happened, and the guard was worse
   than inactive: `if constexpr (kSentinelUiNode != 0)` is true for `0`-valued *sentinels*, so
   `static_assert(sizeof(UiNode) == 0)` fired and **the layout check did not compile at all**. The
   sentinels are now the measured Release sizes and the asserts are unconditional.

2. **`offsetof` on a polymorphic type.** The original code used `offsetof(UiNode, m_id)`, which is not
   well-formed on a non-standard-layout type and emitted a `-Winvalid-offsetof` warning. The offset is
   now computed through a pointer difference, which is both well-defined and what a real ABI consumer
   would do.

3. **The inert devrt link**, described above. This is the significant one.

4. **Dev configuration was stale.** The `debug` build tree predated `prototypes/e`, so the E targets did
   not exist in it and `cmake --build --preset debug` failed with "No rule to make target". The tree has
   been reconfigured; the script now configures both presets before measuring so this cannot recur
   silently.

5. **Duplicate archive on the Dev link line** (`ignoring duplicate libraries`), because `devrt` links the
   node library publicly. Fixed with `LINK_INTERFACE_MULTIPLICITY`.

## What this leaves open

- **AD-02's layout invariance is established** for the three node classes under test, with a
  compile-time guard so it stays established.
- **`ReplaceBinding` needs a real test.** Until then the live-reload path is unverified.
- **The two engines are not independent.** AD-25 needs a genuinely separate compiled path.
- **Link-time exclusion is verified by symbol inspection here**, not by a scripted link-boundary check.
  `plan.md` refers to `build/check-link-boundary.sh`; that script does not exist, so the boundary was
  confirmed with `nm` by hand inside this prototype. Making it a permanent gate is the natural next step.
- **Only two configurations are compared.** §2.3 also names Test (`ALBOR_TEST`); the preset exists but
  was not exercised here.