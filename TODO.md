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

### Upstream

- [ ] Push settled pieces from [`xgraphics.morpho`](share/modules/xgraphics.morpho) (`Graphics` / `Scene` / events) plus [`xshow.morpho`](share/modules/xshow.morpho) (`Show`) and [`xfonts.morpho`](share/modules/xfonts.morpho) to morpho `graphics.morpho` / `show` / `fonts`
- [ ] Keep core `graphics.morpho` free of `meshtools` (prototype: local sphere tessellation in xgraphics; examples like soapbubble may still `import meshtools`)
- [ ] Move `meshtools` (and heavy mesh pipeline) to an extension in morpho
- [ ] Extend morpho `color` module with alpha (`Color(r,g,b)` / `Color(r,g,b,a)` via MD, always store `a`; `rgba` / `alpha`); `Coloring` shared kind; ColorMap not a Color; wire `Show` via Coloring MD — **prototyped in package `xcolor` + `xgraphics`**
- [ ] `xcolor` backlog: `Normalizer` / `LinearNorm` / `LogNorm` + `ColorScale` (under/over/bad); `ListedMap` + `reverse` / `truncate` / `discretize`; perceptual / named scientific cyclic (`PhaseMap`); optional `HueMap` → `HSVMap` alias; bulk `colors(values)` if mesh coloring needs it
- [ ] Modernize morpho `plot` against xgraphics / xcolor — **prototyped in package `xplot`** (Phases 1–5 done: `Plot is Scene`, bulk primitives, axes/`ScaleBar`, normalize/`center=`, live `axes`/`colormap`/`range`/`center`/`refresh(view)`; ScaleBar is a meshtools-free tube, `ScaleBarStrip` for a flat bar). **Dogfood next**; `xnca` per-vertex alpha and optional `plotvectors` are future refinements.

### Transport

- [ ] Binary / byte-buffer vertex transport — viewer IR already accepts float/index blobs; Morpho needs a binary serialize path and ZMQ framing. Biggest win on fat `v` / `U V` paths; redraw often avoids blobs entirely.

### Events (viewer → Morpho)

Returned on the ZMQ PAIR and consumed by `View.poll`:

| Message | Status | Intent |
|---------|--------|--------|
| `ok` / `err …` | Done | Parse result for a command chunk |
| `window.closed` | Done | Last display closed |
| `pick …` | Later | Selection / hit info |
| `view <16 floats>` | Later | Camera / view matrix |
| `click …` | Later | Mouse click (gated / on request to avoid flood) |

### Viewer polish

- [ ] **Per-vertex alpha** — today `v "xnc"` is opaque RGB; translucency is uniform (`C` RGBA / `Color.a`), including as a draw-slot override on ColorTable geometry. Add a format such as `v "xnca"` so interpolated field plots can honor `Opacity(colormap, a)` / maps with varying alpha. Wire through Show (`TriangleComplex` / PointCloud / LineSet), `U V`, and transparent sort (still object-centroid until finer OIT). Motivated by `Plot(field, colormap=Opacity(ViridisMap(), …))`.
- [ ] Transparent centroid cache — recompute object AABB centroid only when geometry changes, not every frame
- [ ] Draw-list hygiene — merge adjacent draws that share VAO/material when packing the renderlist (matters at larger object counts)

### Language / framework

- [ ] Formal Morpho dependents (learn from Graphics→View; formalize a general dependent-object framework later)

## Completed in this rewrite

Live Graphics/Scene/View stack through Text draw-slots: stable ids, define vs draw, Broadcaster listeners, selective pose redraw, unit Sphere/Cylinder/Arrow mesh cache, `U O` / `U V` / `X O` / `X D`, materials/lighting/opacity, framing/auto-fit, ZMQ `View` session API, and yardsticks (`boing`, `nbody`, `soapbubble`, `vectors`, `flyingtext`).

Graphics compactification: `GraphicsEntry.effectiveColor()`, primitive color never nil, uniform morphoview color via draw-slot `C` (ColorTable stays `xnc`/`xc`), `_ViewerSlot` serializer bookkeeping, `TriangleComplex.faceIndices`, modules `xfonts` / `xshow` split out of `xgraphics`.

## Compatibility shims — remove after the migration window

Tagged `// [Compatibility shim]` in source. Delete the whole group in one pass when callers have moved:

- primitive `filter=` / `transmit=` constructor kwargs
- `_legacyAlpha` / `_legacyOpacity` / `withLegacyTransp` (and xplot veneer transparency paths)
- `plotmesh` / `plotselection` / `plotfield` legacy transparency
- `POVRaytracer` mirrored camera fields (`viewpoint`, `viewangle`, …)
- `ColorTable.column`
- selected lowercase compatibility aliases on Plot
- ScaleBar `getfontsize` / `drawbar` / `drawlabel`
- PATH fallback in `findMorphoViewBin` (bare `morphoview`)
