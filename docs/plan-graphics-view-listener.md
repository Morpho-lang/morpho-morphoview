# Plan: Graphics live model + View listener

Design source of truth: [`definedraw.md`](definedraw.md).

**Working agreement:** one phase at a time; pause for review before the next.

**Canonical View API:** `View(scene)` or `View()` + `open(scene)` on a `Scene` (listen after one `Show.write`). `View.open(Graphics)` works for set-and-forget without listening.

## Locked decisions (Phase 1 readiness)

### Entry shape (SRT)

```
{ id, item, position, scale, rotate }
```

- `position`: **Matrix** 3-vector, default origin — absolute placement of the object origin (set, not a relative delta). API accepts list or Matrix; coerce to Matrix on store (`GraphicsEntry` does this).
- `scale`: Float default `1`
- `rotate`: `[angle, ax, ay, az]` or `nil`

API and entry both use **`position`**. Keep SRT as fields on the entry (not a nested dict, not a single 4×4) so `move` merge and `t`/`s`/`r` emit stay simple; a composite-matrix helper can come later. `Show` emits viewer command `t` from `entry.position` components.

`Show` always places from entry SRT.

### Dual pose + shared `display` / `move` signature

Both accept optional **2nd positional** `position` plus kwargs `scale=`, `rotate=`:

```
display(item)                          // identity (Sphere: center→position, r→scale)
display(item, position, scale=, rotate=)
move(id, position, scale=, rotate=)    // Phase 3; same arity pattern
```

Morpho treats `param=nil` defaults as keyword-only, so pose is a separate arity overload (not `position=nil` on the 1-arg form). `scale=` / `rotate=` remain kwargs on the posed form. `display(item, nil, scale=2)` works when position should stay default origin.
- Pose args set on `display` → entry SRT recorded; `Sphere` stored as **unit** abstract primitive (Phase 5c; Phase 1 temporarily stored matching center/r + baked).
- No pose args + `Sphere` → store unit Sphere; copy `center`→entry position, `r`→scale.
- No pose args + other primitives → identity SRT; geometry as authored.
- Same item `display`’d twice → two ids / two entries.

### `move` merge (Phase 3; lock now)

- New `position` (2nd positional or kwarg) **always** sets `entry.position`.
- Omitted `scale` / `rotate` leave that entry component **unchanged**.

### Translucent `Sphere` (Phase 1; emit superseded by 5c)

Graphics stores the abstract `Sphere`; Phase 1 Show baked via `visitGeneric`. **Phase 5c:** unit Sphere + entry SRT; Show mesh cache (few `o`, many `d`); color via `C` so instances share geometry. See Phase 5c locked decisions.

### Other

- `Graphics.add`: remap right-hand ids (unique within one Graphics).
- `open(g)`: one `Show.write`, then listen (no fake N `defined` on open).
- `update(g)`: full `U S`; rebind listener; reset Show/object maps.
- Phase 5a/5b: `beginBatch`/`endBatch` coalesce Moved; View emits pose-only `d` (in-place matrix replace, no full `D`).

## Target loop

Put the **first pose on `display`** (or `move` before `open`) so the first paint is not identity:

```
var ball = g.display(unitBall, [x0,y0,z0], scale=ballR)
var shadow = g.display(unitShadow, [x0,0.02,z0], scale=shadowR)
v = View(g)   # first paint already posed
g.move(ball, [x,y,z], scale=ballR, rotate=[angle,0,1,0])
g.move(shadow, [x,0.02,z], scale=shadowR)
```

## Non-goals

- General Morpho dependent-object framework (learn from this; formalize later)
- Auto-diff inside `update(Graphics)`
- Full scene graph / Mesh-registry API
- Binary transport; pick events
- `U O` / `U V` in the first cut (follow-on once redraw works)

## Phases

### Phase 1 — Entries + Show define/draw foundation ✅

**Files:** `share/modules/xgraphics.morpho`, `test/`

**Steps:**

1. `Graphics.display(item, position=, scale=, rotate=)` allocates id, stores entry `{ id, item, position, scale, rotate }`, returns id. No `id=` on mesh primitives. `GraphicsEntry` coerces `position` to Matrix. Apply locked Sphere pose normalization.
2. `Show` walks **entries**; emit define/draw from **entry SRT only**.
3. Spheres: store abstract `Sphere`; Show `visitGeneric` (no cache). Entry pose recorded; bake-vs-entry-SRT emit polish later.
4. `Graphics.add`: remap right-hand entry ids (unique within one Graphics).
5. Keep `replace=true` → full `U S`.
6. Tests: distinct ids; same primitive twice → two ids; `add` remaps right-hand ids; Sphere entry pose recorded; `Show` / `replace` still work.

