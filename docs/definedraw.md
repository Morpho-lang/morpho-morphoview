# Define vs draw

Maintainer architecture for the Graphics / Scene / View stack. Morpho users: `help morphoview`. Command language: [`commandapi.md`](commandapi.md). Backlog: [`TODO.md`](../TODO.md).

## Layers

| Layer | Role |
|-------|------|
| **Graphics** | Static, id-bearing displaylist + `display` / `add`; accepted by `Show` / `View.open` |
| **Scene is Graphics** | Live subclass: Broadcaster + `move` / `recolor` / `remove` / `replace` / `morph` / batch |
| **View** | Listener / presenter over ZMQ (one of possibly several) |
| **Viewer** | Define/draw command protocol (`o`/`v`/`f`, `d`, `D`, `U O` / `U V`, `X O` / `X D`) |
| **`Show(g)` / `update(Graphics)`** | Snapshot path (full serialize / `U S`) — fire-and-forget and occasional refresh |

A general Morpho “dependent object” framework is out of scope; Scene→View listeners are a concrete example to learn from.

## Two use paths

1. **Set and forget** — build a `Graphics` (or Scene), `Show(g)` or `v.open(g)` once (or occasional `update(g)`). No listeners required.
2. **Live session** — use a `Scene`, attach a `View`, then `g.move(...)`; Scene stays authoritative and View follows.

Do **not** auto-diff inside `update(Graphics)`. Incremental updates go through Scene mutations → listener messages → View → targeted commands.

```
g = Scene()
var id = g.display(Sphere(...), color=, flat=)   // stable id; optional position=/scale=/rotate=
var v = View(g)                   // one Show.write, then listen
g.move(id, ...)                   // Scene → View: pose-only `d`
```

Put the first pose on `display` (or `move` before open) so the first paint is not identity. Scripts should not need viewer command strings for normal animation.

## Define vs draw

| Side | Commands | Purpose |
|------|----------|---------|
| **Define** | `o` / `v` / `p\|l\|f` | Geometry once (objects, vertices, connectivity) |
| **Draw** | `i` / `s` / `t` / `r` / `m` + `d` | Place with transform (draw-slot) |

Re-issuing `d` for an existing OBJECT or TEXT draw-slot updates in place (matrix replace; no-matrix update preserves pose for recolor). `D` clears the displaylist only (objects / colors / fonts / pools kept). Sticky apply context lets follow-up chunks omit a leading `S`. Per-object mesh edits: `U O` / `U V`; per-slot delete: `X D`; object delete: `X O`.

## Graphics model

Not a full scene graph. Richer than an append-only displaylist:

- Stable **ids** from `display` (returned `Int`, or `nil` on failure). Same item displayed twice → two ids.
- Entry **SRT** owns presentation pose (`position` / `scale` / `rotate`). `Show` places from the entry. API accepts list or Matrix for position.
- Unit primitives (`Sphere`, `Cylinder`, `Arrow`) store as unit item + pose on entry; many draws may share one viewer `o`. Color via `C`, not baked vertices.
- `Text` is draw-slot driven (`T <drawId> …`); string/font changes use `replace`.
- Scene mutators return `true`/`false`. Lookups (`findEntry`) use `nil` when missing.
- Listeners via `broadcast` + typed events (`Defined` / `Moved` / `Recolored` / `Removed` / `Replaced`).

## Yardsticks

| Example | Exercises |
|---------|-----------|
| [`examples/boing.morpho`](../examples/boing.morpho) | TriangleComplex movers via `g.move` |
| [`examples/nbody.morpho`](../examples/nbody.morpho) | Shared Spheres + `move` / `recolor` |
| [`examples/soapbubble.morpho`](../examples/soapbubble.morpho) | `U V` morph / refine `replace` |
| [`examples/vectors.morpho`](../examples/vectors.morpho) | Shared Cylinder / Arrow + `move` |
| [`examples/flyingtext.morpho`](../examples/flyingtext.morpho) | Text draw-slots |
