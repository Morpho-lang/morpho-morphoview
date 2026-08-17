[comment]: # (MorphoView package help)
[version]: # (0.6)

# MorphoView
[tagmorphoview]: # (morphoview)
[tagview]: # (View)

The `morphoview` package provides interactive 3D visualization through the external `morphoview` application. Import the package modules:

    import morphoview

There are two ways to display graphics. Static `Show` does not need ZeroMQ (`import xshow` is enough); `import morphoview` pulls `Show` via `xshow` plus live `View`.

* **Display only** — build a `Graphics` object and call `Show(g)`. The viewer opens, then exits when you close the window.
* **Live session** — build a `Scene`, open a `View`, and update with `move` / `recolor` / etc. The viewer stays connected over ZeroMQ.

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

    var g = Scene()
    var id = g.display(Sphere([0,0,0], 1), [0,0,0], color=Red)
    var v = View(g)
    g.move(id, [0.1, 0, 0])
    v.wait()

Requires `morpho-zeromq` package. Low-level viewer ASCII commands are documented in the package `docs/commandapi.md`.

[showsubtopics]: # (subtopics)

## Show
[tagshow]: # (Show)

`Show` (module `xshow`) launches morphoview with a temporary draw file (`-t`). Use it for one-shot display:

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

You can also serialize without launching:

    var show = Show()
    show.write(g, out)   // any object with write(line)

Set `show.replace = true` so the preamble emits `U S` (in-place scene replace) instead of `S` — used by `View.update(Graphics)`.

## View
[tagView]: # (View)

Open immediately from a `Graphics` or `Scene`:

    var v = View(g)              // throws on failure
    var v = View()
    v.open(g)                    // or an ASCII command string

View makes use of the `Listener` protocol to track changes in a `Scene`.  

### Methods

* `open(commands)` / `open(Graphics)` / `open(Scene)` — bind, spawn viewer, send first chunk, wait for `ok`. Parse failures come back as `err` plus a line/char message; `lastErr` holds the reportable string. The `View(g)` constructor throws `VwOpnFl` with that detail.
* `update(commands)` / `update(Graphics)` / `update(Scene)` — send another chunk (`Graphics` uses full `U S` replace)
* `morph(id, item)` — same-layout vertex push (`U V` for `xn`/`xnc`/`xnca`); a layout change falls back to a full replace
* `refreshMesh(id)` — push `U V` for an existing TriangleComplex entry
* `redraw(commands)` — clear draws (`D`) and re-issue draw ASCII (tests / escape hatch)
* `write(line)` — File-compatible sink for `Show.write`
* `poll(timeoutms=0)` — one reply, or `nil` on timeout
* `wait(sessiontimeout=0)` — spin until the window closes (0 = forever)
* `close()` — send `Q`, wait for `window.closed`; idempotent

Occasional full refresh uses `update(Graphics)`. Efficient animation uses a `Scene` and `g.move` after `View(g)`.

## Graphics
[tagGraphics]: # (Graphics)

`Graphics` is a static displaylist of id-bearing entries. Create one and add elements with `display`:

    var g = Graphics()
    var id = g.display(Sphere([0,0,0], 1, color=Red))

`display` returns a stable Graphics-owned id (`Int`), or `nil` on failure. Optional pose:

    g.display(item, position, scale=, rotate=, color=, flat=)

* `position` — list or Matrix (absolute placement)
* `scale` — float or `[sx, sy, sz]`
* `rotate` — `[angle, ax, ay, az]` or `nil`
* `color` / `flat` — presentation color and unlit shading

Combine two Graphics objects with `+` / `add` (left-hand ids kept; right-hand ids remapped).

Optional constructor args: `title=`, `background=` (a `Color`), `light=`.

Lighting is a list of `Light` on `Graphics` / `Scene`. An empty list (`nil`, `[]`, or `resetLights()`) is camera-relative **Neutral** — Show writes no `L` line (a bare `L` is not valid). Named values: `"neutral"` / `"auto"` (same Neutral rig, emitted as `L "neutral"`), `"threepoint"` (studio key/fill/rim), `"off"` (ambient only, `L "off"`).

    var g = Graphics()
    g.setLights([[10, 10, 10], Light([-10, 5, 8], color=Red, intensity=0.4)])
    g.addLight([0, 5, 10])
    g.resetLights()

A 3-vector (`Matrix`, `[x,y,z]`, `(x,y,z)`) is one white lamp; a list of those (or of `Light`) is several (at most 4). `Light` has `position`, `color` (default `White`), and `intensity` (default `1`); the viewer RGB is `color * intensity`. `setLights` replaces the whole list (same values as `Graphics.light`). The first `addLight` leaves Neutral because the list is no longer empty; `"threepoint"` / `"off"` are replaced by a new list. Unknown names and over-long lists error rather than silently falling back.

`import xgraphics` also provides `Light`, `setLights` / `addLight` / `resetLights` (on `Graphics`), and `cross3D(a, b)` (3-vector cross product).

## Scene
[tagScene]: # (Scene)

`Scene` is a live `Graphics` with a Broadcaster. Mutations notify listeners (including an attached `View`):

    var g = Scene()
    var id = g.display(Sphere([0,0,0], 1), [0,0,0], color=Red)
    var v = View(g)
    g.move(id, [0.1, 0, 0])
    g.recolor(id, Blue)

### Methods

* `move(id, position, scale=, rotate=)` — set position; omitted scale/rotate leave that component unchanged
* `recolor(id, color)` — set presentation color; `recolor(id, nil)` clears the override
* `remove(id)` — remove the entry
* `replace(id, item)` — swap item (full redefine in the viewer)
* `morph(id, item)` — same-length mesh swap; `View.morph` uses `U V` when the vertex layout matches
* `setLights(...)` / `addLight(...)` / `resetLights()` — replace the whole lighting, append a world-space `Light` (or position/`color=`/`intensity=`), or restore Neutral; live `View` sends `L`
* `beginBatch()` / `endBatch()` — coalesce Moved/Recolored/Lights notifies (via Broadcaster)

Mutators return `true`/`false`. Prefer `View(Scene)` for live sessions.

## Primitives
[tagprimitives]: # (primitives)

Graphical elements for `display` (from `xgraphics`):

* `Sphere(center, r, color=, transmit=, filter=, maxrefine=)` — unit mesh + entry pose
* `Cylinder(start, end, aspectratio=, n=, color=, …)` — unit shaft + start→end as pose
* `Arrow(start, end, aspectratio=, n=, color=, …)` — unit shaft+tip + start→end as pose
* `Text(string, posn=, font=, color=, …)` — draw-slot text; move/remove like meshes; string/font via `replace`
* `TriangleComplex(position, normals, color, connectivity, …)` — triangle mesh
* `PointCloud` / `LineSet` — points and lines with entry pose
* `Polygon` — convex planar n-gon; winding defines the normal; tessellates to triangles
* `Tube` — tube along a path

All of these are `GraphicsPrimitive`s sharing `color`, `filter`, and `transmit`.

`transmit` / `filter` map to viewer alpha (`alpha = 1 − clamp(transmit + filter, 0, 1)`). True POVRay filter-vs-transmit pigment behavior remains POVRay-only.

Every `Color` has an alpha channel (`Color(r,g,b)` sets `a=1`; use `Color(r,g,b,a)` for translucency). Uniform alpha is sent as draw-slot `C`; a `ColorTable` with a fourth row is sent as per-vertex `a` (`xnca` / `xca`).
