# MorphoView TODO

Near-term and later work. Command-language details also live in [`docs/commandapi.md`](docs/commandapi.md); this file is the checklist.

## Priority sequence

Work in this order. Later tracks depend on decisions from earlier ones.

| # | Track | Why now |
|---|--------|---------|
| **1** | **Graphics prototype** (package `Show` / local Graphics model → upstream) | Unlocks stable ids, define vs display, instancing; spells out which viewer animation ops Morpho actually needs |
| **2** | **Animation-friendly viewer + View API** | Sticky context, display/move, then `U O` / `U V` / `X O` — guided by the Graphics model, not guessed ahead of it |
| **3** | **Binary / byte-buffer transport** | Viewer can accept blobs sooner; real gain needs Morpho-side serialize + framing. Biggest on fat `v` / `U V` paths; display-move often avoids blobs entirely |

Tried and deferred: making `View.update` async / drop-under-pressure. Did not help [`examples/amigaball.morpho`](examples/amigaball.morpho) — bottleneck is full `U S` reserialize, not waiting on `ok`.

## Occasional update vs efficient animation

`Graphics` is a displaylist **container**, not a scene graph. Primitives have no stable viewer ids; `Show` invents ephemeral `o` ids via `uid()` each write. That matches fire-and-forget `Show(g)` and **occasional** live refresh — not frame-rate animation (Graphics was never designed for that).

| Use case | Supported path | Expectation |
|----------|----------------|-------------|
| Occasional refresh | `View.open(Graphics)` / `update(Graphics)` → `U S` + full reserialize | Intentional. Fine for “recompute viz every N steps / on demand.” |
| Efficient animation | After Graphics + animation tracks: stable ids, static once + dynamic updates, display-move / `U O` / `U V` | Do **not** expect `update(Graphics)` to be cheap for large static+dynamic scenes. |

Do **not** make `update(Graphics)` automatically incremental or diff the previous displaylist in an ad-hoc way. Keep `U S` as the high-level snapshot path; efficiency comes from a deliberate Graphics model + targeted viewer ops.

### 1. Graphics prototype (next)

Local prototype in this package ([`xgraphics.morpho`](share/modules/xgraphics.morpho) `Show`); identify what to push to upstream `graphics.morpho`. Goals:

- Stable object ids (not ephemeral `uid()` per write)
- Distinguish **define mesh** (`o` / `v` / `f`) vs **place / display** (`d` + transforms)
- Dedup / instance identical primitives (generalize opaque-sphere instancing in `Show`)
- Clarify multi-Graphics composition (static once + dynamic subset) without changing the meaning of full `update(Graphics)`
- Produce a concrete feature list for track 2 (which View / command ops Morpho will call)

**Identical objects (regular viz, not just animation):** scripts often `display` many copies of the same geometry; today each typically expands to its own `o`/`v`/`f` payload. Instancing should be the normal path where possible.

### 2. Animation / composition infra (after Graphics sketch)

The command language already separates object definition from display. Order once Graphics clarifies the Morpho API:

1. Persistent apply context across `command_process` batches (sticky scene / object)
2. Display/move — re-issue `d` (+ `i` / `s` / `t` / `m`) for an already-defined object without resending `v`/`f` (likely highest leverage for demos like amigaball: floor once, move ball/shadow)
3. `U O <id>` / `X O <id>` — redefine or delete one object
4. `U V <id>` — same-length vertex replace + `glBufferSubData` where possible
5. Morpho `View` helpers for id-bearing / dynamic updates — without changing full `update(Graphics)`

### 3. Binary transport (later)

Viewer IR already accepts float/index blobs; Morpho needs a binary serialize path and ZMQ framing. Prioritize after animation ops are sketched — binary speeds bulk `v` traffic; it does not replace define-once / move-display.

Stress demo (full `U S` each frame on purpose): [`examples/amigaball.morpho`](examples/amigaball.morpho). End-to-end load test of serialize → ZMQ → parse → replace → GL. Yardstick for track 2: rewrite to open floor once, display-move / `U V` for ball and shadow.

## View session API (Morpho)

[`share/modules/morphoview.morpho`](share/modules/morphoview.morpho) — duplex session (Morpho has no async runtime; scripts drive `poll` / `System.sleep`):

