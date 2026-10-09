# Defect D9.4: Canvas draw callbacks run on the ui thread

Draft issue report. Context and the sibling defects live in `RENDERING-DEFECTS.md`.

## Summary

`Canvas` is the application's escape hatch for custom drawing: the application hands it
a draw callback and the callback draws whatever it wants with the render context
(`widgets/canvas.cpp:27-35`). The callback runs during the record pass, which is stage
one, so it executes on the ui thread. The two stage split (AD-18) protects the ui
thread from rendering cost, but the canvas path pushes application rendering cost into
exactly that stage: expensive custom content is recorded on the ui thread on every
frame it builds, while stage two only replays what was recorded.

This is not an output correctness defect. The recorded output is correct, and the
callback runs on the ui thread because that is the only thread where it may touch
application state; moving it to stage two would violate AD-18 and require
synchronization. The defect is a policy gap: nothing states that a canvas callback must
be cheap, the cost recurs every frame even when the underlying data did not change
(recording is whole tree per frame, D6), and the framework offers no supported way to
move the expensive work off the frame.

## Defect

1. **Application code runs in stage one.** `Canvas::render` invokes the application
   draw callback while `SceneBuilder::build` records the scene
   (`widgets/canvas.cpp:27-35`). The render thread never sees the callback; it replays
   the primitives the callback recorded.

2. **The cost recurs per frame.** A canvas that records thousands of primitives pays
   the full record cost on every frame the tree builds, even when its data has not
   changed, because the build re-records the whole tree (D6).

3. **The cheap path exists but is not wired.** The retained resource contract (§6.4,
   AD-06) already describes exactly the missing mechanism: immutable, shared pointer
   resources that the scene references in constant time. A canvas could record one
   `draw_picture` for its whole content. Nothing connects the canvas widget to
   preparing such a resource, and no documentation tells the application to.

4. **The constraint is unstated.** A reasonable reading of the canvas role is that
   custom rendering runs where rendering runs. It does not: it runs on the ui thread,
   and nothing warns the application.

## Reproduce

Give a canvas a draw callback that records a ten thousand point line batch. Observe the
ui thread's build time scale with the point count on every frame, while stage two's
replay stays trivial.

## The options

Implementation time decides between these. Both keep AD-18 intact: application code
never runs on the render thread.

### Option A: document the constraint, keep the callback on the ui thread

The canvas callback stays a stage one hook, and the contract is made explicit.

Plan:

1. State the constraint where the canvas is documented and in the canvas header: a
   draw callback must be cheap, because it runs on the ui thread during scene build.
2. Document the escape hatch the application is expected to use for expensive content:
   record the heavy content once, when its data changes, into a retained resource
   (`PictureResource`, `PathResource`, `LineBatch`), and have the callback record one
   `draw_picture` or `draw_path` per frame.
3. The retained resources already cross the thread boundary as immutable shared
   pointers (§10.5, §13.2), so the prepared content can be produced on any thread the
   application owns. No framework change is required for this path; it is a
   documentation change plus, at most, a guide example.

Cost and benefit: the cheapest option, no API change, no new threading surface. The
expensive path remains possible but stays the application's responsibility, and the
per frame cost of an unprepared canvas remains the application's problem to notice.

### Option B: build the retained prepare path as a first class canvas mode

The framework owns the mechanism: an application records a picture when its data
changes, off the frame path, and the canvas stamps a reference into the scene in
constant time.

Plan:

1. Add a prepare mode to the canvas: the application registers a prepare callback that
   records into a `PictureResource` through a picture recorder. The prepare callback
   runs when the canvas is told its data changed, not per frame.
2. The prepare callback may run on any thread the application chooses, including a
   worker; the finished picture is published as an immutable snapshot
   (`std::shared_ptr<const PictureResource>`, per §10.5, §13.2), which the canvas
   observes.
