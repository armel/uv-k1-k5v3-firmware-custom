# Hell Maze

First-person maze shooter on the 1-bit 128x64 LCD: a grid raycaster in
integer arithmetic, textured walls (brick, stone, pillar) dithered by distance,
dotted floor, demons drawn as scaled sprites hidden behind walls, and a gun.

Seven demons roam the maze. One that sees you walks to you and claws when
close (10 health points every half second); two shots kill it. Kill them all
to clear the maze; at 0 health the game is over. Key 5 then starts again.

## Keys

| Key | Action |
|---|---|
| 2 / 8 | Forward / back |
| 4 / 6 | Turn left / right |
| 1 / 3 | Strafe left / right |
| PTT or 5 | Fire (PTT also works while a move key is held) |
| MENU | Help page: each key in a capsule with its action (the game pauses); MENU again to resume |
| EXIT | Quit |

The status bar shows health (H), ammo (A) and kills out of the demon count
(K). Sounds (shot, empty click, death growl, claw, end of game) are tone sweeps
on the speaker.

## Credits

- Raycasting method after Lode Vandevenne's tutorial,
  <https://lodev.org/cgtutor/raycasting.html>, reimplemented in fixed point
  (no code copied).
- Build script, link script, asset builder and the overlay API are Armel
  F4HWN's; the sine-quadrant scheme comes from his Cube3D app and the help
  page layout from CW Keyer.
- Tone register values from the firmware's BK4829 driver
  (`App/driver/bk4829.c`, Dual Tachyon, muzkr).
- The host check mirrors the firmware's text routines (`App/ui/helper.c`,
  Dual Tachyon and contributors) and uses its fonts (`App/font.c`).
- Maze, sprites and gun are original drawings; no id Software code or assets.

## Host check

`test/render.c` draws one view on a computer from a position and a heading
into a PBM image, with a stub API, the demons at their start positions, the
gun and the HUD; a sixth argument draws the help page instead:

    clang -I.. -I../../.. '-D_Static_assert(...)=' '-Dsection(x)=used' -o render render.c
    ./render ../hellmaze_assets.bin 1664 1664 0 view.pbm