- [x] `init` — defaults only (no spawn)
- [x] `open(commands)` — bind, spawn, send, wait for `ok`
- [x] `open(Graphics)` — serialize via `Show`, then open
- [x] `update(commands)` — send another chunk, wait for `ok`
- [x] `update(Graphics)` — serialize with `U S` replace, then update (occasional refresh; see above)
- [x] `write(line)` — File-compatible sink for `Show.write(g, out)`
- [x] `poll(timeoutMs)` — non-blocking / short wait; return event or `nil`
- [x] `wait(sessionTimeOut=0)` — convenience loop until closed / timeout
- [x] `close()` — idempotent cleanup
- [x] Private helpers prefixed with `_`
- [ ] Display/move API — re-issue `d` (+ transforms) for an already-defined object without resending `v`/`f` (**track 2**, after Graphics)

### Show / Graphics prototype (upstream candidate) — **track 1 (next)**

`Show` lives in [`xgraphics.morpho`](share/modules/xgraphics.morpho) (former package `XShow`):

- [x] `Show()` / `Show(g)` — two inits via multiple dispatch; fire-and-forget still `-t`
- [x] `write(g, out)` — any `out.write(line)` delegate (File, `View`, …)
- [x] `replace` / `sceneId` — preamble emits `U S` vs `S` for live updates
- [x] Merged into `xgraphics` as `Show` (Phase 1); `View` uses it
- [ ] Graphics prototype: stable ids; define mesh vs place/display; dedup identical primitives → instance `d`s
- [x] PointCloud / LineSet (Phase 2)
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
- [ ] Sticky apply context + display/move — **track 2**
- [ ] Object update/delete (`U O` / `X O`) — **track 2**
- [ ] `U V` vertex replace — **track 2**
- [ ] Binary / byte-buffer vertex transport — **track 3**
- [ ] Pick / view / click events

## Command language extensions

### Replace / update

| Command | Status | Intent |
|---------|--------|--------|
| `U S <id>` | Done | Clear scene `id` in place (keep window); select as current; following `o`/`v`/`l`/`d` refill |
| `U O <id>` | Track 2 | Clear/redefine one object in the current scene |
| `U V <objid>` | Track 2 | Replace vertex blob only (morph targets / pointwise edits) |

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
| `X O <id>` | Track 2 | Delete object from current scene |
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

Small viewer-only polish can land anytime; the strategic sequence is still Graphics → animation ops → binary (see **Priority sequence**).

**Done (package-only):**
- [x] ASCII float emission at 3 sfs (`Show.fmt` / `%0.3g`) — smaller strings, faster Morpho concat + C parse

**Viewer-only polish** (no Morpho cooperation required; opportunistic):

- [x] Changed-only prepare — `display_prepareall` uploads only scenes marked changed by the batch (skips untouched displays; light/title-only chunks need no rebuild)
- [x] Parse alloc — count numbers ahead, one `malloc`, parse into the command blob (no grow-by-1 varray + second copy for `v`/`f`/`c`)
- [x] Fewer CPU copies on the ASCII path — `scene_adddata_take` / `scene_addindex_take` adopt parse buffers into the scene pool when empty (no memcpy); append+free otherwise
- [ ] Transparent centroid cache — recompute object AABB centroid only when geometry changes, not every frame
- [ ] Draw-list hygiene — merge adjacent draws that share VAO/material when packing the renderlist (matters at larger object counts)

**Strategic projects** (ordered):

- [ ] **Track 1** — Graphics prototype (stable ids; define vs display; instancing)
- [ ] **Track 2** — Sticky apply context + display/move + `U O` / `U V` / `X O` + View helpers
- [ ] **Track 3** — Binary / byte-buffer vertex transport (viewer IR already accepts blobs; Morpho needs serialize + ZMQ framing)

## Demos / tests

- [x] High-level `View` smoke: [`test/testview.morpho`](test/testview.morpho) (`open` + `wait`)
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
- [x] Full-replace stress (not efficient animation): [`examples/amigaball.morpho`](examples/amigaball.morpho)

## Notes

- Each `command_process` batch currently starts with an empty apply context, so every chunk that draws must establish a scene (`S` or `U S`) before `o`/`v`/…. Sticky context (track 2) is required before display-move / `U O` / `U V` chunks can omit a leading `S` / `U S`.
- Package `Show` prototypes the upstream split: fire-and-forget (`Show(g)` / `-t`) vs serialize-to-delegate (`Show().write(g, out)`). `View` is the live duplex path and a `write` sink. Track 1 extends this prototype toward a Graphics model morphoview can grow into.
- ASCII float emission uses 3 significant figures (`Show.fmt` / `%0.3g`) to keep the string path smaller until binary vertex transport (track 3) exists.
