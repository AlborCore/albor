# Third-party technique references

`render-albor` is the repository's own GPU renderer. **No upstream source code is vendored or copied into it.** The module re-implements techniques that other projects proved, in this repository's style, so that the implementation is owned and maintained here (§6.3, AD-26).

The projects below are the design references whose techniques this module re-implements. Each snapshot records the commit the technique study consulted.

## Reference snapshots

| Project | Snapshot | License | Role |
| --- | --- | --- | --- |
| [nanovg](https://github.com/memononen/nanovg) | `ce3bf74` (2026-02-19) | zlib | Antialiasing by fringe-expanded geometry, fill fans, stroke expansion |
| [femtovg](https://github.com/femtovg/femtovg) | `9f5ad26` (2026-10-13) | MIT OR Apache-2.0 | Rounded scissor, fill rules, composition modes, gaussian blur, layers |
| [thorvg](https://github.com/thorvg/thorvg) | `92032454` (2026-06-10) | MIT | Feature checklist and API comparison for the alternatives study |
| [anyrender](https://github.com/DioxusLabs/anyrender) | `870407d` (2026-10-03) | MIT OR Apache-2.0 | Interface shape that confirmed the HAL (the comparison recorded on epic #102) |
| [MetalNanoVG](https://github.com/ollix/MetalNanoVG) | `0bbbf84` (2026-10-08) | MIT | Metal techniques: coverage framebuffers, state cache, drawable lifetime, density viewport |

## Technique map

| Technique | Source | Applied in |
| --- | --- | --- |
| Feather-expanded geometry with a fragment-shader AA | nanovg | Portable geometry core (P1) and the fill shader (P2) |
| Fill via expanded fans, stroke via expanded contours with joins and caps | nanovg | Portable geometry core (P1) |
| Rounded scissor for `clip_rounded_rect` | femtovg | Portable clip state (P3, P5) |
| Non-zero and even-odd fill rules | femtovg | Core and shaders (P5) |
| Composition modes and opacity layers | femtovg | Core and shaders (P5) |
| Gaussian-blur shadow passes | femtovg | Shadow execution (P3) |
| Single-channel coverage framebuffers | MetalNanoVG | Metal backend (P2) |
| Shared CPU/GPU buffers, pipeline state cache, precompiled shaders | MetalNanoVG | Metal backend (P2) |
| Drawable retention and fractional-density viewport | MetalNanoVG | Metal surface (P2) |

## Licensing

Because no code is copied, no upstream notice obligation attaches to a binary built from this module. Each reference project keeps its own license in its own repository. If a change ever copies source instead of re-implementing a technique, its notice is added to this file and to the file header in the same change.
