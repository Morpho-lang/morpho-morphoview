# MorphoView TODO

Near-term and later work. Command-language details also live in [`docs/commandapi.md`](docs/commandapi.md); this file is the checklist.

Design / impl for live Graphics: [`docs/definedraw.md`](docs/definedraw.md), [`docs/plan-graphics-view-listener.md`](docs/plan-graphics-view-listener.md).

## Priority sequence

Work in this order. Later tracks depend on decisions from earlier ones.

| # | Track | Status | Why now |
|---|--------|--------|---------|
| **1** | **Graphics prototype** (package `Show` / local Graphics model → upstream) | **Done** (package prototype) | Stable ids, entries, define vs draw, listener. Remaining: push settled pieces to morpho `graphics.morpho`. |
| **2** | **Animation-friendly viewer + View API** | **Phase 5e done; 5f next** | Through 5e Cylinder/Arrow. Next: Text draw-slots (5f). |
| **3** | **Binary / byte-buffer transport** | Later | Viewer can accept blobs sooner; real gain needs Morpho-side serialize + framing. Biggest on fat `v` / `U V` paths; redraw often avoids blobs entirely |

Tried and deferred: making `View.update` async / drop-under-pressure. The old bottleneck was full `U S` reserialize; the live path is now `Scene.move` → listener → redraw (no per-frame `U S`).

## Occasional update vs efficient animation

`Graphics` is a static, id-bearing displaylist (not a full scene graph). `Scene is Graphics` adds Broadcaster + `move` / `recolor` for live sessions. `Graphics.display` returns stable Graphics-owned ids; `Show` maps them to viewer `o` ids for a session. Entry SRT owns presentation pose; View listens for Scene mutations.

| Use case | Supported path | Expectation |
|----------|----------------|-------------|
| Occasional refresh | `View.open(Graphics)` / `update(Graphics)` → `U S` + full reserialize | Intentional. Fine for “recompute viz every N steps / on demand.” |
| Efficient animation | `display` once → `View(Scene)` → `g.move` (Phases 1–4); Phase 5 tightens batching / selective redraw | Do **not** expect `update(Graphics)` to be cheap for large static+dynamic scenes. |

Do **not** make `update(Graphics)` automatically incremental or diff the previous displaylist in an ad-hoc way. Keep `U S` as the high-level snapshot path; efficiency comes from Graphics mutations → listener → targeted viewer ops.

### 1. Graphics prototype — done (package)

Local prototype in [`xgraphics.morpho`](share/modules/xgraphics.morpho) (`Graphics` / `Scene` / `Show` / events) + [`broadcast.morpho`](share/modules/broadcast.morpho):

- [x] Stable Graphics-owned ids on `display` (not ephemeral per write for the model)
- [x] Distinguish **define** (`o` / `v` / `f`) vs **draw** (`d` + transforms) via entries + `Show`
- [x] Mid-session `move` + Broadcaster / Listener → View
- [x] Dedup / instance identical primitives (Show-level unit-sphere cache — Phase 5c)
- [x] `Scene is Graphics` — static Graphics container; live Scene subclass (Broadcaster + move/recolor)
- [ ] Push settled pieces to morpho `graphics.morpho`

### 2. Animation / composition infra — Phase 5e done; 5f next

Done (Phases 2–5e):

1. [x] Persistent apply context across `command_process` batches (sticky scene)
2. [x] `D` + display/move — re-issue draws for already-defined objects without resending `v`/`f` (v1: full `D` + all poses)
3. [x] Morpho `View` helpers: `open(g)` / `update(g)` / `redraw` / listener `receive` for Defined/Moved
4. [x] Yardstick: [`examples/boing.morpho`](examples/boing.morpho) — define-once + `g.move` (not full `U S` each frame)

Remaining ([Phase 5](docs/plan-graphics-view-listener.md)):

1. [x] **5a** — `beginBatch` / `endBatch` on Broadcaster (one Moved notify per frame)
2. [x] **5b** — Selective pose redraw (in-place `d` matrix update; no full `D` every move)
3. [x] **5c** — Draw-slot identity; unit Sphere + mesh cache + `C`/recolor; PointCloud/LineSet SRT; n-body yardstick
4. [x] **5d** — `U O <id>` / `X O <id>` / `X D <drawId>` / `U V <id>` + Graphics remove/replace events; soapbubble yardstick
5. [x] **5dx** — Cleaning pass: draw-slot id; `U V` layout; coalesce ids; Moved fallback; `display` `color=`/`flat=`; `morph` vs `replace`; failure convention
6. [x] **5e** — Shared unit Cylinder / Arrow + entry SRT (many draws → few `o`; `move` without rebake)
7. [ ] **5f** — Text draw-slots: `move` / `remove` / content via `replace`
8. [ ] **5g** — Formal Morpho dependents (backlog only)

