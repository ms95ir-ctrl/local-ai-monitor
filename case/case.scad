// Two-part case for the LCDWIKI ES3C28P 2.8" ESP32-S3 touch display module.
// Stack, front to back: shell | screen | board | electronics + speaker | battery | floor.
// Footprint is the board plus walls (about 91 x 55 mm); the battery lies under the board.
//
// HOW TO USE (OpenSCAD 2021.01 or newer):
//   1. Set `part` below. F5 = quick preview, F6 = full render, then File > Export > STL.
//   2. "top_print" and "bottom_print" are already turned the right way up for printing.
//   3. Everything you may want to change is in the parameter section. All lengths are millimetres.
//
// Coordinates: X runs along the 86 mm side, the antenna/mic end is -X and the USB-C end is +X.
// Y runs across the board; +Y is the right-hand side when you look at the screen with the antenna end up
// (the side with the IO, I2C and SPEAKER connectors on the back).
// Z = 0 is the back face of the PCB; the screen is at +Z, the battery at -Z.

part = "assembled";   // "assembled" | "exploded" | "top" | "bottom" | "top_print" | "bottom_print"
section = false;      // assembled view: cut away everything at Y below cut_y to look inside
cut_y = 0;
show_ghosts = true;   // assembled view: show board, screen, battery, speaker, switch and screws

/* ---------- Board (vendor specification V1.0, page 13) ---------- */
board_len      = 86;     // PCB length
board_wid      = 50;     // PCB width
pcb_t          = 1.6;    // PCB thickness
board_corner_r = 3.5;    // PCB corner radius
hole_dx        = 78;     // mounting holes, along the length
hole_dy        = 42;     // mounting holes, across the width
hole_d         = 3.2;    // mounting hole diameter
glass_above_pcb = 4.3;   // touch panel + glue + LCD + glue: 5.90 total from PCB back, minus 1.60 PCB
lcd_len        = 69.2;   // LCD / touch panel size
lcd_wid        = 50.0;
lcd_from_end   = 8.22;   // LCD starts this far from the antenna end
aa_len         = 57.6;   // active display area
aa_wid         = 43.2;
aa_from_end    = 11.12;  // active area starts this far from the antenna end
vis_len        = 58.05;  // visible window of the glass (for reference; the opening is derived from aa_*)
vis_wid        = 43.6;
mic_x_from_end = 4.0;    // microphone hole, from the antenna end
mic_y_from_side= 10.0;   // from the side that has the IO/I2C/SPEAKER connectors on the back
usb_x_edge     = 43;     // USB-C sits at the +X end of the board
usb_z_center   = -1.63;  // receptacle is 3.26 tall on the back of the PCB

/* ---------- Parts you add (MEASURE THESE) ---------- */
bat_len   = 82;    // battery length   (you said "may be 80": verify)
bat_wid   = 40;    // battery width
bat_thick = 7;     // battery thickness
bat_clear = 0.5;   // clearance per side around the battery
spk_len   = 37;    // speaker
spk_wid   = 19;
spk_thick = 5;
sw_len    = 19.5;  // slide switch body (length is the slide direction)
sw_hgt    = 10.6;  // body size along Z when mounted
sw_dep    = 5.7;   // body depth (distance the knob face is from the back of the box)
sw_knob_w = 6;     // knob width along the slide direction
sw_travel = 6;     // knob travel
sw_knob_out = 6;   // knob height above the box face
sw_knob_t = 3.0;   // knob thickness across the slide direction  <-- NOT GIVEN: measure
sw_flap   = 0;     // extra length of the mounting flaps beyond the body, each end  <-- measure

/* ---------- Case ---------- */
fit_clearance = 0.3;   // gap per side between board and case
wall_t        = 2.4;   // outer wall
top_t         = 2.0;   // shell over the screen
floor_t       = 2.0;
back_clear    = 11.0;  // PCB back down to the top of the battery layer (speaker 5 + parts + switch 10.6)
edge_chamfer  = 1.0;   // outer edge chamfer, top and bottom
skirt_t       = 1.0;   // lap joint: skirt thickness (top half overlaps the bottom half)
lap_h         = 2.0;   // lap joint height
lap_clear     = 0.15;
$fn = 48;

/* ---------- Screws (M3 socket or button head, black) ---------- */
head_d        = 5.0;
head_t        = 3.0;
head_extra    = 0.25;  // recess is this much wider than the head
head_recess   = 2.0;   // recess depth, so head_t - head_recess = 1.0 stays proud
screw_hole_d  = 3.4;
sleeve_d      = 7.0;   // tube in the top half around each screw
nut_af        = 5.5;   // M3 nut across flats
nut_t         = 2.4;
nut_clear     = 0.25;
post_d        = 9.0;   // boss in the bottom half

