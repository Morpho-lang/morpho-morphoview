[comment]: # (MorphoView package help)
[version]: # (0.7)

# MorphoView
[tagmorphoview]: # (morphoview)
[tagview]: # (View)

The `morphoview` package provides interactive 3D visualization through the external `morphoview` application. Use this package together with `graphics` and `plot` for visualization:

    import graphics
    import morphoview

There are two ways to display graphics: 

The `Show` class provides a static view of a `Graphics` or `Scene`: 

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

The `View` class provides a live, updatable view of a `Scene`: 

    var g = Scene()
    var id = g.display(Sphere([0,0,0], 1), [0,0,0], color=Red)
    var v = View(g)
    g.move(id, [0.1, 0, 0])
    v.wait()

[showsubtopics]: # (subtopics)

## Show
[tagshow]: # (Show)

`Show` launches the morphoview application to show a `Graphics` or `Scene` object: 

    import graphics, morphoview

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

The view is static and cannot be updated; use `View` for interactive display. 

## View
[tagView]: # (View)

`View` launches the morphoview application to show a `Graphics` or `Scene` object: 

    var v = View(g)              // throws on failure
    var v = View()

The view can be updated:

* a `View` automatically detects changes to the `Scene` and updates the display. 

* you can replace the contents of a `View` using the `update` method.

    v.update(newGraphics)