3. At scene build, the canvas records a single `draw_picture(picture, transform)`
   (§6.4, AD-06) in constant time. The per frame cost of a prepared canvas is one
   reference stamp, regardless of the content's complexity.
4. The picture recorder already exists (`render/picture_resource.h:111-199`) and
   records through the same `RenderContext` surface the canvas callback uses today, so
   the callback signature does not change; only where and when it runs does.
5. Constraints to design at implementation time:
   - the picture must be replaced atomically when new data arrives, so a build never
     observes a half recorded picture;
   - the ui thread must not block waiting on a prepare in flight: when no prepared
     picture is ready, the canvas records nothing (or its last published picture) and
     the frame presents without it;
   - lifetime: the canvas holds the latest snapshot; the destruction ordering with the
     worker that produced it must be defined;
   - the semantic tree must still describe the canvas content, which stays the
     application's responsibility as today.

Cost and benefit: a real framework capability, the natural completion of the retained
resource contract, and it removes the per frame cost entirely for prepared content.
Cost: new API surface, a new threading pattern to test, and a policy for stale or
missing prepared content that must be decided.

### Option C: application provided rendering against a renderer specific native context

The callback receives the renderer's own drawing context instead of recording through
the framework's primitive surface — the maximum control surface, and the shape that
direct native surface access requires.

Plan:

1. A two layer API. Layer one is stable and renderer neutral: the callback receives
   the item size, the scale factor, the accumulated local to screen transform and the
   current clip. Layer two is renderer specific access to the concrete drawing
   context, reached through a downcast hook on the render context; a renderer that
   does not implement the hook reports unsupported explicitly, so the application can
   branch or refuse.
2. No universal drawing API is invented: each renderer already defines its own
   operations, and the callback uses them directly. The framework provides the
   context — transform, clip, size — not a parallel drawing vocabulary.
3. The callback runs during stage one on the ui thread, exactly like today's canvas
   callback, so the constraint that it must be cheap still applies and must be
   documented.
4. The per frame cost problem remains unsolved by this option on its own: content is
   re-drawn per frame unless the application combines it with Option A's escape hatch
   (retained resources recorded when data changes) or Option B's prepare path. For
   the schematic editor case this is acceptable — the application is re-rendering a
   scene whose contents change continuously anyway.

Cost and benefit: direct native access is exactly what a schematic editor needs, and
no retained plumbing is required. Cost: application code becomes renderer specific,
and every non-implementing renderer needs a documented unsupported path.

## Status

**Option A is done, and the contract is now written where a caller will read it**: the header of
`widgets/canvas.h` states that the callback is on the ui thread during recording and must be cheap.
Nothing in the runtime changed, because the recorded output was already correct.

The header says less than it should. The comment rule in `STYLE-GUIDE.md` forbids consecutive comment
lines, so the four point contract cannot be written as a block above the class; it is one line in the
header and the rest of it lives here. A caller who reads only the header learns the thread and the
cost rule, and this document carries the rest: that the callback must not block, that it may touch
application state because it is the one place custom drawing may, and that expensive content belongs
in a retained resource recorded once and replayed. Writing that contract properly needs a doc comment
form the rule exempts, which is a change to `STYLE-GUIDE.md` and not to this defect.

Options B and C remain open and both need a decision before they are built: B is a framework
capability that should land with the retained resource work, and C is a native control surface. The
open question below is the decision that has not been taken (§6.4).

## Open question for implementation time

Option A is a documentation change; Option B is a framework capability; Option C is a
native control surface. They are not mutually exclusive — the schematic editor would
use C while ordinary content uses A or B:

- If expensive canvas content is rare and no direct native access is needed yet,
  Option A is enough, and B or C can be added later without breaking the documented
  contract.
- If expensive content is common, Option B should be built while the retained
  resource work of `RENDERING-DEFECTS.md` is already open, so the command list and
  the prepare path land together.
- If direct native surface access is the near term need — the circuit schematic
  editor — Option C is the one to build first, combined with A's documented
  constraint, and B can follow for prepared content.
