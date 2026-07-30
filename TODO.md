# MorphoView TODO

Near-term and later work. Command-language details also live in [`docs/commandapi.md`](docs/commandapi.md); this file is the checklist.

## View session API (Morpho)

[`share/modules/morphoview.morpho`](share/modules/morphoview.morpho) — duplex session (Morpho has no async runtime; scripts drive `poll` / `System.sleep`):

- [x] `init` — defaults only (no spawn)
- [x] `open(commands)` — bind, spawn, send, wait for `ok`
- [x] `open(Graphics)` — serialize via prototype `Show`, then open
- [x] `update(commands)` — send another chunk, wait for `ok`
- [x] `update(Graphics)` — serialize with `U S` replace, then update
- [x] `write(line)` — File-compatible sink for `Show.write(g, out)`
- [x] `poll(timeoutMs)` — non-blocking / short wait; return event or `nil`
- [x] `wait(sessionTimeOut=0)` — convenience loop until closed / timeout
- [x] `close()` — idempotent cleanup
- [x] Private helpers prefixed with `_`

### Show serializer prototype (upstream candidate)

Mild rewrite of graphics `Show`, living here until pushed back to morpho:

- [x] `Show()` / `Show(g)` — two inits via multiple dispatch; fire-and-forget still `-t`
- [x] `write(g, out)` — any `out.write(line)` delegate (File, `View`, …)
- [x] `replace` / `sceneId` — preamble emits `U S` vs `S` for live updates
- [ ] Push to morpho `graphics.morpho` once API feels right

## Framing / camera

- [x] Scene AABB: auto-compute from drawn geometry, or explicit `B xmin xmax ymin ymax zmin zmax`
- [x] Auto-fit on first `PREPARE` (and after `B`) unless the user moved the camera; Tab restores fitted home view
- [x] Default light / viewPos placed outside scene AABB (unless an explicit light is set later)
- [ ] Explicit light command: `L <x y z [r g b]>` / `L a` (auto)
- [x] Material model: `M shaded|flat` + OpenGL/VTK Phong (\(k_a,k_d,k_s,n\)); uniform `C` for meshes
- [ ] RGBA / opacity (blend + opaque/transparent passes)
- [ ] Object update/delete (`U O` / `X O`) + persistent apply context
- [ ] Binary / byte-buffer vertex transport
- [ ] Pick / view / click events

## Command language extensions

### Replace / update

| Command | Status | Intent |
|---------|--------|--------|
| `U S <id>` | Done | Clear scene `id` in place (keep window); select as current; following `o`/`v`/`l`/`d` refill |
| `U O <id>` | Later | Clear/redefine one object in the current scene |
| `U V <objid>` | Later | Replace vertex blob only (morph targets / pointwise edits) |

`S` stays find-or-create/select. Replace is always explicit via `U`, so re-selecting a scene cannot wipe it by accident.

### Bounds

| Command | Status | Intent |
|---------|--------|--------|
| `B …` | Done | Explicit scene AABB; prepare refits camera if user has not moved the view |

### Lighting

| Command | Status | Intent |
|---------|--------|--------|
| `L <x> <y> <z> [r g b]` | Later | Explicit model-space light (optional color); sets `light_explicit` |
| `L a` | Later | Clear explicit light; resume AABB auto placement |

### Materials

| Command | Status | Intent |
|---------|--------|--------|
| `M shaded [ka kd [ks [n]]]` | Done | OpenGL/VTK Phong; defaults ka=kd=0.5, ks=0 (Lambert) |
| `M flat` | Done | Unlit albedo |
| `C` on meshes | Done | Uniform albedo for subsequent geometry |
| RGBA / opacity | Later | Phase 2c |

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

## Notes

- Each `command_process` batch currently starts with an empty apply context, so every chunk that draws must establish a scene (`S` or `U S`) before `o`/`v`/….
- Package `Show` prototypes the upstream split: fire-and-forget (`Show(g)` / `-t`) vs serialize-to-delegate (`Show().write(g, out)`). `View` is the live duplex path and a `write` sink.
