# MorphoView command API

Commands are the viewer’s public API. ASCII files (and later other producers) emit the same intermediate representation (`mv_command`); only the main/GLFW thread applies them and touches GL.

## Pipeline

```
producer → command_parse / command_enqueue → queue
                                              ↓
                    command_wake → glfwWaitEvents returns
                                              ↓
                         command_process (apply + free)
                                              ↓
                              scene / display / GL
```

1. **Parse** (`command_parse`) — lex an ASCII buffer and **enqueue** only. Does not open windows or mutate GL. On success, appends a trailing `MVCMD_PREPARE`. On failure, clears the queue.
2. **Enqueue** (`command_enqueue`) — takes ownership of an `mv_command`. If the queue was empty, calls `command_wake()`.
3. **Wake** (`command_wake`) — `glfwPostEmptyEvent()`, so a blocked `glfwWaitEvents` can run.
4. **Process** (`command_process`) — apply every queued command in order on the **caller** thread, then free them. Returns the number applied. On apply failure, frees the remainder and stops.

`main` processes once after loading a file (bootstrap, so windows exist), then `display_loop` processes again after each `glfwWaitEvents` (live updates). With `-b`/`-c`, an I/O thread also enqueues via ZMQ while the loop runs.

**Invariant:** only the main/GLFW thread calls `command_process` and touches GL. The command queue is mutex-protected so the I/O thread can `command_enqueue` safely; `command_wake` posts an empty GLFW event.

## Public C API

Declared in [`src/command.h`](../src/command.h):

| Function | Role |
|----------|------|
| `command_initialize` / `command_finalize` | Set up / tear down the queue and parse errors |
| `command_queue_init` / `command_queue_clear` | Init empty queue; free all pending commands |
| `command_enqueue` | Own and append a command; edge-triggered wake |
| `command_wake` | Post an empty GLFW event |
| `command_process` | Apply and free the queue |
| `command_parse` | ASCII → enqueue (+ `MVCMD_PREPARE`) |
| `command_loadinput` | Read a file into a buffer (`MORPHO_FREE` when done) |
| `command_removefile` | `remove()` a temp file (CLI `-t`) |
| `command_free` | Free one command’s owned payloads |

## Intermediate representation

Each command is a tagged `mv_command` (typed structs embed it as the first field). Producers may enqueue IR directly without going through the ASCII parser.

| Type | ASCII | Payload |
|------|-------|---------|
| `MVCMD_SCENE_CREATE` | `S <id> <dim>` | Scene id, dimension (2 or 3) |
| `MVCMD_UPDATE_SCENE` | `U S <id>` | Clear existing scene in place; select as current |
| `MVCMD_UPDATE_OBJECT` | `U O <id>` | Clear one object’s geometry for redefine |
| `MVCMD_UPDATE_VERTICES` | `U V <id> ["format"] <floats…>` | Same-length vertex replace |
| `MVCMD_CLOSE_SCENE` | `X S <id>` | Close scene window (same teardown as Escape) |
| `MVCMD_DELETE_OBJECT` | `X O <id>` | Delete object + OBJECT draws referencing it |
| `MVCMD_DELETE_DRAW` | `X D <drawId>` | Delete one draw-slot; leave the object |
| `MVCMD_QUIT` | `Q` | Request close of all windows / quit viewer |
| `MVCMD_WINDOW_TITLE` | `W "<title>"` | Owned title string |
| `MVCMD_BOUNDS` | `B <xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` | Explicit scene AABB; requests camera refit |
| `MVCMD_LIGHT` | `L <x y z [r g b]>` / `L a` | Explicit model-space light, or clear for AABB auto |
| `MVCMD_BACKGROUND` | `G <r g b>` | Scene clear / background color |
| `MVCMD_OBJECT` | `o <id>` | Object id |
| `MVCMD_VERTICES` | `v ["format"] <floats...>` | Optional format; float blob |
| `MVCMD_ELEMENT` | `p` / `l` / `f` `<indices...>` | Points, lines, or facets |
| `MVCMD_COLOR` | `c <id> <r g b [a]>...` | Color id; RGB triples or RGBA quads |
| `MVCMD_SELECT_COLOR` | `C <id>` | Active color id (uniform albedo for subsequent geometry/text) |
| `MVCMD_MATERIAL` | `M flat` / `M shaded [ka kd [ks [n]]]` | Shade mode + Phong coeffs |
| `MVCMD_DRAW` | `d <id>` | Object id; optional baked 4×4 matrix. If an OBJECT draw for `@p id` already exists, **replaces its matrix** instead of appending. |
| `MVCMD_CLEAR_DISPLAY` | `D` | Clear displaylist only (objects/colors/fonts/pools kept) |
| `MVCMD_FONT` | `F <id> "<path>" <size>` | Font id, path, size |
| `MVCMD_TEXT` | `T <fontid> "<string>"` | Font id, string; optional matrix |
| `MVCMD_PREPARE` | *(none — appended by parse)* | Upload **changed** scenes to GL (scenes touched by this batch) |