**Done when:** `Show(g)` / `View.open(g)` emit correctly from entries; set-and-forget path unchanged in spirit; no C viewer changes.

### Phase 2 — Viewer redraw support ✅

**Files:** `src/command.c` / `command.h`, `src/scene.c` / `scene.h`, `src/display.c`, docs, `share/modules/morphoview.morpho`

**Steps:**

1. Sticky `command_applyctx` across `command_process` batches (follow-up chunks may omit leading `S`).
2. Command `D` — clear displaylist only; mark scene changed; keep objects / colors / fonts / pools.
3. `display_prepareall`: `render_reset` before re-prepare after `D`.
4. Parse: `D` must **not** require parse-time `has_scene` (ok-before-apply race).
5. Docs + make `test/command/definedraw-redraw` runnable.
6. `View.redraw(commands)` — low-level escape hatch (`D\n` + chunk).

**Done when:** `definedraw-redraw` fixture runs; `View.redraw` can clear draws and re-issue poses without `U S`.

### Phase 3 — `move` + View listener ✅

**Files:** `share/modules/xgraphics.morpho` (`Graphics`), `share/modules/morphoview.morpho` (`View`)

**Locked:**

- **Id map:** View session owns `graphicsId → viewerObjectId` (on the live `Show` instance: `objectMap`, plus `entryColorId` / `flatIds`). `open(g)` builds it from one `Show.write` — no fake N `defined` events.
- **Mid-session `display`:** notifies `GraphicsEventDefined`; View emits define+draw via `Show.writeEntry`.
- **Events:** `GraphicsEventDefined`, `GraphicsEventMoved`, `GraphicsEventRecolored`, `GraphicsEventRemoved`, `GraphicsEventReplaced`.
- **On `moved`:** pose-only `d` via `emitEntryPose` (Phase 5b); fall back to `D` + `emitPoseDraws` if unmapped.

**Steps:**

1. `move(id, …)` — locked merge rules; notify listeners. ✅
2. `beginBatch` / `endBatch` — Phase 5a (`broadcast` mixin). ✅
3. `Broadcaster.subscribe` / `Listener.listen`/`ignore` (`broadcast` module). ✅
4. `open(g)`: one `Show.write`, then listen; `update(g)`: full `U S`, rebind, reset Show/object maps. ✅
5. On `moved`: pose-only `d` (Phase 5b). ✅
6. Keep `View.redraw(ascii)` for tests. ✅

**Done when:** script can `display` → `open` → `move` and viewer updates without per-frame `U S`.

**Tests:** `test/testgraphicsmove.morpho` (no viewer), `test/testviewmove.morpho` (live session).

### Phase 4 — Yardstick ✅

**Files:** `examples/amigaball.morpho`

Rewrite: define once on `Graphics`, pose on `display` (or `move` before `open`), physics via `g.move` — no per-frame `update(Graphics)`. No first-paint flash.

**Done when:** amigaball runs as a live Graphics session with define-once + move.

### Phase 5 — Follow-ons

One sub-phase at a time; pause for review before the next. Do not start 5b/5c viewer or Sphere emit work in the same pass as 5a.

Target after 5a+5b:

```
g.beginBatch()
g.move(ball, ...)
g.move(shadow, ...)
g.endBatch()            # one coalesced Moved
# View: pose draws only (no full D)
```

#### Phase 5a — `beginBatch` / `endBatch` ✅

**Files:** `share/modules/broadcast.morpho`, `share/modules/xgraphics.morpho`, `test/testgraphicsmove.morpho`; optionally wrap amigaball moves. View can stay on v1 (`D` + full `emitPoseDraws`) for this sub-phase.

**Locked:**

- `Broadcaster.beginBatch()` / `endBatch()` with a nesting counter (Scene inherits via mixin).
- Inside a batch: `notify` queues events; outermost `endBatch` delivers pending in order.
- Outside a batch: `notify` delivers immediately.
- Nested `beginBatch`/`endBatch` pairs supported; only the outermost `endBatch` flushes.
- Scene `display` / `move` / `recolor` always call `notify` — no Scene-specific batch state.
- **`BroadcastEvent` mixin** — coalescence protocol for batched notify. Defaults: `batchKey()` → `nil` (no coalesce), `coalesce(prev)` no-op. `GraphicsEvent` uses `with BroadcastEvent`; Moved / Recolored override:
  - non-nil `batchKey()` → replace any pending event with the same key (latest wins);
  - `coalesce(previous)` may merge payload into the survivor (Moved / Recolored accumulate `ids`).
