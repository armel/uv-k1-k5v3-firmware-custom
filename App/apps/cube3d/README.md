# Cube3D

Cube3D renders rotating 3D shapes on the 128x64 monochrome display. Geometry
and lookup tables live in the app assets; the overlay uses fixed
point arithmetic and performs no hardware floating-point or division.

The `GLOBE` scene has a dedicated renderer with latitude/longitude lines and
a stable circular limb. In wire mode its far side is
dotted; in solid mode it is hidden. Shape 10, `DODECA`, is reached with `*`
after the globe because the numeric keypad directly selects shapes 1 through 9.

## Keys

| Key | Action |
|---|---|
| 1-9 | Select a shape directly (`9` selects the globe) |
| * | Next shape |
| UP / DOWN | Increase / decrease rotation speed |
| F then UP / DOWN | Zoom in / out (eight levels) |
| F then * | Toggle wire / solid hidden-line rendering (`W` / `S`) |
| 0 | Reset the orientation and zoom |
| MENU | Pause / resume |
| EXIT | Quit |

The status bar shows the shape name, the armed `F`, and `PAUSE` when stopped.
