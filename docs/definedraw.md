# Define vs draw (Graphics / animation sketch)

Target Morpho model: **Graphics is the live scene**; **View is a presenter** that listens for changes and drives the viewer. The viewer command language stays define vs draw (`o`/`v`/`f` vs `d` + transforms).

A general Morpho “dependent object” framework is **out of scope** for now. Graphics→View listeners are a concrete example we can learn from; formalizing the pattern comes later.

## Source of truth

| Layer | Role |
|-------|------|
| **Graphics** | Live, id-bearing model + mutation API; broadcasts to listeners |
| **View** | Listener / presenter over ZMQ (one of possibly several) |
| **Viewer** | Define/draw command protocol (`o`/`v`/`f`, `d`, proposed `D`, later `U O` / `U V`) |
| **`Show(g)` / `update(Graphics)`** | Snapshot path (full serialize / `U S`) — fire-and-forget and occasional refresh |

**Two equally valid uses of Graphics:**

1. **Set and forget** — build a displaylist, `Show(g)` or `v.open(g)` once (or occasional `update(g)`). No `move`, no listeners required. Still the common scientific-viz path.
2. **Live session** — attach a `View`, then `g.move(...)` (optionally `begin`/`end`); Graphics stays authoritative and View follows.

Do **not** auto-diff inside `update(Graphics)`. Incremental updates go through Graphics mutations → listener messages → View → targeted commands.

```
g = Graphics()
var id = g.display(Sphere(...))   // stable id
var v = View(g)                   // registers as listener; define+draw once
g.move(id, ...)                   // Graphics state changes
// g notifies listeners → v sends cheap viewer commands
```

Simulation state may still live in script variables; the script applies it via `g.move` / similar so Graphics (and thus View) stay consistent.

**Ergonomics:** the public modification surface stays small: `display`, **`move`** (single pose verb with optional `scale`/`rotate` — not separate SRT methods), `begin`/`end`. Scripts should not need viewer command strings for normal animation. `View.redraw` is an escape hatch for tests.

## Graphics shape (evolving)

Not a full scene graph. Richer than today’s append-only displaylist:

- Stable **ids** from `Graphics.display` (returned Int; stored on the Graphics entry). Not properties of mesh primitives — the same `Sphere`/`TriangleComplex` value can be `display`’d twice under two ids and `move`’d independently.
- Enough structure for **presentation** per id (transform on the entry) so `move` is a Graphics state change
- Small **mutation API**: `display`, `move`, `begin`/`end`, later replace/remove
- **Listeners** + a small event vocabulary (e.g. `defined`, `moved`, `replaced`, `removed`)
- Coalesce bursts (many moves in one frame → one viewer chunk, not N round-trips)

`Show` walks Graphics **entries** (id + item + transform). Viewer-side mesh instancing (one `o`, many `d`) is allowed when geometry matches — separate from Graphics giving each placement its own id.

## Viewer protocol

**Define** (geometry once): `o` / `v` / `p|l|f`  
**Draw** (place with transform): `i` / `s` / `t` / `r` / `m` + `d`

Re-issuing `d` today only **appends**. To refresh draws without wiping geometry:

| Command | Effect | Status |
|---------|--------|--------|
| `D` | Clear displaylist only (objects / colors / fonts / pools kept) | Proposed |
| then `C` / `M` / transforms / `d` | Rebuild draws | Existing |

Sticky apply context lets follow-up chunks omit leading `S`. Per-object mesh edits later: `U O` / `U V`.

Low-level escape hatch: `View.redraw(ascii)` still useful for tests/fixtures; primary animation API is Graphics mutations → listener.

## `Show.write` emit patterns

| Primitive | First define | Later draw (same Show / same id) |
|-----------|--------------|----------------------------------|
| Opaque `Sphere` | unit `o`/`v`/`f` (keyed by refinement; color via `C`) | `C`? `i` `s` `t` `d` |
| Translucent `Sphere` | expand to mesh (no instance) | — |
| `TriangleComplex` with `id` | `o`/`v`/`f` (+ register `c` if transmit) | `i` `d` (+ transforms from Graphics pose when present) |
| `PointCloud` / `LineSet` with `id` | `o`/`v`/`p|l` | `i` `d` |

Today’s Show already instances **opaque uncolored** spheres; next work extends uniform `C` color and honors explicit ids.

## Implementation order (to refine in plan)

1. **Foundation** — Graphics-owned ids on `display`; Show walks entries; opaque sphere + `C` instancing; keep `U S` snapshot path
2. **Viewer** — sticky context; `D`; prepare-safe redraw
3. **Graphics ↔ View** — `move`; listener registration; View translates events → commands (batched)
4. **Yardstick** — rewrite `examples/amigaball.morpho` via `g.move` (not full `U S`)
5. **Later** — `U O` / `U V`; general Morpho dependents framework

## Hand sequences / tests

| File | Role | Runnable now? |
|------|------|----------------|
| [`test/command/definedraw-once`](../test/command/definedraw-once) | Define + first draws (ASCII) | Yes |
| [`test/command/definedraw-redraw`](../test/command/definedraw-redraw) | Sample `D` + redraw | **No** until `D` exists |
| [`test/testdefinedraw.morpho`](../test/testdefinedraw.morpho) | `View.open` of once fixture | Yes |

Yardstick: [`examples/amigaball.morpho`](../examples/amigaball.morpho).
