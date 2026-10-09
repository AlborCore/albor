# Defect: the vendored shaper agrees on glyphs but not on line box placement

Context: `text/text-harfbuzz/` implements the vendored shaper of phase 3 of `TEXT-RENDERING-PLAN.md`.
The typeface question it raised first is resolved; this is what is left.

**The decision this needed is answered by AD-34 (section 15):** cross-platform pixel identity is the
objective, so the vendored engine defines the metrics and the oracle is compared against it. Matching
the oracle was never the requirement.

## Summary

The two engines **agree on shaping** and **disagree on where the baseline sits**. For Arial at 20
logical pixels the oracle places the first baseline at **15.0** and the vendored engine places it at
**18.105**, a difference of **3.105 logical pixels**. The rendered pictures are therefore different
images, and the measured worst-channel distance between them is 255.

This is why `text/text-skia/` cannot be retired, and why phase 3 is not finished.

## What already agrees

Measured by `tests/text_conformance.test.cpp`, all of which passes:

- glyph count, including on accented and punctuated text
- every glyph position, within 0.75 logical pixels
- measured run width, within 1.0 logical pixel
- reproducibility across repeated measures

So the vendored shaper is doing the hard part correctly. The remaining difference is line box
geometry, not shaping.

## The cause

The vendored engine places the baseline at `ascender * font_size`:

```cpp
line.origin = Point{.x = 0.0f, .y = m_state->ascender * font_size};
```

Arial's ascender is 1854 of 2048 units, giving 18.105 at 20 pixels. The oracle's paragraph reports a
line origin of 15.0, which is not the ascender at all. A line box places its baseline using the face's
vertical metrics **and** the leading the paragraph distributes, and the vendored engine models a line
box with one number. It therefore cannot agree with a paragraph engine that models three.

## Plan

1. **Model the line box the way the specification does rather than as one number.** §9.1 says text
   measurement is what containers adapt to; a line box needs ascent, descent and leading, and the
   baseline follows from all three. The face exposes `ascender`, `descender`, `height` and, through
   FreeType, the OS/2 and hhea tables the metrics are chosen from.
2. **The vendored engine defines the metrics; the oracle is compared against it.** This is answered by
   AD-34 and section 15, and it is not a choice between two good options. Three platforms cannot agree
   with a macOS paragraph engine, so matching one was never the goal: the metrics must come from the
   vendored engine over an embedded font, and the oracle's job is to be measured against it until it is
   deleted. The fix is therefore to model ascent, descent and leading in the vendored line box, not to
   tune the vendored engine toward a paragraph engine that will not exist on the other two platforms.
3. **Then compare pixels**, which is the real test and the one `AVendoredRunDrawsOnTheReferenceSurface`
   already runs. It records the distance rather than asserting it, because the distance is 255 today.

## Verification

- The glyph level conformance suite stays green, so fixing line box geometry cannot silently regress shaping.
- `AVendoredRunDrawsOnTheReferenceSurface` renders the same run from both engines on the reference
  surface and reports the worst-channel distance; it must drop to a value inside the written tolerance.
- `TheTwoEnginesDisagreeOnBaselinePlacement` currently records the two baselines and must be deleted
  or turned into an assertion once they agree.
- No tolerance is introduced before the metrics agree, because a tolerance wide enough to hide a
  three pixel baseline error would also hide every error worth finding.
