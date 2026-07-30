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
| `MVCMD_CLOSE_SCENE` | `X S <id>` | Close scene window (same teardown as Escape) |
| `MVCMD_QUIT` | `Q` | Request close of all windows / quit viewer |
| `MVCMD_WINDOW_TITLE` | `W "<title>"` | Owned title string |
| `MVCMD_BOUNDS` | `B <xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` | Explicit scene AABB; requests camera refit |
| `MVCMD_OBJECT` | `o <id>` | Object id |
| `MVCMD_VERTICES` | `v ["format"] <floats...>` | Optional format; float blob |
| `MVCMD_ELEMENT` | `p` / `l` / `f` `<indices...>` | Points, lines, or facets |
| `MVCMD_COLOR` | `c <id> <r g b>...` | Color id; RGB triples |
| `MVCMD_SELECT_COLOR` | `C <id>` | Active color id |
| `MVCMD_DRAW` | `d <id>` | Object id; optional baked 4×4 matrix |
| `MVCMD_FONT` | `F <id> "<path>" <size>` | Font id, path, size |
| `MVCMD_TEXT` | `T <fontid> "<string>"` | Font id, string; optional matrix |
| `MVCMD_PREPARE` | *(none — appended by parse)* | Upload every open display’s scene to GL |

`S` is find-or-create: a new id opens a window; a repeated id selects that scene as current (does **not** clear). `U S` clears an existing scene’s contents while keeping its window, then selects it. `X S` marks that scene’s window for close (loop tears it down). `Q` marks every window for close; if none are open and the listener is active, emits `window.closed` and stops. `MVCMD_PREPARE` calls `display_prepareall()`, which auto-computes the scene AABB when no explicit `B` was given and fits the camera on first prepare (or after `B`) unless the user has already moved the view.

## ASCII language

Whitespace between tokens is ignored. Prefixes are single letters. Strings use `"..."` with `\` escaping the next character.

### Geometry and drawing

| Letter | Arguments | Notes |
|--------|-----------|-------|
| `S` | `<id> <dim>` | Create or select scene; open window if needed (does not clear) |
| `U` | `S <id>` | Clear scene `id` in place and select it (window kept); see also `TODO.md` for `U O` / `U V` |
| `X` | `S <id>` | Close scene `id` / its window; see also `TODO.md` for `X O` |
| `Q` | — | Quit viewer (close all windows); Morpho `View.close` sends this |
| `W` | `"<title>"` | Set current window title |
| `B` | `<xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` | Explicit scene AABB; next prepare refits unless the user moved the camera |
| `o` | `<id>` | Current object (requires a scene) |
| `v` | `["format"] <floats...>` | Vertex data for current object |
| `p` / `l` / `f` | `<indices...>` | Points / lines / facets |
| `c` | `<id> <r g b>...` | Define color table entry |
| `C` | `<id>` | Select color for subsequent draws |
| `d` | `<id>` | Draw object (matrix from prior transforms if any) |
| `F` | `<id> "<path>" <size>` | Load font |
| `T` | `<fontid> "<string>"` | Draw text (matrix like `d`) |

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
c 0 1 0 0
o 1
v "xnc"
-0.5 -0.5 0  0 0 1  1 0 0
 0.5 -0.5 0  0 0 1  1 0 0
 0.0  0.5 0  0 0 1  1 0 0
l
0 1 1 2 2 0
i
C 0
d 1
```

See also `test/command/linespts`, `test/command/polyhedra`, `test/command/twoscenes`, and `test/command/largebbox` (geometry outside `[-1,1]`; auto-fit).

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