`S` is find-or-create: a new id opens a window; a repeated id selects that scene as current (does **not** clear). `U S` clears an existing scene’s contents while keeping its window, then selects it. `D` clears only the displaylist (draws) for the current sticky scene — objects, colors, fonts, and data pools remain. Apply context is **sticky across ZMQ/file batches**, so follow-up chunks may omit a leading `S` and still target the last selected scene. `X S` marks that scene’s window for close (loop tears it down). `Q` marks every window for close; if none are open and the listener is active, emits `window.closed` and stops. `MVCMD_PREPARE` calls `display_prepareall()`, which resets GL geometry then uploads only scenes marked changed (scenes touched by this batch). It auto-computes the scene AABB when no explicit `B` was given and fits the camera on first prepare (or after `B`) unless the user has already moved the view. Untouched open displays are left alone (avoids redundant GL rebuilds when several windows are open).

## ASCII language

Whitespace between tokens is ignored. Prefixes are single letters. Strings use `"..."` with `\` escaping the next character.

### Geometry and drawing

| Letter | Arguments | Notes |
|--------|-----------|-------|
| `S` | `<id> <dim>` | Create or select scene; open window if needed (does not clear) |
| `U` | `S <id>` \| `O <id>` \| `V <id> …` | `U S` clear scene; `U O` clear object for redefine; `U V` same-length vertex replace |
| `X` | `S <id>` \| `O <id>` \| `D <drawId>` | `X S` close scene; `X O` delete object (+ draws); `X D` delete one draw-slot |
| `Q` | — | Quit viewer (close all windows); Morpho `View.close` sends this |
| `W` | `"<title>"` | Set current window title |
| `B` | `<xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` | Explicit scene AABB; next prepare refits unless the user moved the camera |
| `L` | `<x> <y> <z> [<r> <g> <b>]` \| `a` | Explicit model-space light (optional color), or `a` to resume AABB auto placement |
| `G` | `<r> <g> <b>` | Scene clear / background color (default dark gray if omitted) |
| `o` | `<id>` | Current object (requires a scene) |
| `v` | `["format"] <floats...>` | Vertex data for current object |
| `p` / `l` / `f` | `<indices...>` | Points / lines / facets |
| `c` | `<id> <r g b [a]>...` | Define color table entry (RGB or RGBA) |
| `C` | `<id>` | Select uniform color for subsequent geometry and text |
| `M` | `flat` \| `shaded` [`<ka> <kd>` [`<ks>` [`<n>`]]] | Material: unlit or OpenGL/VTK Phong (default ka=kd=0.5, ks=0) |
| `d` | `<drawId>` \| `<drawId> <objectId>` | Draw slot `drawId` referencing object (defaults to `drawId` if one arg). Matrix from prior transforms. Updates existing slot by `drawId` (legacy: first object id match). Stamps current `C` onto the slot. No-matrix update preserves pose (recolor). |
| `D` | — | Clear displaylist only; keep objects/colors/fonts/pools (sticky scene) |
| `F` | `<id> "<path>" <size>` | Load font |
| `T` | `<fontid> "<string>"` | Draw text (matrix like `d`) |

### Materials and color

Shading uses the standard Phong model (Lambert when \(k_s=0\)):

\[
I = (k_a + k_d \max(\mathbf{N}\cdot\mathbf{L},0) + k_s (\mathbf{R}\cdot\mathbf{V})^n)\,C_\text{light}\,\text{albedo}
\]

- **`M shaded`** (default) — Phong/Lambert with science-friendly defaults (\(k_a=k_d=0.5\), \(k_s=0\)). Optional floats override coeffs.
- **`M flat`** — unlit albedo (diagrams / categorical color).
- **Uniform color:** `c` / `C` then `v "xn"` — `C` sets albedo (and optional alpha) for subsequent draws.
- **Vertex color:** `v "xnc"` without a preceding `C` — per-vertex RGB is the albedo (opaque). At draw time, meshes whose format includes `c` clear uniform-color mode so a prior translucent `C` cannot steal their albedo/alpha.
- **Opacity:** `c <id> <r g b a>` — alpha on the selected color. Opaque draws (`a ≈ 1`) first with depth write; transparent draws after with depth write off and standard alpha blending. Transparent objects are sorted **far → near** by object centroid (view-space z). Closed translucent meshes draw back faces then front to avoid mesh-order striping. Not triangle-level / OIT — intersecting translucents can still artifact.
- **Facet winding:** Package `Show` emits sparse face indices via `rowindices` (preserves mesh order). At upload, the viewer also reorients triangles so geometric normals agree with averaged vertex normals — needed for the transparent back/front pass.
- **Graphics `transmit` / `filter`:** Morpho mesh primitives carry POVRay-style transparency. Package `Show` maps `alpha = 1 − clamp(transmit + filter, 0, 1)` to uniform `c`/`C` + `v "xn"` (per-vertex color left for later). True filter-vs-transmit pigment behavior remains POVRay-only.

Lighting and eye position are in model space (stable under camera rotation). By default the light is placed outside the scene AABB. `L <x> <y> <z>` sets an explicit position (color unchanged; default white); optional `<r g b>` sets light color; `L a` clears the override and resumes AABB auto placement.

`G <r> <g> <b>` sets the clear color. If omitted, the viewer uses a dark bluish gray. Package `Show` emits `G` from `Graphics.background` (default `Black`).

### Transforms (parse-only)

These update a parse-local model matrix and are **not** enqueued. On the next `d` or `T`, if the matrix changed, it is baked into that command and the dirty flag is cleared (the matrix itself is kept until `i`).

| Letter | Arguments | Effect |
|--------|-----------|--------|
| `i` | — | Identity |
| `m` | 16 floats | Left-multiply by 4×4 |
| `r` | `<phi> <ax ay az>` | Rotate about axis |
| `s` | `<scale>` | Uniform scale |
| `t` | `<tx ty tz>` | Translate |

## Example

```
S 0 3
W "Example"
G 0 0 0
M shaded
c 0 1 0 0
C 0
o 1
v "xn"
-0.5 -0.5 0  0 0 1
 0.5 -0.5 0  0 0 1
 0.0  0.5 0  0 0 1