- `GraphicsEventDefined` / `Removed` / `Replaced` keep default `batchKey` → each id is flushed separately.
- `respondsto("batchKey")` in Broadcaster is only a guard for non-protocol objects on the open queue; prefer real `BroadcastEvent` citizens.

**Done when:** test (and/or amigaball) can `beginBatch` → two `move`s → `endBatch` and a Capture listener sees **one** Moved; one viewer round-trip per frame when View is attached.

**Out of scope for 5a:** changing `D` + full pose redraw on the View side (done in 5b).

#### Phase 5b — Selective pose redraw (no full `D`) ✅

**Why:** After `D`, static draws vanish unless re-issued. True “movers only” needs in-place draw update.

**Files:** `src/command.c`, `src/scene.c` / `scene.h`, `share/modules/morphoview.morpho`, `share/modules/xgraphics.morpho` (`emitEntryPose`, `GraphicsEventMoved.ids`), `test/command/definedraw-pose-update`, `test/testdefinedraw.morpho`.

**Locked:**

- Viewer: when applying `d <id>` with a matrix, if an `OBJECT` draw for that id already exists in the scene displaylist, **replace its matrix** instead of appending. Objects first; text `T` draws later if cheap.
- View `receive(Moved)`: emit **only** pose lines via `Show.emitEntryPose` (no `C`/`M`); **do not** send `D`. Unmapped ids → fall back to full `D` + `emitPoseDraws`.
- Coalesced batch Moved carries `ids` (merged via event `coalesce`); View pose-updates every mover.
- Mid-session `Defined` unchanged (append define+draw).
- Keep `View.redraw(ascii)` and explicit `D` for tests/fixtures (`definedraw-redraw` stays valid).

**Done when:** `move` updates one object’s pose without clearing other draws; amigaball room is not re-sent each frame.

#### Phase 5c — Show emit review (primitives) + draw-slot identity ✅

**Goal:** entry SRT (and color) are per-instance draw authority; many spheres share few viewer objects.

**Substeps done:** 5c.1 draw-slots · 5c.2 unit Sphere + mesh cache · 5c.3 recolor · 5c.4 PointCloud/LineSet SRT · 5c.5 n-body (`examples/nbody.morpho`).

**Contract:** Graphics entry id = viewer draw-slot id (`d <drawId> [objectId]`); color stamped on slot from `C`; unit Sphere + Show mesh cache by refine level; `Graphics.recolor` + `GraphicsEventRecolored`. `Cylinder` / `Arrow` / `Text` remain world-baked here (Phases 5e / 5f).

#### Phase 5d — `U O` / `U V` / `X O` / `X D` ✅

Viewer command language + Graphics/View wiring:

| Command | Intent |
|---------|--------|
| `U O <id>` | Clear/redefine one object in the current scene |
| `X O <id>` | Delete one object (+ its draws) |
| `X D <drawId>` | Delete one draw-slot; leave the object |
| `U V <id>` | Same-length vertex replace + `glBufferSubData` where possible |

Graphics: `Scene.remove` / `replace` → `GraphicsEventRemoved` / `GraphicsEventReplaced`. View maps Removed → `X D` (+ `X O` if last user of the viewer object); Replaced → sphere-cache refresh when possible, else remove + re-`writeEntry`. Full `update(Graphics)` / `U S` unchanged. `U O` remains for low-level same-id refill.

**Done when:** command fixtures exist; Morpho smoke for redefine/delete. Not required for pose animation. Yardstick: [`examples/soapbubble.morpho`](../examples/soapbubble.morpho) (`U V` + refine `replace`).

#### Phase 5dx — Cleaning pass (before 5e)

**Why:** Holistic review after 5d found silent draw-slot / listener / `U V` traps that would poison Cylinder/Arrow work (5e) and make mixed scenes unreliable. Fix hygiene first; then 5e builds on a correct draw-slot contract.

**Source:** post-5d architecture review (simplicity / composability / clarity).

**Locked — must fix (bugs):**

1. **Draw-slot id for world-baked visits** — `visit(TriangleComplex)` (and PointCloud/LineSet identity draws that synthesize `GraphicsEntry(0,…)`) must emit `d <graphicsEntryId> <viewerObjectId>` when `_currentEntry` is set, not `d <viewerObjectId>`. Same for any baked path that reaches `emitEntryDraw` under an entry. Mixed Sphere + Cylinder scenes must not collide slots.
2. **`U V` / `refreshMesh` float layout** — emit vertex layout must match how the object was defined (`xn` vs `xnc`). Prefer matching both paths; if opaque `xnc` morph is deferred, `refreshMesh` / `emitEntryVertices` must return `false` (or throw) rather than report success while the viewer drops the batch.
3. **Pending ids vs multiple listeners** — ✅ `GraphicsEventMoved` / `Recolored` carry `ids`; Broadcaster coalesce calls optional `event.coalesce(previous)` so the surviving event merges payloads. No side-channel bag; every listener reads `ev.ids`.
4. **Moved fallback** — if no mapped ids, do **not** `D` + empty redraw (blank window). No-op or skip; rely on (1) so unmapped baked ids are rare.

