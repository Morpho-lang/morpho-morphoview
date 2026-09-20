# Morphoview command language

The morphoview application is driven by an ASCII command 
language, which forms an intermediate representation (IR). The ASCII form below is the public encoding: single-letter prefixes, whitespace-separated tokens, and `"..."` strings (`\` escapes the next character).

The morpho graphics system comprises a number of packages, including public packages intended for users: 

* `graphics` provides `Graphics` and `Scene` together with a number of primitives. These are container objects representing a graphical scene. `Graphics` should be used for static scenes; `Scene` is used for interactive and animatable scenes. This package is part of morpho's standard distribution.

* `morphoview` provides `Show` and `View` to display graphics objects in a static or interactive viewer respectively. It is provided with the `morphoview-opengl` package.

There are two private packages intended for those implementing viewer applications:

* `graphicsserializer` is part of morpho's standard distribution. It is not intended for direct use by the user, but could be used by those implementing a viewer application to serialize `Graphics` and `Scene` into the morphoview IR.

* `morphoviewclient` is provided with this package. It is not intended for direct use by the user, but could be used by those implementing a viewer application; it provides a generic transport-independent protocol for driving viewer applications.

## Model

A **scene** has a dimension (2 or 3), a window title, bounds, background color, and lighting. It holds:

* **Objects** — vertex data and indexed points, lines, or facets.
* **Color tables** — named RGB or RGBA palettes.
* **Fonts** — named typefaces for text.
* **Draw-slots** — items on the display list. An object draw references an object and a pose; a text draw references a font and a string.

The current scene, object, color, material, and transform persist until they are changed. A later command stream may omit `S` and still target the last selected scene.

World space is right-handed: **+X right, +Y up, +Z toward the home viewer**.

## Commands

| Command | Arguments | Meaning |
|---------|-----------|---------|
| `S` | `<id> <dim>` | Create or select a scene. A new id creates a scene; a repeated id selects it (does not clear). |
| `U S` | `<id>` | Clear an existing scene in place and select it. |
| `U O` | `<id>` | Clear one object’s geometry so it can be redefined; select it as current. |
| `U V` | `<id> ["format"] <floats…>` | Replace an object’s vertices. Length and layout must match the existing data. |
| `X S` | `<id>` | Close that scene. |
| `X O` | `<id>` | Delete an object and any object draws that reference it. |
| `X D` | `<drawId>` | Delete one draw-slot; leave the object. |
| `Q` | — | Close all scenes. |
| `W` | `"<title>"` | Set the current scene’s window title. |
| `B` | `<xmin> <xmax> <ymin> <ymax> <zmin> <zmax>` | Set an explicit axis-aligned bounding box. |
| `L` | see [Lighting](#lighting) | Replace the scene’s lighting. |
| `G` | `<r> <g> <b>` | Set the background color. |
| `o` | `<id>` | Create or select the current object (requires a scene). |
| `v` | `["format"] <floats…>` | Vertex data for the current object. |
| `p` / `l` / `f` | `<indices…>` | Indexed points, lines, or facets on the current object. |
| `c` | `<id> <r g b [a]>…` | Define a color table. |
| `C` | `<id>` or (none) | Select the color for subsequent draws. Bare `C` clears a uniform override so vertex colors show. |
| `M` | `flat` \| `shaded` [`<ka> <kd>` [`<ks>` [`<n>`]]] | Material for subsequent draws. |
| `d` | `<drawId>` \| `<drawId> <objectId>` | Create or update an object draw-slot. |
| `D` | — | Clear the display list. Objects, colors, and fonts remain. |
| `F` | `<id> "<path>" <size>` | Load a font. |
| `T` | `<fontid> "…"` \| `<drawId> <fontid> "…"` | Legacy text append, or create/update a text draw-slot. |

A bare `L` is invalid.

## Draw-slots

`d <drawId> [objectId]` places an object on the display list. The object defaults to `drawId` if only one argument is given. If a slot with that id already exists, the command updates it in place (pose and, if `C` preceded this `d`, color) rather than appending.

* A `d` with a current transform sets the slot’s pose.
* A `d` with no transform and no preceding `C` leaves pose and color mode unchanged.
* Recolor is `C` then `d`.
* Bare `C` then `d` clears the uniform color override.

`T <drawId> <fontid> "…"` is the text equivalent (pose like `d`). `T <fontid> "…"` appends text without a stable draw id.

## Vertices

`v` and `U V` take an optional format string. Letters name fields in order:

| Letter | Field | Size |
|--------|-------|------|
| `x` | position | `dim` floats |
| `n` | normal | `dim` floats |
| `c` | RGB | 3 floats |
| `a` | alpha | 1 float |

Missing `c` / `n` / `a` default to white, +z, and alpha 1. Typical layouts: `xn`, `xnc`, `xnca`, `x`, `xc`, `xca`.

`U V` requires the same number of floats (and the same layout) as the object already has.

## Color

`c <id> …` defines a table. The float count infers layout: RGB triples, or RGBA if the count is 4 or a multiple of 4 that is not a multiple of 3. **12 floats is ambiguous** (4 RGB vs 3 RGBA) and is treated as RGB.

`C <id>` is a uniform color on the next draw-slot. Vertex alpha is unchanged. Bare `C` then `d` restores vertex colors.

A color with alpha, or a vertex format that includes `a`, is translucent.

## Material

* `M shaded` (default) — Phong / Lambert. Defaults \(k_a=k_d=0.5\), \(k_s=0\). Optional floats override \(k_a\), \(k_d\), \(k_s\), and shininess \(n\).
* `M flat` — unlit albedo.

Shaded intensity:

\[
I = \text{albedo}\,\Bigl(k_a\,\text{ambient}
    + \sum_i C_i\bigl(k_d \max(\mathbf{N}\cdot\mathbf{L}_i,0) + k_s (\mathbf{R}_i\cdot\mathbf{V})^n\bigr)\Bigr)
\]

Ambient is white. Lamps contribute diffuse and specular only. Lighting is evaluated in view space; for an orthographic view \(\mathbf{V}=(0,0,1)\).

## Lighting

One `L` replaces the whole list. If `L` is omitted, lighting is Neutral.

* `L "neutral"` / `L "auto"` — Neutral: three white view-space directionals plus ambient. `"auto"` is an alias for `"neutral"`.
* `L "threepoint"` — key / fill / rim, also view-relative.
* `L <n> "x" <posn>…` — n world-space point lights, white. At most 4.
* `L <n> "xc" <posn> <color>…` — same, with RGB per lamp.
* `L "off"` / `L 0` — ambient only.

## Transforms

`i`, `m`, `r`, `s`, and `t` set a current model transform. They are not scene objects. The current transform is applied to the next `d` or `T` that carries a pose, then left unchanged until `i`.

| Command | Arguments | Effect |
|---------|-----------|--------|
| `i` | — | Identity |
| `m` | 16 floats | Right-multiply by 4×4 (`model = model * X`) |
| `r` | `<phi> <ax ay az>` | Rotate about an axis (left-multiply) |
| `s` | `<scale>` or `<sx sy sz>` | Uniform or per-axis scale (left-multiply) |
| `t` | `<tx ty tz>` | Translate (left-multiply) |

`s` / `r` / `t` left-multiply so commands written in application order compose as usual (`t` after `s` → scale then translate). `m` right-multiplies so a matrix between basis and translation stays in local space: `i` / `s` / `r`, then `m`, then `t` yields `T·R·S·X`.

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
