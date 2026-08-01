# Define vs draw (Graphics / animation sketch)

Target Morpho model: **Graphics is the live scene**; **View is a presenter** that listens for changes and drives the viewer. The viewer command language stays define vs draw (`o`/`v`/`f` vs `d` + transforms).

A general Morpho “dependent object” framework is **out of scope** for now. Graphics→View listeners are a concrete example we can learn from; formalizing the pattern comes later.

## Source of truth

| Layer | Role |
|-------|------|
| **Graphics** | Live, id-bearing model + mutation API; broadcasts to listeners |
| **View** | Listener / presenter over ZMQ (one of possibly several) |
| **Viewer** | Define/draw command protocol (`o`/`v`/`f`, `d`, `D`; later `U O` / `U V`) |
| **`Show(g)` / `update(Graphics)`** | Snapshot path (full serialize / `U S`) — fire-and-forget and occasional refresh |

**Two equally valid uses of Graphics:**

1. **Set and forget** — build a displaylist, `Show(g)` or `v.open(g)` once (or occasional `update(g)`). No `move`, no listeners required. Still the common scientific-viz path.
2. **Live session** — attach a `View`, then `g.move(...)`; Graphics stays authoritative and View follows.

Do **not** auto-diff inside `update(Graphics)`. Incremental updates go through Graphics mutations → listener messages → View → targeted commands.

```
g = Graphics()
var id = g.display(Sphere(...))   // stable id; optional position=/scale=/rotate=
var v = View(g)                       // one Show.write, then listen
g.move(id, ...)                   // Graphics → View: pose-only `d` (in-place matrix)
```

Simulation state may still live in script variables; the script applies it via `g.move` / similar so Graphics (and thus View) stay consistent.

**Ergonomics:** small surface — `display` / `move` share pose args (`position` as 2nd positional + `scale=` / `rotate=` kwargs; Morpho arity overloads because `=nil` defaults are keyword-only). Same unit item under two `display`s → two ids that `move` independently. API and entry both use `position` (absolute Matrix internally); `Show` emits viewer `t` from it. Scripts should not need viewer command strings for normal animation. Canonical session API: `View(g)` (or `View()` + `open(g)`). Put the first pose on `display` (or `move` before open) so the first paint is not identity.

## Graphics shape (evolving)

Not a full scene graph. Richer than today’s append-only displaylist:

- Stable **ids** from `Graphics.display` (returned Int on the entry). Not on mesh primitives — same value may be displayed twice under two ids.
- Entry **SRT** owns presentation pose (`position` as Matrix 3-vector / `scale` / `rotate` as fields on the entry); `Show` places from the entry only. API accepts list or Matrix for position and coerces to Matrix. Keep SRT fields (not one 4×4). Posed `Sphere`s normalize to unit item + pose on entry. `move`: position always sets absolute `entry.position`; omitted scale/rotate leave components unchanged.
- Small **mutation API**: `display`, `move`; `beginBatch`/`endBatch` on Broadcaster; later replace/remove
- **Listeners** (`broadcast` module) + typed events (`GraphicsEventDefined` / `Moved`)
- `open(g)` uses one `Show.write` then listens; `update(g)` full replace + rebind

`Show` walks Graphics **entries**. Abstract primitives (`Sphere`, `Cylinder`, …) convert in `visit` / `visitGeneric` like today — no Show-level sphere mesh cache. Prefer clients `display`ing one item at many poses. Entry SRT is recorded now; using it for draw (vs baking) is Phase 5c.

## Viewer protocol

**Define** (geometry once): `o` / `v` / `p|l|f`  
**Draw** (place with transform): `i` / `s` / `t` / `r` / `m` + `d`

Re-issuing `d` today only **appends**. To refresh draws without wiping geometry:

| Command | Effect | Status |
|---------|--------|--------|
| `D` | Clear displaylist only (objects / colors / fonts / pools kept) | Done |
| then `C` / `M` / transforms / `d` | Rebuild draws | Existing |

Sticky apply context lets follow-up chunks omit leading `S`. Per-object mesh edits later: `U O` / `U V` (Phase 5d). Re-issuing `d` for an object id already in the displaylist **replaces** its matrix (Phase 5b); first `d` for an id still appends.

Low-level escape hatch: `View.redraw(ascii)` still useful for tests/fixtures; primary animation API is Graphics mutations → listener.

## `Show.write` emit patterns

| Primitive | First define | Later draw (same Show / same id) |
|-----------|--------------|----------------------------------|
| Opaque `Sphere` | `visitGeneric` → `o`/`v`/`f` (bake center/r) | `i`/`d` (entry SRT — Phase 5c) |
| Translucent `Sphere` | same + `C` if transmit | `i`/`d` (entry SRT — Phase 5c) |
| `TriangleComplex` | `o`/`v`/`f` (+ register `c` if transmit) | entry SRT (often identity if world-baked) |
| `PointCloud` / `LineSet` | `o`/`v`/`p|l` | entry SRT (confirm in Phase 5c) |

## Implementation order

1. **Foundation** — Graphics-owned ids on `display`; Show walks entries; keep `U S` snapshot path ✅
2. **Viewer** — sticky context; `D`; prepare-safe redraw ✅
3. **Graphics ↔ View** — `move`; listener registration; View translates events → commands ✅
4. **Yardstick** — rewrite `examples/amigaball.morpho` via `g.move` (not full `U S`) ✅
5. **Phase 5** — batching (5a), selective redraw (5b), Show emit (5c), `U O` / `U V` / `X O` (5d); dependents framework later (5e). Details: [`plan-graphics-view-listener.md`](plan-graphics-view-listener.md).
## Hand sequences / tests

| File | Role | Runnable now? |
|------|------|----------------|
| [`test/command/definedraw-once`](../test/command/definedraw-once) | Define + first draws (ASCII) | Yes |
| [`test/command/definedraw-redraw`](../test/command/definedraw-redraw) | `D` + redraw draws (no `#` comments — command lexer) | Yes |
| [`test/command/definedraw-pose-update`](../test/command/definedraw-pose-update) | Pose-only `d` (in-place matrix, no `D`) | Yes |
| [`test/testdefinedraw.morpho`](../test/testdefinedraw.morpho) | `View.open` once + `View.redraw` | Yes |
| [`test/testgraphicsmove.morpho`](../test/testgraphicsmove.morpho) | `move` / listeners / objectMap | Yes |
| [`test/testviewmove.morpho`](../test/testviewmove.morpho) | Live `open` → `move` | Yes |

Yardstick: [`examples/amigaball.morpho`](../examples/amigaball.morpho).
