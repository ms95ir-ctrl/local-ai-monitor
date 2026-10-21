// Thin two-part case for the LCDWIKI ES3C28P 2.8" ESP32-S3 touch display module: just the board, USB powered.
// Stack, front to back: shell | screen | board | 5 mm cavity for the connectors | floor.
// Footprint is the board plus walls (about 91 x 55 mm); thickness about 14.4 mm.
//
// HOW TO USE (OpenSCAD 2021.01 or newer):
//   1. Set `part` below. F5 = quick preview, F6 = full render, then File > Export > STL.
//   2. "top_print" and "bottom_print" are already turned the right way up for printing.
//   3. Everything you may want to change is in the parameter section. All lengths are millimetres.
//
// Coordinates: X runs along the 86 mm side, the antenna/mic end is -X and the USB-C end is +X.
// Y runs across the board; +Y is the side with the IO, I2C and SPEAKER connectors on the back.
// Z = 0 is the back face of the PCB; the screen is at +Z, the floor at -Z.

part = "assembled";   // "assembled" | "exploded" | "top" | "bottom" | "top_print" | "bottom_print"
section = false;      // assembled view: cut away everything at Y below cut_y to look inside
cut_y = 0;
show_ghosts = true;   // assembled view: show board, screen and screws

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
mic_x_from_end = 4.0;    // microphone hole, from the antenna end
mic_y_from_side= 10.0;   // from the side that has the IO/I2C/SPEAKER connectors on the back
usb_z_center   = -1.63;  // receptacle is 3.26 tall on the back of the PCB
tallest_back   = 4.7;    // tallest part on the back of the PCB (the connectors)
btn_x          = 39.8;   // BOOT (-Y) and RESET (+Y) buttons on the back, centre
btn_y          = 11.6;
btn_top        = -1.5;   // top of the buttons, measured from the back of the PCB
u1_x0 = 13; u1_x1 = 28; u1_y0 = 8.8; u1_y1 = 20.4;   // ESP32 module on the back (the warmest part)

/* ---------- Case ---------- */
fit_clearance = 0.3;   // gap per side between board and case
wall_t        = 2.4;   // outer wall
top_t         = 2.0;   // shell over the screen
floor_t       = 1.5;
back_clear    = 5.0;   // PCB back down to the floor: tallest part 4.7 + 0.3
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
post_d        = 9.0;   // post in the bottom half
blind_below   = 2.0;   // screw hole continues this far below the nut pocket

/* ---------- Screen opening (Game Boy style bevel) ---------- */
window_margin = 0.1;   // opening is the active area plus this much per side
bevel_run     = 2.0;   // horizontal run of the bevel through the shell (= top_t: 45 degrees, prints without support)
window_r      = 1.5;

/* ---------- Openings ---------- */
mic_hole_d    = 1.5;
usb_cut_w     = 12.0;  // plug overmould clearance
usb_cut_h     = 6.6;
pin_hole_d    = 2.2;   // BOOT / RESET: a paperclip or SIM pin goes through the floor and the guide tube
guide_od      = 4.4;
guide_gap     = 0.4;   // the guide tube stops this far below the button
vent_w        = 1.6;   // floor slots under the ESP32 module
vent_pitch    = 3.0;
vent_n        = 3;

// ======================= derived values, normally leave alone =======================
x_in_max = board_len/2 + fit_clearance;
x_in_min = -x_in_max;
in_hw    = board_wid/2 + fit_clearance;
in_r     = board_corner_r + fit_clearance;
z_glass_top = pcb_t + glass_above_pcb;      // 5.9
z_top       = z_glass_top + top_t;          // outside of the shell
z_p         = pcb_t;                        // parting plane (PCB front face)
z_floor_top = -back_clear;
z_bottom    = z_floor_top - floor_t;
holes = [for (sx=[-1,1], sy=[-1,1]) [sx*hole_dx/2, sy*hole_dy/2]];
aa_cx = -board_len/2 + aa_from_end + aa_len/2;
mic_x = -board_len/2 + mic_x_from_end;
mic_y = board_wid/2 - mic_y_from_side;
btns = [[btn_x, btn_y], [btn_x, -btn_y]];

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
module usb_cut() {
    translate([x_in_max - 1, -usb_cut_w/2, usb_z_center - usb_cut_h/2])
        cube([wall_t + 3, usb_cut_w, usb_cut_h]);
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
            // full-height posts: the PCB rests on them
            for (h = holes) translate([h[0], h[1], z_floor_top - 0.1]) cylinder(d=post_d, h=back_clear + 0.1);
            // guide tubes that carry a pin to the BOOT and RESET buttons
            for (b = btns) translate([b[0], b[1], z_floor_top - 0.1]) cylinder(d=guide_od, h=back_clear + btn_top - guide_gap + 0.1);
        }
        // nut pockets (open towards the PCB so nuts drop in from above) and the blind screw holes
        for (h = holes) {
            translate([h[0], h[1], -pocket]) rotate(30) cylinder(d=nut_d, h=pocket + 0.1, $fn=6);
            translate([h[0], h[1], -pocket - blind_below]) cylinder(d=screw_hole_d, h=blind_below + 0.2);
        }
        for (b = btns) translate([b[0], b[1], z_bottom - 1]) cylinder(d=pin_hole_d, h=floor_t + back_clear + 1);   // all the way through floor and guide tube
        usb_cut();
        // floor slots under the ESP32 module
        for (i = [0:vent_n-1])
            translate([u1_x0 + 1, (u1_y0 + u1_y1)/2 + (i - (vent_n-1)/2)*vent_pitch - vent_w/2, z_bottom - 1])
                cube([u1_x1 - u1_x0 - 2, vent_w, floor_t + 2]);
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
module screws_ghost() {
    for (h = holes) color("black") translate([h[0], h[1], 0]) {
        translate([0,0,z_glass_top]) cylinder(d=head_d, h=head_t);
        translate([0,0,-4.1]) cylinder(d=3, h=z_glass_top + 4.1);
    }
}
module ghosts() { pcb_ghost(); screen_ghost(); screws_ghost(); }

module assembled(dz=0) {
    color("lightsteelblue") translate([0,0,dz]) top_half();
    color("slategray") bottom_half();
    if (show_ghosts) translate([0,0,dz/2]) ghosts();
}

if (part == "assembled")        difference() { assembled(); if (section) translate([-200,-200 + cut_y,-200]) cube([400,200,400]); }
else if (part == "exploded")    assembled(20);
else if (part == "top")         top_half();
else if (part == "bottom")      bottom_half();
else if (part == "top_print")   translate([0,0,z_top]) rotate([180,0,0]) top_half();     // outer face on the bed
else if (part == "bottom_print") translate([0,0,-z_bottom]) bottom_half();                // floor on the bed
