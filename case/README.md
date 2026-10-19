# Case for the ES3C28P (two parts, battery under the board)

A 3D-printable case for the 2.8" LCDWIKI **ES3C28P** ESP32-S3 touch display module, designed in
OpenSCAD. It is the case for the [`es3c28p-320x240`](../firmware/es3c28p-320x240) firmware variant.

**Status: designed and checked on screen, never printed.** Print the fit-test frame first.

![Assembled](img/assembled.png)

## What it is

Two printed parts held together by four M3 screws, about **91.4 x 55.4 x 28.4 mm** (the screw heads add
1 mm, the switch pad adds 1.5 mm on one side).

Front to back: 2 mm shell with a bevelled screen opening, screen, board, an 11 mm cavity (speaker and
slide switch), the battery, floor.

![Exploded](img/exploded.png)

- **Top half** holds the screen and the board. The opening has a 2 mm Game Boy style bevel, wider at the
  outer surface and narrowing to the glass, where it covers the bezel (opening = active area + 0.1 mm per
  side). Four screw recesses are 0.25 mm wider than the heads, 2 mm deep, so 1 mm of each head stands
  proud. A 1.5 mm hole above the microphone.
- **Bottom half** holds the battery, speaker and switch. Each screw lands in a hexagonal nut pocket that
  opens upward: drop an M3 nut in, add a dab of glue, then fit the board. Slide switch on the +Y wall next
  to the USB-C end, wall 3 mm thick there so the knob stands 3 mm proud. An 8 mm placeholder speaker port
  on the same wall. USB-C cutout, vent slots at the battery layer and beside the board's battery connector.
- **Joint:** the top half has a 1 mm skirt that laps over a step in the bottom half's wall.

![Cross-section](img/sec_y0.png)

## Why the battery slides out of one end

The mounting holes are 42 mm apart and the battery is 40 mm wide, so a screw sits only 1 mm outside the
battery edge. Full-height screw posts would hit the battery at all four corners. So the four screw
bosses hang from the corner walls **above** the battery layer, and the battery lies under them.

That means you cannot lift the battery straight out. Instead the bottom half has a slot in the antenna
(-X) end wall, and the top half carries a plug that fills it. Undo the four screws, lift the top half
off, and the battery slides out of the open end.

![Bottom half](img/bottom_in.png)

## Parts you need

| Part | Notes |
|---|---|
| 4 x M3 x 10 mm screws, black, 5 mm head, 3 mm thick | length computed: head seat to the bottom of the nut is 8.5 mm |
| 4 x M3 nuts | 5.5 mm across flats, 2.4 mm thick |
| Battery 40 x 82 x 7 mm | dimensions are placeholders: measure yours |
| Speaker 37 x 19 x 5 mm | held with foam tape |
| Slide switch 19.5 x 10.6 x 5.7 mm | knob thickness and flap size are placeholders |

## Printing

Files are already turned the right way up:

- `case_top_print.stl`: face-down, the outer surface on the bed. The bevel is 45 degrees, so no supports.
- `case_bottom_print.stl`: floor on the bed.

Suggested: 0.2 mm layers, 3 perimeters, 15-20% infill, no supports. Both halves are solid manifolds in
OpenSCAD. Long bridges are the 12 mm USB-C and switch-slot cutouts; the screw bosses have a sloped
underside.

## Changing the design

Open `case.scad` in [OpenSCAD](https://openscad.org) (Windows, macOS and Linux builds exist; 2021.01 or
newer). Every dimension is a named variable at the top. Set `part`, press F5 to preview, F6 to render, then
File > Export > STL.

| `part` | Shows |
|---|---|
| `assembled`, `exploded` | everything, with board, screen, battery, speaker, switch and screws as ghosts |
| `top`, `bottom` | one half in design orientation |
| `top_print`, `bottom_print` | one half in print orientation, for export |

Set `section = true` and `cut_y` to cut the assembled view open.

`check.scad` runs 19 collision checks (screen, board, battery, speaker, switch, screws and the tallest
back-of-board parts against both halves). All pass. I verified the checker catches a deliberate collision.

```bash
openscad -D 'check="battery_bottom"' -D 'part="none"' -o /tmp/x.stl check.scad
# "Current top level object is empty" means no overlap, which is the wanted result
```

## What to verify before printing

1. **Fit-test frame** (`fit_test.scad`): print it and check the board drops in. Adjust `fit_clearance`.
2. **Battery:** measure length, width, thickness and where its connector is.
3. **Switch:** the knob thickness (`sw_knob_t`, now 3 mm) and the mounting flap size (`sw_flap`, now 0)
   were not given.
4. **Back of the board:** component positions are read from the vendor drawing and are approximate. The
   cavity is 11 mm; the tallest part is 4.7 mm. Check the switch zone (+Y edge, X 12 to 33 mm) and the
   USB-C position (assumed flush with the board edge) on the real board.
5. **Antenna:** the battery sits below the ESP32's PCB antenna. A metal-foil pouch can weaken Wi-Fi.
   Test signal strength.

## Left out, and why

- **SD card opening:** you said it is not needed.
- **BOOT and RESET access:** both buttons press from the back of the board, and the battery is directly
  below them, so a pin cannot reach them. The ESP32-S3 normally flashes over USB without them; if you
  need them, open the case.

## Other files

`fit_test.scad`, `fit_test.stl`, `fit_test.png`: the thin fit-test frame (board pocket and the four
holes). Print flat, no supports.