### 3. Binary transport (later)

Viewer IR already accepts float/index blobs; Morpho needs a binary serialize path and ZMQ framing. Prioritize after Phase 5 animation ops — binary speeds bulk `v` traffic; it does not replace define-once / redraw.

## View session API (Morpho)

[`share/modules/morphoview.morpho`](share/modules/morphoview.morpho) — duplex session (Morpho has no async runtime; scripts drive `poll` / `System.sleep`):

- [x] `init` — defaults only (no spawn)
- [x] `open(commands)` — bind, spawn, send, wait for `ok`
- [x] `open(Graphics)` — serialize via `Show`, then open and listen
- [x] `View(Graphics)` — open immediately; throws on failure
- [x] `update(commands)` — send another chunk, wait for `ok`
- [x] `update(Graphics)` — serialize with `U S` replace, then update (occasional refresh; see above)
- [x] `redraw(commands)` — `D` + redraw draws (tests / escape hatch)
- [x] `write(line)` — File-compatible sink for `Show.write(g, out)`
- [x] Listener `receive` for Defined / Moved / Recolored / Removed / Replaced
- [x] `poll(timeoutms)` — non-blocking / short wait; return event or `nil`
- [x] `wait(sessiontimeout=0)` — convenience loop until closed / timeout
- [x] `close()` — idempotent cleanup
- [x] Private helpers prefixed with `_`
- [x] `beginBatch` / `endBatch` via Broadcaster mixin (Phase 5a)
- [x] Phase 5b — selective pose redraw (in-place `d`; `emitEntryPose`)
- [x] Phase 5c — draw-slot identity; unit Sphere + cache + recolor; n-body example
- [x] Phase 5d — `U O` / `U V` / `X O` / `X D`; `Scene.remove` / `replace`
- [x] Phase 5dx — cleaning pass (draw-slot id, `U V` layout, multi-listener, API polish)
- [x] Phase 5e — shared Cylinder / Arrow + entry SRT
- [ ] Phase 5f — Text draw-slots (`move` / `remove`; content via `replace`)

### Show / Graphics prototype (upstream candidate) — track 1 done

`Show` lives in [`xgraphics.morpho`](share/modules/xgraphics.morpho):

- [x] `Show()` / `Show(g)` — two inits via multiple dispatch; fire-and-forget still `-t`
- [x] `write(g, out)` — any `out.write(line)` delegate (File, `View`, …)
- [x] `replace` / `sceneId` — preamble emits `U S` vs `S` for live updates
- [x] `GraphicsEntry` + `display` / `move`; Show walks entries
- [x] PointCloud / LineSet
- [x] Broadcaster events (`GraphicsEventDefined` / `Moved` / `Recolored` / `Removed` / `Replaced`)
- [x] Phase 5c — draw-slots; unit Sphere + entry SRT; mesh cache; `recolor`; PointCloud/LineSet SRT; n-body
- [x] Phase 5d — `remove` / `replace`; Show `emitEntryRemove` / `emitEntryReplace`
- [x] Phase 5dx — cleaning pass (baked draw-slot id; `U V` layout; pending ids; display color/flat)
- [x] Phase 5e — unit Cylinder / Arrow + entry SRT (mesh cache)
- [ ] Phase 5f — Text entry pose + map for move/remove
- [ ] Push settled pieces to morpho `graphics.morpho`

## Framing / camera

- [x] Scene AABB: auto-compute from drawn geometry **and text glyph extents**, or explicit `B xmin xmax ymin ymax zmin zmax`
- [x] Auto-fit on first `PREPARE` (and after `B`) unless the user moved the camera; Tab restores fitted home view
- [x] Default light / viewPos placed outside scene AABB (unless an explicit light is set later)
- [x] Explicit light command: `L <x y z [r g b]>` / `L a` (auto)
- [x] Background color: `G <r g b>` (sampled each frame; `Show` emits `Graphics.background`)
- [x] Material model: `M shaded|flat` + OpenGL/VTK Phong (\(k_a,k_d,k_s,n\)); uniform `C` for meshes
- [x] RGBA / opacity (blend + opaque/transparent passes)
- [x] Phase 2c: consolidate geometry shaders (one program + `uFlat`, cached uniforms, CPU normal matrix)
- [x] Phase 2c: transparent depth sort (object centroid, far→near)
- [x] Graphics `transmit`/`filter` → View uniform alpha (POVRay-style; Color API later)
- [x] Sticky apply context + `D` + display/move (Phases 2–3)
- [x] Object update/delete (`U O` / `X O` / `X D`) — Phase 5d
- [x] `U V` vertex replace — Phase 5d
- [ ] Binary / byte-buffer vertex transport — **track 3**
- [ ] Pick / view / click events

