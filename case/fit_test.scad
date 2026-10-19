// Fit-test frame for the ES3C28P 2.8" ESP32-S3 touch display board.
// Print this first (about 15 minutes) and check the real board drops in and the screws line up.
// All board values come from ES3C28P_ES2N28P_Specification_V1.0.pdf, page 13.
// Orientation: X runs along the 86 mm side, Y along the 50 mm side. Origin = centre of the board.

// ---- Board (from the PDF) ----
board_len        = 86;     // PCB length (PDF: 86.00)
board_wid        = 50;     // PCB width  (PDF: 50.00)
board_thick      = 1.6;    // PCB thickness (PDF side view: 1.60)
board_corner_r   = 3.5;    // PCB corner radius (PDF: R3.50)
hole_dx          = 78;     // mounting hole spacing along the length (PDF: 78.00)
hole_dy          = 42;     // mounting hole spacing across the width (PDF: 42.00)
hole_d           = 3.2;    // mounting hole diameter (PDF: 3.20)

// ---- Printing ----
fit_clearance    = 0.3;    // gap per side between board and pocket wall
hole_extra       = 0.3;    // extra on the hole diameter so an M3 screw slides through

// ---- Fit-test frame ----
margin           = 3;      // frame wall width around the board
floor_t          = 0.8;    // thickness under the board
window_inset     = 7;      // the floor is cut out this far inside the board edge so you can see through
window_corner_r  = 3;
$fn              = 48;

pocket_len = board_len + 2*fit_clearance;
pocket_wid = board_wid + 2*fit_clearance;

module rrect(l, w, r, h) {
    linear_extrude(h)
        offset(r) square([l - 2*r, w - 2*r], center = true);
}

difference() {
    // outer plate
    rrect(pocket_len + 2*margin, pocket_wid + 2*margin, board_corner_r + margin, floor_t + board_thick);
    // board pocket
    translate([0, 0, floor_t]) rrect(pocket_len, pocket_wid, board_corner_r, board_thick + 1);
    // viewing window
    translate([0, 0, -1]) rrect(board_len - 2*window_inset, board_wid - 2*window_inset, window_corner_r, floor_t + 2);
    // four screw holes
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx*hole_dx/2, sy*hole_dy/2, -1]) cylinder(d = hole_d + hole_extra, h = floor_t + 2);
}