/* ---------- Screen opening (Game Boy style bevel) ---------- */
window_margin = 0.1;   // opening is the active area plus this much per side
bevel_run     = 2.0;   // horizontal run of the bevel through the shell
window_r      = 1.5;

/* ---------- Openings ---------- */
mic_hole_d    = 1.5;
usb_cut_w     = 12.0;  // plug overmould clearance
usb_cut_h     = 6.6;
speaker_port_d = 8;    // placeholder acoustic port, you refine the shape and position
speaker_x     = -9;    // speaker centre along X
speaker_y     = 15.3;  // speaker centre across Y (+Y side); its outer edge rests on the shelf
switch_x      = 22.5;  // switch centre along X, on the +Y wall next to the USB-C end
switch_wall   = 3.0;   // wall thickness at the switch (knob stands out knob_out - this)
vent_w        = 1.6;
vent_pitch    = 5;
vent_n_bat    = 9;     // slots at the battery layer, both long walls
vent_n_conn   = 5;     // slots beside the battery connector, -Y wall

// ======================= derived values, normally leave alone =======================
x_in_max = board_len/2 + fit_clearance;
x_in_min = -x_in_max;
in_hw    = board_wid/2 + fit_clearance;
in_r     = board_corner_r + fit_clearance;
z_glass_top = pcb_t + glass_above_pcb;      // 5.9
z_top       = z_glass_top + top_t;          // outside of the shell
z_p         = pcb_t;                        // parting plane (PCB front face)
z_bat_top   = -back_clear;
bat_pocket_t = bat_thick + bat_clear;
z_floor_top = z_bat_top - bat_pocket_t;
z_bottom    = z_floor_top - floor_t;
bat_pw      = bat_wid + 2*bat_clear;        // pocket width
bat_pl      = bat_len + 2*bat_clear;
holes = [for (sx=[-1,1], sy=[-1,1]) [sx*hole_dx/2, sy*hole_dy/2]];
aa_cx = -board_len/2 + aa_from_end + aa_len/2;
mic_x = -board_len/2 + mic_x_from_end;
mic_y = board_wid/2 - mic_y_from_side;
notch_hw = bat_pw/2 + 0.3;                  // battery exit slot in the -X end wall
sw_face_y = in_hw + 0.9;                    // knob face of the switch box
sw_box_y0 = sw_face_y - sw_dep;
sw_z0 = z_bat_top;                          // box rests on the shelf
sw_ln = sw_len + 2*sw_flap;

module outline(o=0) {
    offset(r = in_r + o) square([2*(x_in_max - in_r), 2*(in_hw - in_r)], center=true);
}
module slab(z, o) { translate([0,0,z]) linear_extrude(0.01) outline(o); }
module body(z0, z1, cb, ct) {
    hull() {
        slab(z0, wall_t - cb);
        slab(z0 + cb, wall_t);
        slab(z1 - ct - 0.01, wall_t);
        slab(z1 - 0.01, wall_t - ct);
    }
}
module rr2(lx, ly, r) { offset(r=r) square([lx-2*r, ly-2*r], center=true); }

// ---------------- top half ----------------
module window_cut() {
    lx = aa_len + 2*window_margin; ly = aa_wid + 2*window_margin;
    translate([aa_cx, 0, 0]) {
        translate([0,0,z_glass_top - 1]) linear_extrude(1.001) rr2(lx, ly, window_r);
        hull() {
            translate([0,0,z_glass_top]) linear_extrude(0.001) rr2(lx, ly, window_r);
            translate([0,0,z_top - 0.001]) linear_extrude(0.001) rr2(lx + 2*bevel_run, ly + 2*bevel_run, window_r + bevel_run);
        }
        translate([0,0,z_top - 0.002]) linear_extrude(1) rr2(lx + 2*bevel_run, ly + 2*bevel_run, window_r + bevel_run);
    }
}
module top_half() {
    difference() {
        union() {
            difference() {
                body(z_p - lap_h, z_top, 0, edge_chamfer);
                translate([0,0,z_p]) linear_extrude(z_glass_top - z_p) outline(0);
                translate([0,0,z_p - lap_h - 0.1]) linear_extrude(lap_h + 0.101) outline(wall_t - skirt_t);
            }
            for (h = holes) translate([h[0], h[1], pcb_t]) cylinder(d=sleeve_d, h=z_glass_top - pcb_t + 0.1);
            translate([mic_x, mic_y, pcb_t]) cylinder(d=4.4, h=z_glass_top - pcb_t + 0.1);   // sound tube
            // plug that closes the battery exit slot in the bottom half
            translate([x_in_min - wall_t, -(notch_hw - 0.15), z_floor_top + 0.1])
                cube([wall_t, 2*(notch_hw - 0.15), z_p - z_floor_top - 0.1 + 0.2]);
        }
        window_cut();
        for (h = holes) {
            translate([h[0], h[1], z_top - head_recess]) cylinder(d=head_d + head_extra, h=head_recess + 1);
            translate([h[0], h[1], pcb_t - 0.1]) cylinder(d=screw_hole_d, h=z_top);
        }
        translate([mic_x, mic_y, pcb_t - 0.1]) cylinder(d=2.4, h=z_glass_top - pcb_t);   // inside of sound tube
        translate([mic_x, mic_y, z_glass_top - 0.2]) cylinder(d=mic_hole_d, h=top_t + 1);
        usb_cut();
    }
}

