![Morpho](https://github.com/Morpho-lang/morpho-manual/blob/main/src/Figures/morphologosmall-white.png#gh-light-mode-only)![Morpho](https://github.com/Morpho-lang/morpho-manual/blob/main/src/Figures/morphologosmall-white.png#gh-dark-mode-only)

# Morphoview 

Interactive scientific visualization application for the [morpho](https://github.com/Morpho-lang/morpho) language. 

## Installation

The `specialfn` package can be installed with the [morphopm](https://github.com/Morpho-lang/morpho-morphopm) package manager. Type the following into a terminal:

    morphopm install morphoview

Use morphopm to locate the package and try some of the examples: 

    cd "$(morphopm path morphoview)"
    cd examples
    morpho6 plotripple.morpho

## Usage

Morphoview is typically used together with morpho's `graphics` modules:

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

## Manual Installation

You can also build morphoview manually for development purposes. To do so, first install dependencies, including [GLFW](https://www.glfw.org), [FreeType](https://freetype.org), and [czmq](https://github.com/zeromq/czmq). 

On homebrew: 

    brew install glfw freetype czmq

On apt: 

    sudo apt install libglfw3-dev libfreetype-dev libczmq-dev

Then clone this repository onto your computer in any convenient place:

    git clone https://github.com/morpho-lang/morpho-morphoview.git

and add the location of this repository to your .morphopackages file:

    echo PACKAGEPATH >> ~/.morphopackages
    where PACKAGEPATH is the location of the git repository.

You then need to compile the extension, which you can do by navigating to the repository and typing:

    cmake -S . -B build
    cmake --build build --config Release
    cmake --install build --config Release

The binary is installed locally within the package. 
