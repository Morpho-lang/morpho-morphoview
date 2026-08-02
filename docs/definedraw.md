# Define vs draw (Graphics / Scene / animation sketch)

Target Morpho model: **Graphics** is the static displaylist container; **Scene is Graphics** is the live model (Broadcaster + `move` / `recolor`); **View** is a presenter that listens for Scene changes and drives the viewer. The viewer command language stays define vs draw (`o`/`v`/`f` vs `d` + transforms).

A general Morpho “dependent object” framework is **out of scope** for now. Scene→View listeners are a concrete example we can learn from; formalizing the pattern comes later.

## Source of truth

| Layer | Role |
|-------|------|
| **Graphics** | Static, id-bearing displaylist + `display` / `add`; accepted by `Show` / `View.open` |
| **Scene is Graphics** | Live subclass: Broadcaster + `move` / `recolor` / batch; broadcasts to listeners |
| **View** | Listener / presenter over ZMQ (one of possibly several) |
| **Viewer** | Define/draw command protocol (`o`/`v`/`f`, `d`, `D`, `U O` / `U V`, `X O` / `X D`) |
| **`Show(g)` / `update(Graphics)`** | Snapshot path (full serialize / `U S`) — fire-and-forget and occasional refresh |

**Two equally valid uses:**

1. **Set and forget** — build a `Graphics` (or Scene), `Show(g)` or `v.open(g)` once (or occasional `update(g)`). No `move`, no listeners required. Still the common scientific-viz path.
2. **Live session** — use a `Scene`, attach a `View`, then `g.move(...)`; Scene stays authoritative and View follows.

Do **not** auto-diff inside `update(Graphics)`. Incremental updates go through Scene mutations → listener messages → View → targeted commands.

```
g = Scene()
var id = g.display(Sphere(...), color=, flat=)   // stable id; optional position=/scale=/rotate=
var v = View(g)                   // one Show.write, then listen
g.move(id, ...)                   // Scene → View: pose-only `d` (in-place matrix)
```

Simulation state may still live in script variables; the script applies it via `g.move` / similar so Scene (and thus View) stay consistent.

**Ergonomics:** small surface — `display` / `move` share pose args (`position` as 2nd positional + `scale=` / `rotate=` kwargs; Morpho arity overloads because `=nil` defaults are keyword-only). Same unit item under two `display`s → two ids that `move` independently. API and entry both use `position` (absolute Matrix internally); `Show` emits viewer `t` from it. Scripts should not need viewer command strings for normal animation. Canonical live session API: `View(Scene)` (or `View()` + `open(scene)`). Put the first pose on `display` (or `move` before open) so the first paint is not identity.

## Graphics / Scene shape (evolving)

Not a full scene graph. Richer than an append-only displaylist:

- Stable **ids** from `display` (returned Int on the entry). Not on mesh primitives — same value may be displayed twice under two ids.
- Entry **SRT** owns presentation pose (`position` as Matrix 3-vector / `scale` / `rotate` as fields on the entry); `Show` places from the entry only. API accepts list or Matrix for position and coerces to Matrix. Keep SRT fields (not one 4×4). `Sphere`s store as **unit** item + pose on entry (Phase 5c). `Cylinder` / `Arrow` store as **unit** shaft (+ tip) + start→end as entry SRT with non-uniform `scale` `[R,R,L]` (Phase 5e). `move` (on Scene): position always sets absolute `entry.position`; omitted scale/rotate leave components unchanged.
- Small **mutation API** on Scene: `move`, `recolor`, `remove`, `replace`, `morph`; `beginBatch`/`endBatch` on Broadcaster
- **`morph` vs `replace`:** `Scene.morph` / `View.morph` / `View.refreshMesh` — same-length vertex push (`U V`). `replace` — full redefine (new connectivity / type). Prefer `View.morph(id, item)` when a listener is attached.
- **Return values:** `display` returns a Graphics id (`Int`) or `nil` (no id); mutators (`move` / `recolor` / `remove` / `replace` / `morph`) return `true`/`false`. Lookups (`findEntry`) still use `nil` when missing.
- **Phase 5f:** `Text` is draw-slot driven — `move` / `remove` / `recolor` update text like mesh slots; string/font via `replace`.
- **Listeners** (`broadcast` module) + typed events (`GraphicsEventDefined` / `Moved` / `Recolored` / `Removed` / `Replaced`)
- `open(g)` uses one `Show.write` then listens if `g` is a Scene; `update(g)` full replace + rebind

