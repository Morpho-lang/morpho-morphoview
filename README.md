# Morphoview 

Interactive scientific visualization application for the [morpho](https://github.com/Morpho-lang/morpho) language. 

## Installation

To install this, clone this repository onto your computer in any convenient place:

    git clone https://github.com/morpho-lang/morpho-morphoview.git

then add the location of this repository to your .morphopackages file.

    echo PACKAGEPATH >> ~/.morphopackages
    where PACKAGEPATH is the location of the git repository.

You need to compile the extension, which you can do by navigating to the repository and typing:

    cmake -S . -B build
    cmake --build build --config Release
    cmake --install build --config Release

The binary is installed locally within the package. Dependencies include [GLFW](https://www.glfw.org), [FreeType](https://freetype.org), and [czmq](https://github.com/zeromq/czmq) (`brew install czmq` / `apt install libczmq-dev`).

## Usage

    import morphoview
    import graphics
    import color

Static display of a `Graphics` or `Scene`: 

    var g = Graphics()
    g.display(Sphere([0,0,0], 1, color=Red))
    Show(g)

Interactive, live updatable display of a `Scene`: 

    var g = Scene()
    var id = g.display(Sphere([0,0,0], 1), [0,0,0], color=Red)
    var v = View(g)
    g.move(id, [0.1, 0, 0])
    v.wait()

Online help: `help morphoview` (package file in `share/help/`).

Viewer command language: [`docs/commandapi.md`](docs/commandapi.md).

Viewer terminal app command line switches:
* `-v` / `--version` displays a version string. 
* `-b <endpoint>` binds morphoview to a ZeroMQ PAIR socket
* `-c <endpoint>` connects to a ZeroMQ endpoint (used by `View`).
* `-t` unlinks a temp draw file on exit (used by `Show`).
