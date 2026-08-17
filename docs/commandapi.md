# MorphoView command API

Commands are the viewer’s public API: an ASCII language that parses into a tagged IR (`mv_command`). Only the main/GLFW thread applies commands and touches GL.

Morpho users: `help morphoview` (or see [`share/modules/morphoview.morpho`](../share/modules/morphoview.morpho)).

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

1. **Parse** (`command_parse`) — lex an ASCII buffer and **enqueue** only. Does not open windows or mutate GL. On success, appends a trailing `MVCMD_PREPARE`. On failure, discards only that chunk’s staged IR; previously enqueued batches stay.
2. **Enqueue** (`command_enqueue`) — takes ownership of an `mv_command`. If the queue was empty, calls `command_wake()`.
3. **Wake** (`command_wake`) — `glfwPostEmptyEvent()`, so a blocked `glfwWaitEvents` can run.
4. **Process** (`command_process`) — apply every queued command in order on the **caller** thread, then free them. Returns the number applied. On apply failure, frees the remainder and stops.

`main` processes once after loading a file (bootstrap), starts `-b`/`-c` listening after that parse, then `display_loop` processes again after each `glfwWaitEvents`. With `-b`/`-c`, an I/O thread enqueues via ZMQ while the loop runs. Live parse replies `ok`/`err …` immediately; apply runs later on the GLFW thread.

**Invariant:** only the main/GLFW thread calls `command_process` and touches GL. The queue is mutex-protected so the I/O thread can `command_enqueue` safely.

## ASCII reference

