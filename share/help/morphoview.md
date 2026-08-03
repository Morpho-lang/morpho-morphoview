[comment]: # (MorphoView package help)
[version]: # (0.6)

# MorphoView
[tagmorphoview]: # (morphoview)
[tagview]: # (View)

The `morphoview` package provides interactive 3D visualization through the external `morphoview` application. Import the package modules:

    import morphoview
    import xgraphics
    import color

There are two ways to display graphics:

* **Fire-and-forget** — build a `Graphics` object and call `Show(g)`. The viewer opens, then exits when you close the window.
* **Live session** — build a `Scene`, open a `View`, and update with `move` / `recolor` / etc. The viewer stays connected over ZeroMQ.

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

    var g = Scene()
    var id = g.display(Sphere([0,0,0], 1), [0,0,0], color=Red)
    var v = View(g)
    g.move(id, [0.1, 0, 0])
    v.wait()

Requires `morpho-zeromq`. Low-level viewer ASCII commands are documented in the package `docs/commandapi.md`.

[showsubtopics]: # (subtopics)

## Show
[tagshow]: # (Show)

`Show` launches morphoview with a temporary draw file (`-t`). Use it for one-shot display:

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

You can also serialize without launching:

    var show = Show()
    show.write(g, out)   // any object with write(line)

Set `show.replace = true` so the preamble emits `U S` (in-place scene replace) instead of `S` — used by `View.update(Graphics)`.

## View
[tagView]: # (View)

`View` keeps a duplex ZeroMQ session with a running morphoview process. Drive it with `poll` / `System.sleep` — Morpho has no async runtime.

Open immediately from a `Graphics` or `Scene`:

    var v = View(g)              // throws on failure
    var v = View()
    v.open(g)                    // or an ASCII command string

For a `Scene`, `open` also registers as a listener so later `move` / `recolor` / `remove` / `replace` events are forwarded to the viewer.

### Methods

* `open(commands)` / `open(Graphics)` / `open(Scene)` — bind, spawn viewer, send first chunk, wait for `ok`
* `update(commands)` / `update(Graphics)` / `update(Scene)` — send another chunk (`Graphics` uses full `U S` replace)
* `morph(id, item)` — same-length vertex push (`U V`); use `replace` for new connectivity
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

Optional constructor args: `title=`, `background=` (a `Color`).

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
* `recolor(id, color)` — set presentation color
* `remove(id)` — remove the entry
* `replace(id, item)` — swap item (full redefine in the viewer)
* `morph(id, item)` — same-length mesh swap; pair with `View.morph` or `View.refreshMesh`
* `beginBatch()` / `endBatch()` — coalesce Moved/Recolored notifies (via Broadcaster)

Mutators return `true`/`false`. Prefer `View(Scene)` for live sessions.

## Primitives
[tagprimitives]: # (primitives)

Graphical elements for `display` (from `xgraphics`):

* `Sphere(center, r, color=, transmit=, filter=, maxrefine=)` — unit mesh + entry pose
* `Cylinder(start, end, aspectratio=, n=, color=, …)` — unit shaft + start→end as pose
* `Arrow(start, end, aspectratio=, n=, color=, …)` — unit shaft+tip + start→end as pose
* `Text(string, posn=, font=, color=, …)` — draw-slot text; move/remove like meshes; string/font via `replace`
* `TriangleComplex(position, normals, colors, connectivity, …)` — triangle mesh
* `PointCloud` / `LineSet` — points and lines with entry pose
* `Tube` — tube along a path

`transmit` / `filter` map to viewer alpha (`alpha = 1 − clamp(transmit + filter, 0, 1)`). True POVRay filter-vs-transmit pigment behavior remains POVRay-only.