`Show` walks Graphics **entries** (Scene included). Phase 5c: `Sphere` → unit mesh via Show cache (key = refine bucket + material mode), draw with entry SRT; many entries may share one viewer `o`. Color/opacity via `C`, not baked vertices, so instances share geometry. `PointCloud` / `LineSet` use entry SRT. **Phase 5e:** same pattern for `Cylinder` / `Arrow` (unit shaft ± tip, start→end as SRT; viewer `s sx sy sz`). **Phase 5f:** `Text` on draw-slots (`T <drawId> <fontid> "…"`) so `move` / `remove` work (string/font via `replace`).

## Viewer protocol

**Define** (geometry once): `o` / `v` / `p|l|f`  
**Draw** (place with transform): `i` / `s` / `t` / `r` / `m` + `d`

Re-issuing `d` today only **appends**. To refresh draws without wiping geometry:

| Command | Effect | Status |
|---------|--------|--------|
| `D` | Clear displaylist only (objects / colors / fonts / pools kept) | Done |
| then `C` / `M` / transforms / `d` | Rebuild draws | Existing |

Sticky apply context lets follow-up chunks omit leading `S`. Per-object mesh edits: `U O` / `U V`; per-slot delete: `X D`; object delete: `X O` (Phase 5d).

**Draw slots (Phase 5c):** each Graphics entry ↔ one displaylist slot (`d <drawId> [objectId]`; matrix + stamped uniform color). Pose/`recolor` update that slot; several slots may reference the same object id. No-matrix `d` preserves pose (recolor).

Low-level escape hatch: `View.redraw(ascii)` still useful for tests/fixtures; primary animation API is Graphics mutations → listener.

## `Show.write` emit patterns

| Primitive | First define | Later draw (same Show / same id) |
|-----------|--------------|----------------------------------|
| Opaque / translucent `Sphere` | unit mesh `o`/`v`/`f` (cached by refine key); `C` for color/alpha | entry SRT `i`/`s`/`r`/`t`/`d` (Phase 5c; many `d` → one `o`) |
| `Cylinder` / `Arrow` | unit shaft (± tip) mesh cache (Phase 5e); `C` for color | entry SRT from start→end; non-uniform `s sx sy sz` |
| `Text` | font + `T` (Phase 5f: entry-owned pose) | entry pose update / remove (Phase 5f); content via `replace` |
| `TriangleComplex` | `o`/`v`/`f` (+ register `c` if transmit) | entry SRT (often identity if world-baked) |
| `PointCloud` / `LineSet` | `o`/`v`/`p|l` | entry SRT (Phase 5c) |

## Implementation order

1. **Foundation** — Graphics-owned ids on `display`; Show walks entries; keep `U S` snapshot path ✅
2. **Viewer** — sticky context; `D`; prepare-safe redraw ✅
3. **Graphics ↔ View** — `move`; listener registration; View translates events → commands ✅
4. **Yardstick** — rewrite `examples/boing.morpho` via `g.move` (not full `U S`) ✅
5. **Phase 5** — 5a–5f ✅; dependents later (**5g**). Details: [`plan-graphics-view-listener.md`](plan-graphics-view-listener.md).
## Hand sequences / tests

| File | Role | Runnable now? |
|------|------|----------------|
| [`test/command/definedraw-once`](../test/command/definedraw-once) | Define + first draws (ASCII) | Yes |
| [`test/command/definedraw-redraw`](../test/command/definedraw-redraw) | `D` + redraw draws (no `#` comments — command lexer) | Yes |
| [`test/command/definedraw-pose-update`](../test/command/definedraw-pose-update) | Pose-only `d` (in-place matrix, no `D`) | Yes |
| [`test/testdefinedraw.morpho`](../test/testdefinedraw.morpho) | `View.open` once + `View.redraw` | Yes |
| [`test/testgraphicsmove.morpho`](../test/testgraphicsmove.morpho) | `move` / listeners / objectMap / recolor | Yes |
| [`test/testviewmove.morpho`](../test/testviewmove.morpho) | Live `open` → `move` | Yes |
| [`test/command/definedraw-update-object`](../test/command/definedraw-update-object) | `U O` redefine + draw | Yes |
| [`test/command/definedraw-update-vertices`](../test/command/definedraw-update-vertices) | Same-length `U V` | Yes |
| [`test/command/definedraw-delete-object`](../test/command/definedraw-delete-object) | `X O` | Yes |
| [`test/command/definedraw-delete-draw`](../test/command/definedraw-delete-draw) | `X D` one slot | Yes |
| [`test/testviewremove.morpho`](../test/testviewremove.morpho) | Live `replace` / `remove` | Yes |

Yardsticks: [`examples/boing.morpho`](../examples/boing.morpho) (TriangleComplex movers); [`examples/nbody.morpho`](../examples/nbody.morpho) (shared `Sphere`s + `move`/`recolor`); [`examples/soapbubble.morpho`](../examples/soapbubble.morpho) (Area+Volume CG → `U V` morph / refine `replace`).
