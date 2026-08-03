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

- [ ] Push settled pieces from [`xgraphics.morpho`](share/modules/xgraphics.morpho) (`Graphics` / `Scene` / `Show` / events) to morpho `graphics.morpho`

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

- [ ] Transparent centroid cache — recompute object AABB centroid only when geometry changes, not every frame
- [ ] Draw-list hygiene — merge adjacent draws that share VAO/material when packing the renderlist (matters at larger object counts)

### Language / framework

- [ ] Formal Morpho dependents (learn from Graphics→View; formalize a general dependent-object framework later)

## Completed in this rewrite

Live Graphics/Scene/View stack through Text draw-slots: stable ids, define vs draw, Broadcaster listeners, selective pose redraw, unit Sphere/Cylinder/Arrow mesh cache, `U O` / `U V` / `X O` / `X D`, materials/lighting/opacity, framing/auto-fit, ZMQ `View` session API, and yardsticks (`boing`, `nbody`, `soapbubble`, `vectors`, `flyingtext`).