l
0 1 1 2 2 0
i
d 1
```

See also `test/command/linespts`, `test/command/polyhedra`, `test/command/twoscenes`, `test/command/largebbox` (auto-fit), `test/command/flatshade`, `test/command/uniformphong`, `test/command/materials` (flat | Lambert | Phong spheres), `test/command/opacity` (semi-transparent over opaque), `test/command/depthsort` (overlapping translucents, far→near), `test/command/transparentspheres` (overlapping Phong spheres), `test/command/light` (two windows: AABB auto vs explicit `L`), and `test/command/background` (`G` clear color).

## ZeroMQ transport

CLI:

| Flag | Meaning |
|------|---------|
| `-b <endpoint>` | Bind a ZMQ PAIR socket (e.g. `tcp://127.0.0.1:5555`) |
| `-c <endpoint>` | Connect a ZMQ PAIR socket (used by Morpho `View`) |

An I/O thread owns the socket. Each received string is an ASCII command chunk (`command_parse`). Replies/events (also strings):

| Message | Meaning |
|---------|---------|
| `ok` | Chunk parsed and enqueued successfully |
| `err …` | Parse failed |
| `window.closed` | Last display window closed (also after `Q` with no windows) |

Morpho helper: `import morphoview` then `View()` / `open` / `update` / `poll` / `wait` / `close` in [`share/modules/morphoview.morpho`](../share/modules/morphoview.morpho). `View.close()` sends `Q` and waits for `window.closed` (falls back to `pkill` only if the peer hangs).

`open` / `update` accept either an ASCII string or a `Graphics` object. Graphics is serialized by the package’s prototype `Show` (same visitor as graphics.morpho) into this command language; `View.write` is the File-compatible sink, so the serializer never knows about ZMQ. `Show(g)` remains fire-and-forget (temp file + `-t`). Live scene replace uses `U S <id>` (also what `update(Graphics)` emits).

### Occasional update vs efficient animation

Morpho `Graphics` is a displaylist container, not a scene graph. Serializer object ids are ephemeral per write.

- **Occasional refresh** — `View.open(Graphics)` / `update(Graphics)` → full `U S` reserialize. This is the supported high-level path for “new snapshot” / update-on-demand. Do not expect it to be cheap when most of the scene is static.
- **Efficient animation / composition** — see priority sequence in [`TODO.md`](../TODO.md): **Graphics prototype** (stable ids, define vs display, instancing) → **animation viewer ops** (sticky context, display/move, `U O` / `U V` / `X O` / `X D`) → **binary transport**. The command language already splits object definition (`o`/`v`/`f`) from display (`d` + transforms).
- **Stress demo** — `examples/amigaball.morpho` exercises full replace under animation load on purpose.