// ---------------- bottom half ----------------
module usb_cut() {
    translate([x_in_max - 1, -usb_cut_w/2, usb_z_center - usb_cut_h/2])
        cube([wall_t + 3, usb_cut_w, usb_cut_h]);
}
module boss(h) {   // screw boss hanging from the corner walls, above the battery layer
    sx = sign(h[0]); sy = sign(h[1]);
    hull() {
        translate([h[0], h[1], -4]) cylinder(d=post_d, h=4);
        // small block buried in the corner wall, bottom 0.5 above the battery layer
        translate([(sx>0 ? x_in_max : -x_in_max) - 1, (sy>0 ? in_hw : -in_hw) - 1, z_bat_top + 0.5])
            cube([2, 2, 6.5]);
    }
}
module vent_row(n, y_side, x_c, z_c, h) {
    for (i = [0:n-1])
        translate([x_c + (i - (n-1)/2)*vent_pitch - vent_w/2, y_side - 1, z_c - h/2])
            cube([vent_w, wall_t + 2, h]);
}
module bottom_half() {
    nut_d = (nut_af + nut_clear) / cos(30);
    pocket = nut_t + 0.2;
    difference() {
        union() {
            difference() {
                body(z_bottom, z_p, edge_chamfer, 0);
                translate([0,0,z_floor_top]) linear_extrude(z_p - z_floor_top + 1) outline(0);
                translate([0,0,z_p - lap_h]) linear_extrude(lap_h + 1)
                    difference() { outline(wall_t + 1); outline(wall_t - skirt_t - lap_clear); }
            }
            for (h = holes) boss(h);
            // battery rails and +X end stop (the -X end stays open: the battery slides out there)
            for (s = [-1, 1]) translate([-bat_pl/2, s > 0 ? bat_pw/2 : -bat_pw/2 - 1.2, z_floor_top])
                cube([bat_pl, 1.2, 4.5]);
            translate([bat_pl/2 - 0.1, -bat_pw/2 - 1.2, z_floor_top]) cube([1.3, bat_pw + 2.4, 4.5]);
            // shelf along the +Y wall under the speaker and the switch (45 degree underside)
            translate([speaker_x - spk_len/2 - 1.5, 0, 0]) rotate([90,0,90])
                linear_extrude(switch_x + sw_ln/2 + 2.5 - (speaker_x - spk_len/2 - 1.5))
                    polygon([[in_hw + 0.5, z_bat_top], [in_hw + 0.5, z_bat_top - 4.2], [bat_pw/2 + 1.2, z_bat_top]]);
            // speaker end stops on the shelf
            for (x = [speaker_x - spk_len/2 - 1.2, speaker_x + spk_len/2])
                translate([x, bat_pw/2 + 1.2, z_bat_top]) cube([1.2, in_hw - bat_pw/2 - 1.2 + 0.5, 4]);
            // switch cradle: end ribs that hold the box, open towards the inside
            for (s = [-1, 1])
                translate([switch_x + s*(sw_ln/2 + 0.15) + (s>0 ? 0 : -1.6), sw_box_y0 - 0.6, z_bat_top])
                    cube([1.6, sw_face_y - sw_box_y0 + 0.6 + 1.0, sw_hgt - 1.0]);
            // pad on the outside of the +Y wall: wall is switch_wall thick behind the knob
            translate([switch_x - sw_ln/2 - 1.2, in_hw + wall_t - 0.1, z_bat_top])
                cube([sw_ln + 2.4, switch_wall - (wall_t - 0.9) + 0.1 + 0.0, sw_hgt - 1.0]);
        }
        // nut pockets (open towards the PCB so nuts drop in from above) and screw holes
        for (h = holes) {
            translate([h[0], h[1], -pocket]) rotate(30) cylinder(d=nut_d, h=pocket + 0.1, $fn=6);
            translate([h[0], h[1], -pocket - 3.5]) cylinder(d=screw_hole_d, h=3.6);
        }
        usb_cut();
        // battery exit slot, -X end wall, from the floor to the rim
        translate([x_in_min - wall_t - 1, -notch_hw, z_floor_top]) cube([wall_t + 2, 2*notch_hw, z_p - z_floor_top + 1]);
        // acoustic port (placeholder), +Y wall
        translate([speaker_x, in_hw - 1, z_bat_top + spk_thick/2 + 0.2]) rotate([-90,0,0]) cylinder(d=speaker_port_d, h=wall_t + 2);
        // recess for the switch box in the wall, and the knob slot
        translate([switch_x - sw_ln/2 - 0.15, in_hw - 0.1, sw_z0 + 0.0]) cube([sw_ln + 0.3, sw_face_y - in_hw + 0.1, sw_hgt]);
        translate([switch_x - (sw_knob_w + sw_travel + 0.8)/2, sw_face_y - 0.2, sw_z0 + sw_hgt/2 - (sw_knob_t + 0.6)/2])
            cube([sw_knob_w + sw_travel + 0.8, 6, sw_knob_t + 0.6]);
        // ventilation: battery layer on both long walls, and beside the battery connector
        vent_row(vent_n_bat,  in_hw, 0, (z_floor_top + z_bat_top)/2, 4.5);
        vent_row(vent_n_bat, -in_hw - wall_t, 0, (z_floor_top + z_bat_top)/2, 4.5);
        vent_row(vent_n_conn, -in_hw - wall_t, 20, -6, 6);
    }
}

