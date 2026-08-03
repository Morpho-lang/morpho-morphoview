# morpho-morphoview

Interactive viewer application for `morpho`.

## Installation

To install this, clone this repository onto your computer in any convenient place:

    git clone https://github.com/morpho-lang/morpho-morphoview.git

then add the location of this repository to your .morphopackages file.

    echo PACKAGEPATH >> ~/.morphopackages
    where PACKAGEPATH is the location of the git repository.

You need to compile the extension, which you can do by navigating to the repository and typing:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make install

You may need to use `sudo`. Dependencies include GLFW, FreeType, and [czmq](https://github.com/zeromq/czmq) (`brew install czmq` / `apt install libczmq-dev`).

## Usage

    import morphoview
    import xgraphics
    import color

Fire-and-forget display:

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

Live session (ZeroMQ duplex; drive with `poll` / `wait`):

    var g = Scene()
    var id = g.display(Sphere([0,0,0], 1), [0,0,0], color=Red)
    var v = View(g)
    g.move(id, [0.1, 0, 0])
    v.wait()

Online help: `help morphoview` (package file in `share/help/`).

Viewer command language (ASCII / C API / ZMQ): [`docs/commandapi.md`](docs/commandapi.md). Architecture: [`docs/definedraw.md`](docs/definedraw.md). Backlog: [`TODO.md`](TODO.md).

Viewer CLI: `-b <endpoint>` binds a ZMQ PAIR socket; `-c <endpoint>` connects (used by `View`). `-t` unlinks a temp draw file on exit (used by `Show`).