**Locked — API polish (same pass if small; else follow immediately):**

5. **`display` kwargs** — `color=` and/or `flat=` on `display` (and Sphere overloads) so yardsticks stop `findEntry(id).color = …` / `.flat = true` before open.
6. **Named morph path** — public `Scene`/`View` name for same-length vertex push (e.g. keep `refreshMesh` but document; or `updateVertices(id)`) distinct from `replace` (full redefine). soapbubble should not need `findEntry.item =` as the only efficient path — either document that pairing or add `Scene.morph(id, item)` that sets item + notifies a dedicated event / calls through View.
7. **Failure convention** — `display` returns `false` (or keep `nil` but document) consistently with mutators; optional: document that Cylinder/Arrow/Text `move`/`remove` remain limited until 5e/5f.

**Out of scope for 5dx:** unit Cylinder/Arrow (5e); Text slots (5f); binary transport; COLOR-draw coalescing in the viewer (note only — unbounded `C` appends on live recolor); formal dependents (5g).

**Done when:**

- Test: Sphere + Cylinder (or other baked) in one Scene → both visible; `move`/`remove` on each id target the right slot.
- Test: opaque TriangleComplex `refreshMesh` either succeeds with matching layout or fails cleanly (no silent drop).
- Test: two listeners on one Scene batch-`move` see the same full id list.
- Yardsticks (amigaball / nbody / soapbubble) still pass; prefer updating them to use `color=`/`flat=` if (5) lands.

**Working agreement:** finish 5dx and pause for review before starting 5e.

#### Phase 5e — Shared Cylinder / Arrow models (entry SRT)

**Why:** Many cylinders/arrows each bake a full world-space mesh today (`visitGeneric`). Same pattern as spheres: few unit meshes, many draws posed by entry SRT.

**Locked (proposed):**

- Canonical **unit cylinder** along a fixed axis (e.g. +z, height 1, radius 1); Show mesh cache keyed by refine (and any material mode that affects geometry).
- `display(Cylinder)` / posed form: store a unit (or radius-normalized) abstract cylinder; encode **start→end** as entry `position` + `scale` (length and/or radius) + `rotate` (align axis). Color via `C` like spheres.
- **Arrow:** shared shaft mesh + shared tip mesh (two draws or one composite object — pick one approach in implementation); same start→end → SRT mapping. Degenerate zero-length still skipped.
- Mid-session `move` / `recolor` / `remove` use the existing draw-slot path (no per-frame rebake).
- Length or radius changes that cannot be expressed as uniform `scale` may need non-uniform scale later, or `replace` for now — prefer SRT-only in the first cut if possible (e.g. scale.z = length, scale.xy = radius).

**Out of scope for 5e:** Text; binary transport; general Tube.

**Done when:** many cylinders (and arrows) share one or two viewer `o`s; `g.move` updates pose without resending `v`/`f`; tests cover define-once + move + remove.

#### Phase 5f — Text draw-slots (move / remove / update)

**Why:** `visit(Text)` bakes `item.posn` / `dirn` / `vertical` into `t`/`m`/`T` and does not `_recordObject`, so `move` / `remove` do not drive Text like mesh draw-slots.

**Locked (proposed):**

- Graphics entry id ↔ viewer text draw-slot (or equivalent id tracked in `objectMap` / a parallel text map).
- Store unit/default text pose on the entry: `position` from `posn` (and optionally orientation from `dirn`/`vertical` → entry `rotate` or a retained `m`); `Show` emits `T` with entry transform, not only baked `item.posn`.
- `g.move(id, …)` → in-place pose update for that text slot (viewer: replace text draw matrix, analogous to mesh `d`).
- `g.remove(id)` → delete that text draw (`X D` or text-specific delete if required).
- String / font / size change → `replace` (remove + redefine) in the first cut; optional later in-place `T` content update if cheap.
- Recolor: stamp color on the text draw if the viewer path allows; else redefine.

**Out of scope for 5f:** Cylinder/Arrow (5e); rich text layout.

**Done when:** live session can `display(Text)` → `move` → `remove` without full `U S`; smoke test + small example or fixture.

#### Phase 5g — Formal Morpho dependents (backlog only)

Non-goal for this series. Learn from Graphics→View; formalize a general Morpho dependent-object framework later.
