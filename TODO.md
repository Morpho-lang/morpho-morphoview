# MorphoView TODO

Future work for this package. Command language: [`docs/commandapi.md`](docs/commandapi.md).

## Occasional update vs efficient animation

`Graphics` is a static, id-bearing displaylist (not a full scene graph). `Scene is Graphics` adds Broadcaster + `move` / `recolor` for live sessions.

| Use case | Supported path | Expectation |
|----------|----------------|-------------|
| Occasional refresh | `View.open(Graphics)` / `update(Graphics)` → `U S` + full reserialize | Intentional. Fine for “recompute viz every N steps / on demand.” |
| Efficient animation | `display` once → `View(Scene)` → `g.move` | Do **not** expect `update(Graphics)` to be cheap for large static+dynamic scenes. |

Do **not** make `update(Graphics)` automatically incremental. Keep `U S` as the high-level snapshot path; efficiency comes from Graphics mutations → listener → targeted viewer ops.

## Backlog

### Upstream (morpho)

- [x] Push graphics to morpho (`graphics`)
- [x] Push fonts to morpho (`fonts`); Show/View stay in this package
- [x] Push color to morpho (`Color` alpha, `Coloring`, `ColorTable`, ColorMap ≠ Color)
- [x] Push xplot to morpho `plot`
- [x] Keep core `graphics.morpho` free of `meshtools` (UV sphere in graphics; examples like soapbubble may still `import meshtools`)
- [x] `String.join(list, sep)` — View already joins `cmdLines` in one call
- [ ] Move `meshtools` (and heavy mesh pipeline) to an extension in morpho

### This package

- [ ] Improve and consolidate error reporting beyond parse errors — `command_apply` (and similar) still mix prefixed `morphoview:` messages, generic `return false`, and unprefixed lines like `Font id … not found`. Funnel those through one diagnostic path; CLI → stderr, live session → `err …` on ZMQ (`ok` is already sent before apply).
- [ ] Binary vertex transport — viewer IR already takes float/index blobs; Morpho still needs a way to serialize those buffers and send them over ZMQ. Biggest win on fat `v` / `U V` paths; redraw often avoids blobs entirely.
- [x] Transparent centroid cache — recompute object AABB centroid only when geometry changes, not every frame
- [x] Draw-list hygiene — merge adjacent draws that share VAO/material when packing the renderlist (matters at larger object counts)

### Events (viewer → Morpho)

Returned on the ZMQ PAIR and consumed by `View.poll`:

| Message | Status | Intent |
|---------|--------|--------|
| `ok` / `err …` | Done | Parse result for a command chunk |
| `window.closed` | Done | Last display closed |
| `pick …` | Later | Selection / hit info |
| `view <16 floats>` | Later | Camera / view matrix |
| `click …` | Later | Mouse click (gated / on request to avoid flood) |

## Done in this package

- [x] Live Graphics/Scene/View through Text draw-slots: stable ids, define vs draw, Broadcaster listeners, selective pose redraw, unit Sphere/Cylinder/Arrow mesh cache, `U O` / `U V` / `X O` / `X D`, materials/lighting/opacity, framing/auto-fit, ZMQ `View` session API, yardsticks (`boing`, `nbody`, `soapbubble`, `vectors`, `flyingtext`)
- [x] Graphics compactification: `GraphicsEntry.effectiveColor()`, primitive color never nil, uniform color via draw-slot `C`, `_ViewerSlot` serializer bookkeeping, `TriangleComplex.faceIndices`; fonts/Show split out of graphics
- [x] Local UV-sphere tessellation (no meshtools in graphics)
- [x] `color`: `Color(r,g,b)` / `Color(r,g,b,a)`, `Coloring`, `ColorTable` RGB/RGBA, ColorMap ≠ Color, Show via Coloring MD
- [x] `plot` (was xplot) Phases 1–5: `Plot is Scene`, bulk primitives, axes/`ScaleBar`/`ScaleBarStrip`, normalize/`center=`, live `axes`/`colormap`/`range`/`center`/`refresh`
- [x] Per-vertex alpha: format letter `a`; Show emits `xnca`/`xca` for RGBA ColorTables. Uniform `Color.a` still uses `C`. Transparent sort remains object-centroid.

## Compatibility shims

Filter/transmit, plot veneers, POVRaytracer camera fields, `ColorTable.column`, and ScaleBar `getfontsize` / `drawbar` / `drawlabel` now live in morpho (`graphics` / `plot` / `color` / `povray`). Remaining in this package:

- PATH fallback in `findMorphoViewBin` (bare `morphoview`)