## Command language extensions

### Replace / update

| Command | Status | Intent |
|---------|--------|--------|
| `U S <id>` | Done | Clear scene `id` in place (keep window); select as current; following `o`/`v`/`l`/`d` refill |
| `D` | Done | Clear displaylist only; keep objects / colors / fonts / pools |
| Draw-slot pose/color | Done (Phase 5c) | Per-instance matrix + albedo (many draws → one `o`) |
| `U O <id>` | Done (Phase 5d) | Clear/redefine one object in the current scene |
| `U V <objid>` | Done (Phase 5d) | Replace vertex blob only (same length; `glBufferSubData` when prepared) |

`S` stays find-or-create/select. Replace is always explicit via `U`, so re-selecting a scene cannot wipe it by accident. Full-scene `U S` remains the supported high-level path for occasional `update(Graphics)` snapshots.

### Bounds

| Command | Status | Intent |
|---------|--------|--------|
| `B …` | Done | Explicit scene AABB; prepare refits camera if user has not moved the view |

### Lighting

| Command | Status | Intent |
|---------|--------|--------|
| `L <x> <y> <z> [r g b]` | Done | Explicit model-space light (optional color); sets `light_explicit` |
| `L a` | Done | Clear explicit light; resume AABB auto placement |

### Background

| Command | Status | Intent |
|---------|--------|--------|
| `G <r> <g> <b>` | Done | Scene clear color; sampled each frame (no GL rebuild). Default dark gray; `Show` emits from `Graphics.background` |

### Materials

| Command | Status | Intent |
|---------|--------|--------|
| `M shaded [ka kd [ks [n]]]` | Done | OpenGL/VTK Phong; defaults ka=kd=0.5, ks=0 (Lambert) |
| `M flat` | Done | Unlit albedo |
| `C` on meshes | Done | Uniform albedo for subsequent geometry |
| RGBA / opacity | Done | `c … a`, blend, opaque then transparent pass |
| Graphics transmit/filter | Done | Package `Show`: `alpha = 1 - clamp(transmit+filter)`; uniform `C` + `v "xn"` |
| Shader consolidate | Done | Phase 2c: one geometry program + `uFlat`; cached uniforms; CPU normal matrix |
| Transparent depth sort | Done | Phase 2c: far→near by object centroid (intersecting translucents can still artifact) |

### Delete / quit

| Command | Status | Intent |
|---------|--------|--------|
| `X O <id>` | Done (Phase 5d) | Delete object from current scene (+ its draws) |
| `X D <drawId>` | Done (Phase 5d) | Delete one draw-slot; leave the object |
| `X S <id>` | Done | Close scene / window |
| `Q` | Done | Quit viewer cleanly (prefer over Morpho-side `pkill`) |

### Events (viewer → Morpho)

Returned on the ZMQ PAIR and consumed by `View.poll`:

| Message | Status | Intent |
|---------|--------|--------|
| `ok` / `err …` | Done | Parse result for a command chunk |
| `window.closed` | Done | Last display closed |
| `pick …` | Later | Selection / hit info |
| `view <16 floats>` | Later | Camera / view matrix |
| `click …` | Later | Mouse click (gated / on request to avoid flood) |

### Performance / bulk data

Small viewer-only polish can land anytime; strategic sequence is Graphics (done) → Phase 5 animation polish → binary (see **Priority sequence**).

**Done (package-only):**
- [x] ASCII float emission at 3 sfs (`Show.fmt` / `%0.3g`) — smaller strings, faster Morpho concat + C parse

**Viewer-only polish** (no Morpho cooperation required; opportunistic):

- [x] Changed-only prepare — `display_prepareall` uploads only scenes marked changed by the batch (skips untouched displays; light/title-only chunks need no rebuild)
- [x] Parse alloc — count numbers ahead, one `malloc`, parse into the command blob (no grow-by-1 varray + second copy for `v`/`f`/`c`)
- [x] Fewer CPU copies on the ASCII path — `scene_adddata_take` / `scene_addindex_take` adopt parse buffers into the scene pool when empty (no memcpy); append+free otherwise
- [ ] Transparent centroid cache — recompute object AABB centroid only when geometry changes, not every frame
- [ ] Draw-list hygiene — merge adjacent draws that share VAO/material when packing the renderlist (matters at larger object counts)

**Strategic projects** (ordered):

