# Case (work in progress)

OpenSCAD parts for a 3D-printed case. This is a separate project from the monitor above: it is for the
2.8" **ES3C28P** ESP32-S3 touch display module (86 x 50 mm board, 78 x 42 mm mounting holes), not the
display the dashboard firmware drives. Every board dimension comes from the vendor's specification
PDF, page 13, and sits in a named variable at the top of the file.

| File | What it is |
|---|---|
| `fit_test.scad` | Thin fit-test frame: a pocket for the board plus the four screw holes. Print it first. |
| `fit_test.stl` | Ready-to-print export. |
| `fit_test.png` | Preview render. |

Not done yet: the two-part case itself (screen bevel, screw posts, battery bay, speaker, switch, vents).

Regenerate the files with OpenSCAD:

```bash
openscad -o fit_test.stl fit_test.scad
xvfb-run -a openscad -o fit_test.png --imgsize=1000,700 --camera=0,0,0,40,0,25,170 fit_test.scad
```

Print flat, no supports. If the board is loose or tight, adjust `fit_clearance` (0.3 mm per side).
The fit-test frame has not been test-printed yet.