Whitespace between tokens is ignored. Prefixes are single letters. Strings use `"..."` with `\` escaping the next character. Producers may also enqueue IR directly (typed structs embed `mv_command` as the first field).

| Letter | IR | Arguments | Notes |
|--------|-----|-----------|-------|
| `S` | `MVCMD_SCENE_CREATE` | `<id> <dim>` | Create or select scene; open window if needed (does **not** clear) |
| `U S` | `MVCMD_UPDATE_SCENE` | `<id>` | Clear existing scene in place; keep window; select as current |
| `U O` | `MVCMD_UPDATE_OBJECT` | `<id>` | Clear one object’s geometry for redefine |
| `U V` | `MVCMD_UPDATE_VERTICES` | `<id> ["format"] <floats…>` | Same-length vertex replace |
| `X S` | `MVCMD_CLOSE_SCENE` | `<id>` | Close scene window (same teardown as Escape) |
| `X O` | `MVCMD_DELETE_OBJECT` | `<id>` | Delete object + OBJECT draws referencing it |
| `X D` | `MVCMD_DELETE_DRAW` | `<drawId>` | Delete one draw-slot; leave the object |
| `Q` | `MVCMD_QUIT` | — | Close all windows / quit viewer |
| `W` | `MVCMD_WINDOW_TITLE` | `"<title>"` | Set current window title |
| `B` | `MVCMD_BOUNDS` | `<xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` | Explicit scene AABB; next prepare refits unless user moved camera |
| `L` | `MVCMD_LIGHT` | `"neutral"` \| `"threepoint"` \| `"auto"` \| `"off"` \| `<n> "x"` \| `<n> "xc"` \| `0` | Named camera-relative rig, or n world-space point lights (cap 4). Omit `L` for Neutral (a bare `L` is invalid). `L "off"` / `L 0` is ambient only |
| `G` | `MVCMD_BACKGROUND` | `<r> <g> <b>` | Scene clear / background color |
| `o` | `MVCMD_OBJECT` | `<id>` | Current object (requires a scene) |
| `v` | `MVCMD_VERTICES` | `["format"] <floats…>` | Vertex data for current object |
| `p` / `l` / `f` | `MVCMD_ELEMENT` | `<indices…>` | Points / lines / facets |
| `c` | `MVCMD_COLOR` | `<id> <r g b [a]>…` | Color table entry (RGB or RGBA) |
| `C` | `MVCMD_SELECT_COLOR` | `<id>` or (none) | Active color stamped onto subsequent `d` / `T`. Bare `C` clears the draw-slot override (restore geometry vertex colors). Parse context only |
| `M` | `MVCMD_MATERIAL` | `flat` \| `shaded` [`<ka> <kd>` [`<ks>` [`<n>`]]] | Unlit or Phong (default ka=kd=0.5, ks=0) |
| `d` | `MVCMD_DRAW` | `<drawId>` \| `<drawId> <objectId>` | Draw-slot; matrix from prior transforms; stamps `C`. Existing slot → update in place |
| `D` | `MVCMD_CLEAR_DISPLAY` | — | Clear displaylist only; keep objects/colors/fonts/pools |
| `F` | `MVCMD_FONT` | `<id> "<path>" <size>` | Load font |
| `T` | `MVCMD_TEXT` | `<fontid> "…"` \| `<drawId> <fontid> "…"` | Legacy append, or text draw-slot create/update |
| — | `MVCMD_PREPARE` | *(appended by parse)* | Upload **changed** scenes to GL |

## Semantics

### Scenes

- `S` is find-or-create: a new id opens a window; a repeated id selects that scene (does not clear).
- `U S` clears an existing scene’s contents while keeping its window, then selects it.
- `X S` marks that scene’s window for close (the display loop tears it down).
- `Q` marks every window for close. If none are open and the listener is active, emits `window.closed` and stops. Morpho `View.close` sends `Q`.

### Sticky context

Apply context persists across ZMQ/file batches. Follow-up chunks may omit a leading `S` and still target the last selected scene.

### Draw-slots

- `d <drawId> [objectId]` — object defaults to `drawId` if one arg. If an OBJECT or TEXT draw for that id already exists, **replaces its matrix** (and stamps color if `C` preceded this `d`) instead of appending. A no-matrix `d` without `C` preserves pose and color mode (recolor uses `C` then `d`).
- Bare `C` then `d` clears the uniform override so ColorTable geometry shows again.
- `D` clears only the displaylist (draws) for the current sticky scene — objects, colors, fonts, and data pools remain.
- `T <drawId> <fontid> "…"` creates/updates a text draw-slot (matrix like `d`). Legacy `T <fontid> "…"` appends.

### Prepare

Each successful parse appends `MVCMD_PREPARE`, which calls `display_prepareall()`:

- Uploads only scenes marked changed by this batch (untouched open displays are left alone).
- Auto-computes the scene AABB when no explicit `B` was given.
- Fits the camera on first prepare (or after `B`) unless the user has already moved the view.

## Materials, color, lighting

Shading uses Phong (Lambert when \(k_s=0\)):

\[
I = \text{albedo}\,\Bigl(k_a\,\text{ambient}
    + \sum_i C_i\bigl(k_d \max(\mathbf{N}\cdot\mathbf{L}_i,0) + k_s (\mathbf{R}_i\cdot\mathbf{V})^n\bigr)\Bigr)
\]

- **`M shaded`** (default) — Phong/Lambert; defaults \(k_a=k_d=0.5\), \(k_s=0\). Optional floats override coeffs.
- **`M flat`** — unlit albedo (diagrams / categorical color).
- **Uniform color:** `c` / `C` then `v "xn"` (or `v "x"` for points/lines) — `C` sets albedo (and optional alpha) for subsequent draws.
- **Vertex format:** `v` / `U V` take a format string whose letters name fields in order: `x` position (`dim` floats), `n` normal (`dim`), `c` RGB (3), `a` alpha (1). Missing `c`/`n`/`a` use defaults (white, +z, alpha 1). Typical Show layouts: `xn`, `xnc`, `xnca`, `x`, `xc`, `xca`.
- **Vertex color:** `v "xnc"` / `v "xc"` with a draw-slot in empty color mode — per-vertex RGB is the albedo (opaque unless `a` is also present). `C <id>` on the same draw-slot is a uniform RGB override that hides vertex colors without redefining geometry; vertex `a` is unchanged. Bare `C` then `d` clears that override. `d` without a preceding `C` preserves the current mode. Package `Show` emits `C` or `C <id>` immediately before each `d` so the color mode is self-contained (bare `C` for an intrinsic ColorTable).
- **Opacity:** `c <id> <r g b a>` — opaque draws (`a ≈ 1`) first with depth write; transparent draws after with depth write off. A format that includes `a` is treated as transparent. Transparent objects sorted **far → near** by object centroid. Closed translucent meshes draw back faces then front. Not triangle-level / OIT — intersecting translucents can still artifact.
- **Facet winding:** Package `Show` emits sparse face indices via `rowindices`. Plot duplicates face vertices and emits `0,2,1` when orientation requires a flip, so geometric winding already matches authored normals. At upload, the viewer still reorients triangles if they disagree (safety net for the transparent back/front pass).
- **Graphics alpha:** Package `Show` maps uniform `Color.a` (and `Coloring.opacity`) to `c`/`C` + colorless geometry (`v "xn"` / `v "x"`). `Color(r,g,b)` is opaque with `a=1`; `Color(r,g,b,a)` sets alpha. A `ColorTable` on a vertex-bearing primitive is one color per vertex; RGB tables emit `v "xnc"` / `v "xc"`, RGBA tables emit `v "xnca"` / `v "xca"`. Draw-slot `C` is a uniform `GraphicsEntry.color` override (not a `ColorTable`).

World space is right-handed: **+X right, +Y up, +Z toward the home viewer**. Home view is `Scale * Translate(-center)` (no extra rotation). Lighting is evaluated in **view space**; ortho `V = (0,0,1)` (toward the camera). Scene ambient is white in every mode; lamps contribute diffuse and specular only.

- **Default (omit `L`)** — Neutral: three white view-space directionals plus white ambient (colormap-safe; key stays on the camera side when you orbit).
- **`L "neutral"`** / **`L "auto"`** — same Neutral rig (`"auto"` is an alias).
- **`L "threepoint"`** — key / fill / rim, also view-relative.
- **`L <n> "x" <posn>…`** — n world-space point lights, white. Cap 4.
- **`L <n> "xc" <posn> <color>…`** — same, with RGB per lamp. Package `Show` always emits `"xc"`.
- **`L "off"`** / **`L 0`** — ambient only.

One `L` replaces the whole list. Package `Show` writes no `L` line when `Graphics.light` is empty (`[]`, Neutral). `g.light = "neutral"` / `"auto"` emit `L "neutral"`; `"threepoint"` and `"off"` emit those names. Set `g.light` to a 3-vector / `Light` / list of those, or call `g.addLight(...)`. Morpho accepts at most 4 lamps; unknown names error. Live `View` sends `L "neutral"` on `resetLights()` so the viewer drops prior lamps.

`G <r> <g> <b>` sets the clear color (default dark bluish gray). Package `Show` emits `G` from `Graphics.background` (default `Black`).

## Transforms (parse-only)

These update a parse-local model matrix and are **not** enqueued. On the next `d` or `T`, if the matrix changed, it is baked into that command and the dirty flag is cleared (the matrix itself is kept until `i`).

| Letter | Arguments | Effect |
|--------|-----------|--------|
| `i` | — | Identity |
| `m` | 16 floats | Right-multiply by 4×4 (`model = model * X`) |
| `r` | `<phi> <ax ay az>` | Rotate about axis (left-multiply) |
| `s` | `<scale>` or `<sx sy sz>` | Uniform or non-uniform scale (left-multiply) |
| `t` | `<tx ty tz>` | Translate (left-multiply) |

`s` / `r` / `t` left-multiply so commands written in application order compose as usual (`t` after `s` → scale then translate). `m` right-multiplies so a matrix emitted between basis and translation (e.g. text `dirn`/`vertical`) stays in local space: `i`/`s`/`r`, then `m`, then `t` yields `T·R·S·X`.

## Example

```
S 0 3
W "Example"
G 0 0 0
M shaded
c 0 1 0 0
o 1
v "xn"
-0.5 -0.5 0  0 0 1
 0.5 -0.5 0  0 0 1
 0.0  0.5 0  0 0 1
