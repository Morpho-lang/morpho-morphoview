# Plan: Graphics live model + View listener

Design source of truth: [`definedraw.md`](definedraw.md).

**Working agreement:** one phase at a time; pause for review before the next.

**Canonical View API:** `View(g)` or `View()` + `open(g)` (both listen after one `Show.write`).

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
- Pose args set on `display` → entry SRT recorded; `Sphere` stored as abstract primitive with matching `center`/`r` (Show bakes via `visitGeneric`, like Cylinder/Arrow).
- No pose args + `Sphere` → store the Sphere; copy `center`→entry position, `r`→scale.
- No pose args + other primitives → identity SRT; geometry as authored.
- Same item `display`’d twice → two ids / two entries.

### `move` merge (Phase 3; lock now)

- New `position` (2nd positional or kwarg) **always** sets `entry.position`.
- Omitted `scale` / `rotate` leave that entry component **unchanged**.

### Translucent `Sphere` (Phase 1)

Graphics stores the abstract `Sphere`; `Show` visits via `visitGeneric` → `totrianglecomplex()` (same pattern as Cylinder/Arrow). No Show-level sphere mesh cache — prefer clients `display`ing one unit item at many poses; a serializer cache can return later if needed. Entry SRT is recorded for later `move`; applying entry pose in Show (instead of baking `center`/`r`) is part of the later primitive emit review.

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

- **Id map:** View session owns `graphicsId → viewerObjectId` (on the live `Show` instance: `objectMap`, plus `posedIds` / `entryColorId`). `open(g)` builds it from one `Show.write` — no fake N `defined` events.
- **Mid-session `display`:** notifies `GraphicsEventDefined`; View emits define+draw via `Show.writeEntry`.
- **Events:** `GraphicsEventDefined`, `GraphicsEventMoved` (`removed` / `replaced` later).
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

- `Broadcaster.beginBatch()` / `endBatch()` with a nesting counter (Graphics inherits via mixin).
- Inside a batch: `notify` queues events; outermost `endBatch` delivers pending in order.
- Events that implement `batchKey()` coalesce within a batch (latest with that key wins). `GraphicsEventMoved` coalesces; `GraphicsEventDefined` does not (each id flushed).
- Outside a batch: `notify` delivers immediately.
- Nested `beginBatch`/`endBatch` pairs supported; only the outermost `endBatch` flushes.
- Graphics `display` / `move` always call `notify` — no Graphics-specific batch state.

**Done when:** test (and/or amigaball) can `beginBatch` → two `move`s → `endBatch` and a Capture listener sees **one** Moved; one viewer round-trip per frame when View is attached.

**Out of scope for 5a:** changing `D` + full pose redraw on the View side (done in 5b).

#### Phase 5b — Selective pose redraw (no full `D`) ✅

**Why:** After `D`, static draws vanish unless re-issued. True “movers only” needs in-place draw update.

**Files:** `src/command.c`, `src/scene.c` / `scene.h`, `share/modules/morphoview.morpho`, `share/modules/xgraphics.morpho` (`emitEntryPose`, `takeMovedIds`), `test/command/definedraw-pose-update`, `test/testdefinedraw.morpho`.

**Locked:**

- Viewer: when applying `d <id>` with a matrix, if an `OBJECT` draw for that id already exists in the scene displaylist, **replace its matrix** instead of appending. Objects first; text `T` draws later if cheap.
- View `receive(Moved)`: emit **only** pose lines via `Show.emitEntryPose` (no `C`/`M`); **do not** send `D`. Unmapped ids → fall back to full `D` + `emitPoseDraws`.
- Graphics tracks `_pendingMoved`; `takeMovedIds()` returns ids (displaylist order) and clears — coalesced batch Moved updates every mover.
- Mid-session `Defined` unchanged (append define+draw).
- Keep `View.redraw(ascii)` and explicit `D` for tests/fixtures (`definedraw-redraw` stays valid).

**Done when:** `move` updates one object’s pose without clearing other draws; amigaball room is not re-sent each frame.

#### Phase 5c — Show emit review (primitives)

**Goal:** entry SRT is the draw authority for more than `TriangleComplex`.

**Locked first cut:**

- **Sphere:** define a **unit** mesh (per distinct tessellation/transmit key, or first display of that abstract sphere); draw with entry SRT — stop baking center/r into vertices for the posed path (Phase 1 bake-vs-entry-SRT follow-through).
- **PointCloud / LineSet:** confirm `visitEntry` uses entry SRT; add overload if still world-baked + identity.
- **Cylinder / Arrow / Text:** still world-baked for this cut (orientation/path baked); document; no fake SRT.
- Optional Show-level unit-sphere mesh cache only if a single `Show.write` with many identical spheres is hot — secondary to clients `display`ing one unit item at many poses.

**Done when:** posed `Sphere` via `display`/`move` animates without rebuilding sphere mesh; tests cover Sphere `objectMap` + pose redraw.

#### Phase 5d — `U O` / `U V` / `X O`

Viewer command language + minimal Graphics/View wiring:

| Command | Intent |
|---------|--------|
| `U O <id>` | Clear/redefine one object in the current scene |
| `X O <id>` | Delete one object (+ its draws) |
| `U V <id>` | Same-length vertex replace + `glBufferSubData` where possible |

Graphics: `removed` / `replaced` events (deferred from Phase 3). View maps events → commands. Full `update(Graphics)` / `U S` unchanged.

**Done when:** command fixtures exist; optional Morpho smoke for redefine/delete. Not required for pose animation.

#### Phase 5e — Formal Morpho dependents (backlog only)

Non-goal for this series. Learn from Graphics→View; formalize a general Morpho dependent-object framework later.
