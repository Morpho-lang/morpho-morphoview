# MorphoView TODO

Near-term and later work. Command-language details also live in [`docs/commandapi.md`](docs/commandapi.md); this file is the checklist.

## Occasional update vs efficient animation

`Graphics` is a displaylist **container**, not a scene graph. Primitives have no stable viewer ids; `Show`/`XShow` invent ephemeral `o` ids via `uid()` each write. That matches fire-and-forget `Show(g)` and **occasional** live refresh — not frame-rate animation (Graphics was never designed for that).

| Use case | Supported path | Expectation |
|----------|----------------|-------------|
| Occasional refresh | `View.open(Graphics)` / `update(Graphics)` → `U S` + full reserialize | Intentional. Fine for “recompute viz every N steps / on demand.” |
| Efficient animation | Later: stable object ids, multi-Graphics (static once + dynamic chunks), viewer `U O` / `U V` / sticky apply context | Do **not** expect `update(Graphics)` to be cheap for large static+dynamic scenes. |

Do **not** make `update(Graphics)` automatically incremental or diff the previous displaylist in an ad-hoc way. A **deliberate Graphics review** is likely soon (see below); until then keep `U S` as the high-level snapshot path.

**Animation / composition infra** (viewer first, then Morpho helpers):

1. Persistent apply context across `command_process` batches
2. `U O <id>` (redefine one object) + `X O <id>`
3. `U V <id>` (same-length vertex replace + `glBufferSubData` where possible)
4. Morpho helpers for **id-bearing** objects / a dynamic Graphics subset — without changing the meaning of full `update(Graphics)`

The command language already separates **object definition** (`o` / `v` / `f` / …) from **display** (`d`, plus `i` / `s` / `t` / `m`). A natural View API is to *move* or re-issue display/transform commands for an already-defined object (without resending vertex blobs). That may want a mirror in `Graphics` itself (stable ids on items; distinguish “define mesh” vs “place instance”).

**Identical objects (regular viz, not just animation):** scripts often `display` many copies of the same geometry. Today each typically expands to its own `o`/`v`/`f` payload. Instancing (one definition, many `d` + transforms) already exists for uncolored opaque spheres in `XShow`; a Graphics review should generalize that pattern so identical meshes are not re-sent as full geometry.

**Near-term design work:** detailed look at upstream `graphics.morpho` + package `XShow` — ids, define vs display, dedup/instance of identical primitives — coordinated with View APIs that emit display/move updates against the existing command language.

Stress demo (full `U S` each frame on purpose): [`examples/amigaball.morpho`](examples/amigaball.morpho). Good end-to-end load test of serialize → ZMQ → parse → replace → GL. Optional later rewrite: open floor once, `U V` / display-move for ball and shadow — after those viewer ops exist. Not a reason to force a half-baked Graphics redesign, but a useful yardstick once the model is clearer.

## View session API (Morpho)

[`share/modules/morphoview.morpho`](share/modules/morphoview.morpho) — duplex session (Morpho has no async runtime; scripts drive `poll` / `System.sleep`):

- [x] `init` — defaults only (no spawn)
- [x] `open(commands)` — bind, spawn, send, wait for `ok`
- [x] `open(Graphics)` — serialize via prototype `Show`, then open
- [x] `update(commands)` — send another chunk, wait for `ok`
- [x] `update(Graphics)` — serialize with `U S` replace, then update (occasional refresh; see above)
- [x] `write(line)` — File-compatible sink for `Show.write(g, out)`
- [x] `poll(timeoutMs)` — non-blocking / short wait; return event or `nil`
- [x] `wait(sessionTimeOut=0)` — convenience loop until closed / timeout
- [x] `close()` — idempotent cleanup
- [x] Private helpers prefixed with `_`
- [ ] Display/move API — re-issue `d` (+ transforms) for an already-defined object without resending `v`/`f` (pairs with Graphics define-vs-display review)

### Show serializer prototype (upstream candidate)

Mild rewrite of graphics `Show`, living here until pushed back to morpho:

- [x] `Show()` / `Show(g)` — two inits via multiple dispatch; fire-and-forget still `-t`
- [x] `write(g, out)` — any `out.write(line)` delegate (File, `View`, …)
- [x] `replace` / `sceneId` — preamble emits `U S` vs `S` for live updates
- [ ] Push to morpho `graphics.morpho` once API feels right
- [ ] Graphics review (soon): stable ids; define mesh vs place/display; dedup identical primitives → instance `d`s

## Framing / camera

- [x] Scene AABB: auto-compute from drawn geometry, or explicit `B xmin xmax ymin ymax zmin zmax`
- [x] Auto-fit on first `PREPARE` (and after `B`) unless the user moved the camera; Tab restores fitted home view
- [x] Default light / viewPos placed outside scene AABB (unless an explicit light is set later)
- [x] Explicit light command: `L <x y z [r g b]>` / `L a` (auto)
- [x] Material model: `M shaded|flat` + OpenGL/VTK Phong (\(k_a,k_d,k_s,n\)); uniform `C` for meshes
- [x] RGBA / opacity (blend + opaque/transparent passes)
- [x] Phase 2c: consolidate geometry shaders (one program + `uFlat`, cached uniforms, CPU normal matrix)
- [x] Phase 2c: transparent depth sort (object centroid, far→near)
- [x] Graphics `transmit`/`filter` → View uniform alpha (POVRay-style; Color API later)
- [ ] Object update/delete (`U O` / `X O`) + persistent apply context — **animation track** (order above)
- [ ] `U V` vertex replace — **animation track**
- [ ] Binary / byte-buffer vertex transport
- [ ] Pick / view / click events

## Command language extensions

### Replace / update

| Command | Status | Intent |
|---------|--------|--------|
| `U S <id>` | Done | Clear scene `id` in place (keep window); select as current; following `o`/`v`/`l`/`d` refill |
| `U O <id>` | Later | Clear/redefine one object in the current scene (animation track) |
| `U V <objid>` | Later | Replace vertex blob only (morph targets / pointwise edits; animation track) |

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
| `X O <id>` | Later | Delete object from current scene |
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

- [ ] Binary / byte-buffer transport for large vertex payloads (ASCII remains fine for control)
- [ ] Persistent apply context across `command_process` batches (needed for chunks that only do `U O` / `v` without a leading `S` / `U S`)

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

- Each `command_process` batch currently starts with an empty apply context, so every chunk that draws must establish a scene (`S` or `U S`) before `o`/`v`/…. Sticky context is required before `U O` / `U V` chunks can omit a leading `S` / `U S`.
- Package `Show` prototypes the upstream split: fire-and-forget (`Show(g)` / `-t`) vs serialize-to-delegate (`Show().write(g, out)`). `View` is the live duplex path and a `write` sink.
- ASCII float emission uses 3 significant figures (`XShow.fmt` / `%0.3g`) to keep the string path smaller until binary vertex transport exists.