// ---------------- ghosts (not printed) ----------------
module pcb_ghost() {
    color("seagreen") difference() {
        linear_extrude(pcb_t) offset(r=board_corner_r) square([board_len - 2*board_corner_r, board_wid - 2*board_corner_r], center=true);
        for (h = holes) translate([h[0], h[1], -1]) cylinder(d=hole_d, h=pcb_t + 2);
    }
}
module screen_ghost() {
    color("dimgray") translate([-board_len/2 + lcd_from_end + lcd_len/2, 0, pcb_t]) translate([-lcd_len/2, -lcd_wid/2, 0]) cube([lcd_len, lcd_wid, glass_above_pcb]);
}
module battery_ghost() { color("silver") translate([-bat_len/2, -bat_wid/2, z_floor_top + 0.2]) cube([bat_len, bat_wid, bat_thick]); }
module speaker_ghost() { color("black") translate([speaker_x - spk_len/2, speaker_y - spk_wid/2, z_bat_top + 0.2]) cube([spk_len, spk_wid, spk_thick]); }
module switch_ghost() {
    color("crimson") translate([switch_x - sw_len/2, sw_box_y0, sw_z0 + 0.1]) cube([sw_len, sw_dep, sw_hgt - 0.2]);
    color("white") translate([switch_x - sw_knob_w/2, sw_face_y, sw_z0 + sw_hgt/2 - sw_knob_t/2]) cube([sw_knob_w, sw_knob_out, sw_knob_t]);
}
module screws_ghost() {
    for (h = holes) color("black") translate([h[0], h[1], 0]) {
        translate([0,0,z_glass_top]) cylinder(d=head_d, h=head_t);
        translate([0,0,-4.1]) cylinder(d=3, h=z_glass_top + 4.1);
    }
}
module ghosts() { pcb_ghost(); screen_ghost(); battery_ghost(); speaker_ghost(); switch_ghost(); screws_ghost(); }

module assembled(dz=0) {
    color("lightsteelblue") translate([0,0,dz]) top_half();
    color("slategray") bottom_half();
    if (show_ghosts) translate([0,0,dz/2]) ghosts();
}

if (part == "assembled")        difference() { assembled(); if (section) translate([-200,-200 + cut_y,-200]) cube([400,200,400]); }
else if (part == "exploded")    assembled(28);
else if (part == "top")         top_half();
else if (part == "bottom")      bottom_half();
else if (part == "top_print")   translate([0,0,z_top]) rotate([180,0,0]) top_half();     // outer face on the bed
else if (part == "bottom_print") translate([0,0,-z_bottom]) bottom_half();                // floor on the bed
