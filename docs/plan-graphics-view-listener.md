# Plan: Graphics live model + View listener

Design source of truth: [`definedraw.md`](definedraw.md).

**Working agreement:** one phase at a time; pause for review before the next.

**Canonical View API:** `View()` + `open(g)` (not `View(g)`).

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
- Phase 3 batching: immediate outside `begin`/`end`; queue inside; no nested `begin`; `display` in batch queued; prefer batching for multi-object frames.
- `open(g)`: one `Show.write`, then listen (no fake N `defined` on open).
- `update(g)`: full `U S`; rebind listener; drop pending batch; reset Show/object maps.
- Phase 3 v1: `D` + full pose redraw (known limit for large static+one mover).

## Target loop

Put the **first pose on `display`** (or `move` before `open`) so the first paint is not identity:

```
var ball = g.display(unitBall, [x0,y0,z0], scale=ballR)
var shadow = g.display(unitShadow, [x0,0.02,z0], scale=shadowR)
v = View()
v.open(g)   # first paint already posed
g.begin()
g.move(ball, [x,y,z], scale=ballR, rotate=[angle,0,1,0])
g.move(shadow, [x,0.02,z], scale=shadowR)
g.end()
```

## Non-goals

- General Morpho dependent-object framework (learn from this; formalize later)
- Auto-diff inside `update(Graphics)`
- Full scene graph / Mesh-registry API
- Binary transport; pick events
- `U O` / `U V` in the first cut (follow-on once redraw works)

## Phases

### Phase 1 — Entries + Show define/draw foundation

**Files:** `share/modules/xgraphics.morpho`, `test/`

**Steps:**

1. `Graphics.display(item, position=, scale=, rotate=)` allocates id, stores entry `{ id, item, position, scale, rotate }`, returns id. No `id=` on mesh primitives. `GraphicsEntry` coerces `position` to Matrix. Apply locked Sphere pose normalization.
2. `Show` walks **entries**; emit define/draw from **entry SRT only**.
3. Spheres: store abstract `Sphere`; Show `visitGeneric` (no cache). Entry pose recorded; bake-vs-entry-SRT emit polish later.
4. `Graphics.add`: remap right-hand entry ids (unique within one Graphics).
5. Keep `replace=true` → full `U S`.
6. Tests: distinct ids; same primitive twice → two ids; Sphere unit + entry SRT (posed / translucent / no-pose `Sphere(center,r)`).

**Done when:** `Show(g)` / `View.open(g)` emit correctly from entries; set-and-forget path unchanged in spirit; no C viewer changes.

### Phase 2 — Viewer redraw support

**Files:** `src/command.c` / `command.h`, `src/scene.c` / `scene.h`, `src/display.c`, docs, `share/modules/morphoview.morpho`

**Steps:**

1. Sticky `command_applyctx` across `command_process` batches (follow-up chunks may omit leading `S`).
2. Command `D` — clear displaylist only; mark scene changed; keep objects / colors / fonts / pools.
3. `display_prepareall`: `render_reset` before re-prepare after `D`.
4. Parse: `D` must **not** require parse-time `has_scene` (ok-before-apply race).
5. Docs + make `test/command/definedraw-redraw` runnable.
6. `View.redraw(commands)` — low-level escape hatch (`D\n` + chunk).

**Done when:** `definedraw-redraw` fixture runs; `View.redraw` can clear draws and re-issue poses without `U S`.

### Phase 3 — `move` + View listener

**Files:** `share/modules/xgraphics.morpho` (`Graphics`), `share/modules/morphoview.morpho` (`View`)

**Before coding, lock:**

- Who owns `graphicsId → viewerObjectId` (View session state vs Show); `open` builds map from one `Show.write` (no fake N `defined`).
- Mid-session `display` after `open` emits `defined` (define+draw); batched `display` queued.
- Minimal events: `defined`, `moved` (`removed` / `replaced` later).

**Steps:**

1. `move(id, position=, scale=, rotate=)` — locked merge rules; notify listeners.
2. `begin` / `end` — immediate outside batch; queue inside; no nested `begin`.
3. `addListener` / `removeListener` on Graphics (no global dependents framework).
4. `open(g)`: one `Show.write`, then listen; `update(g)`: full `U S`, rebind, drop pending batch, reset Show/object maps.
5. On batched `moved`: `D` + full pose redraw (v1; known limit for large static+one mover).
6. Keep `View.redraw(ascii)` for tests.

**Done when:** script can `display` → `open` → `begin`/`move`/`end` and viewer updates without per-frame `U S`.

### Phase 4 — Yardstick

**Files:** `examples/amigaball.morpho`

Rewrite: define once on `Graphics`, pose on `display` (or `move` before `open`), physics via `g.move` — no per-frame `update(Graphics)`. No first-paint flash.

**Done when:** amigaball runs as a live Graphics session with define-once + move.

### Phase 5 — Follow-ons

- `U O` / `U V` / `X O`
- Finer draw updates without full `D` every frame
- Formal Morpho dependents framework
- Review Show emit for all primitives (LOD / tessellation, Text pose, non-Sphere entry SRT, optional sphere mesh cache)
- Optional: Show-level unit-sphere mesh cache if a single `Show.write` with many identical static spheres is hot — secondary to clients `display`ing one unit item at many poses