l
0 1 1 2 2 0
i
C 0
d 1
```

Fixtures under `test/command/`: `linespts`, `polyhedra`, `twoscenes`, `largebbox` (auto-fit), `flatshade`, `uniformphong`, `materials`, `opacity`, `vertexalpha`, `depthsort`, `transparentspheres`, `light`, `background`, `color-override`, plus define/draw fixtures (`definedraw-*`).

## C API

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

## ZeroMQ and Morpho

| Flag | Meaning |
|------|---------|
| `-b <endpoint>` | Bind a ZMQ PAIR socket (e.g. `tcp://127.0.0.1:5555`) |
| `-c <endpoint>` | Connect a ZMQ PAIR socket (used by Morpho `View`) |

An I/O thread owns the socket. Each received string is an ASCII command chunk (`command_parse`). Replies:

| Message | Meaning |
|---------|---------|
| `ok` | Chunk parsed and enqueued successfully |
| `err …` | Parse failed; remainder is a user-reportable string (`Error [id] at line N char M: …`) |
| `window.closed` | Last display window closed (also after `Q` with no windows) |

Command-line file parse (`morphoview file.cmd`) writes that same reportable string to stderr and does not use ZMQ. A live `-b`/`-c` session sends `err …` on the PAIR socket instead of printing.

`import morphoview` then `View` / `Show` — see `help morphoview`. Summary:

- `Show(g)` — fire-and-forget (temp file + `-t`; unlinks when the viewer exits).
- `View.open` / `update` — ASCII string or `Graphics` / `Scene`. Graphics is serialized by package `Show`; `View.write` is the File-compatible sink.
- `update(Graphics)` emits `U S` (full replace). Live animation uses `Scene.move` → listener → targeted `d` / `C` / `U V` / etc.
- `View.close()` sends `Q` and waits for `window.closed` (falls back to process kill if the peer hangs).

### Occasional update vs efficient animation

| Use case | Path | Expectation |
|----------|------|-------------|
| Occasional refresh | `View.open(Graphics)` / `update(Graphics)` → `U S` | Fine for snapshot / on-demand. Not cheap for large static+dynamic scenes. |
| Efficient animation | `Scene` + `View` → `g.move` / `recolor` / … | Define once; pose-only updates. |

Do not make `update(Graphics)` auto-diff. Efficiency comes from Scene mutations → targeted viewer ops. Future work (binary transport, etc.): [`TODO.md`](../TODO.md).