- [x] **Track 1** — Graphics prototype (stable ids; define vs draw; listener) — package done; upstream push still open
- [x] **Track 2** — Phase 5 through 5e; **5f** Text next
- [ ] **Track 3** — Binary / byte-buffer vertex transport (viewer IR already accepts blobs; Morpho needs serialize + ZMQ framing)

## Demos / tests

- [x] High-level `View` smoke: [`test/testview.morpho`](test/testview.morpho) (`open` + `wait`)
- [x] Define/draw fixture via `View`: [`test/testdefinedraw.morpho`](test/testdefinedraw.morpho) (`command/definedraw-once`)
- [x] Animation demo: [`test/testviewanim.morpho`](test/testviewanim.morpho) (`U S` + `update` / `poll`)
- [x] Graphics → View: [`test/testviewgraphics.morpho`](test/testviewgraphics.morpho) (`open`/`update` + `Show`)
- [x] Quit via `Q`: [`test/testquit.morpho`](test/testquit.morpho) (`close` → `window.closed`)
- [x] Low-level transport: [`test/testzmq.morpho`](test/testzmq.morpho)
- [x] `U S` replace: [`test/testupdate.morpho`](test/testupdate.morpho)
- [x] Large geometry auto-fit: [`test/command/largebbox`](test/command/largebbox)
- [x] Large tetrahedron via Graphics/`View`: [`test/testviewlarge.morpho`](test/testviewlarge.morpho)
- [x] Flat material: [`test/command/flatshade`](test/command/flatshade)
- [x] Uniform Phong: [`test/command/uniformphong`](test/command/uniformphong)
- [x] Material comparison (flat / Lambert / Phong): [`test/command/materials`](test/command/materials)
- [x] Phong torus: [`test/command/torus`](test/command/torus)
- [x] Opacity (RGBA blend): [`test/command/opacity`](test/command/opacity)
- [x] Explicit light (auto vs `L`): [`test/command/light`](test/command/light)
- [x] Transparent depth sort: [`test/command/depthsort`](test/command/depthsort) (near listed before far; sort still composites near on top)
- [x] Transparent spheres: [`test/command/transparentspheres`](test/command/transparentspheres) (overlapping Phong spheres + depth sort)
- [x] Graphics `transmit`/`filter` → RGBA: [`test/testshowtransmit.morpho`](test/testshowtransmit.morpho)
- [x] Translucent Graphics spheres: [`test/testviewtransmit.morpho`](test/testviewtransmit.morpho)
- [x] Graphics entries / `add` remap: [`test/testgraphicsentries.morpho`](test/testgraphicsentries.morpho)
- [x] `move` / listeners / objectMap: [`test/testgraphicsmove.morpho`](test/testgraphicsmove.morpho)
- [x] Live `open` → `move`: [`test/testviewmove.morpho`](test/testviewmove.morpho)
- [x] Live Scene yardstick (define-once + `g.move`): [`examples/boing.morpho`](examples/boing.morpho)
- [x] Phase 5c n-body yardstick (many shared `Sphere`s + `g.move` / `recolor`): [`examples/nbody.morpho`](examples/nbody.morpho)
- [x] Phase 5d command fixtures: update-object / update-vertices / delete-object / delete-draw (via [`test/testdefinedraw.morpho`](test/testdefinedraw.morpho))
- [x] Phase 5d live remove/replace: [`test/testviewremove.morpho`](test/testviewremove.morpho)
- [x] Deformable mesh yardstick (soap bubble / `U V` + refine `replace`): [`examples/soapbubble.morpho`](examples/soapbubble.morpho)
- [x] Phase 5e Cylinder/Arrow mesh cache + SRT: [`test/testcylinderarrow.morpho`](test/testcylinderarrow.morpho)
- [x] Phase 5e live cylinder move: [`test/testviewcylinder.morpho`](test/testviewcylinder.morpho)
- [x] Phase 5e vector-field yardstick (shared Cylinder/Arrow + `g.move`): [`examples/vectors.morpho`](examples/vectors.morpho)

## Notes

- Sticky `command_applyctx` persists across `command_process` batches, so follow-up chunks (redraw / `U O` / `U V` / `X O` / `X D`) may omit a leading `S` / `U S`.
- Package `Show` prototypes the upstream split: fire-and-forget (`Show(g)` / `-t`) vs serialize-to-delegate (`Show().write(g, out)`). `View` is the live duplex path and a `write` sink.
- ASCII float emission uses 3 significant figures (`Show.fmt` / `%0.3g`) to keep the string path smaller until binary vertex transport (track 3) exists.
- Phase 5a–5e done. Next: **5f** Text.
