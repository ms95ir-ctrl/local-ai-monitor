// Interference checks: each check intersects two things that must not overlap.
// Run, for example:  openscad -D 'check="keepout_bottom"' -D 'part="none"' -o /tmp/x.stl check.scad
// "Current top level object is empty" means NO overlap, which is the wanted result.
include <case.scad>
check = "";
bump = 0;   // test only: shifts the pin in Y to prove the checker can see a collision
kz = 0;     // test only: shifts the board keep-outs in Z

e = 0.05;   // test shapes are shrunk by this much so surfaces that only touch (sleeve on PCB, post under PCB) do not count
module box(x0,x1,y0,y1,z0,z1) { translate([x0+e,y0+e,z0+e]) cube([x1-x0-2*e, y1-y0-2*e, z1-z0-2*e]); }
// Tallest back-of-board parts, positions read from the vendor drawing (back view, page 13). Approximate.
module keepouts() { translate([0,0,kz]) {
    box(17, 23, -23.2, -18.4, -4.7, 0);      // BAT connector (unused)
    box(3, 13, -23.2, -15, -4.7, 0);         // UART connector
    box(-15, 0.7, -22.5, -8.9, -4.7, 0);     // SD card slot
    box(-28.7, -20.7, 17, 25, -4.7, 0);      // IO connector
    box(-10.9, -2.9, 17, 25, -4.7, 0);       // I2C connector
    box(3.7, 9.7, 19, 25, -4.7, 0);          // SPEAKER connector
    box(13, 28, 8.8, 20.4, -1.5, 0);         // U1 module
    box(38.3, 41.3, 9.6, 13.6, -1.5, 0);     // RESET
    box(38.3, 41.3, -13.6, -9.6, -1.5, 0);   // BOOT
}}
module glass()  { translate([-board_len/2 + lcd_from_end + e, -lcd_wid/2 + e, pcb_t + e]) cube([lcd_len - 2*e, lcd_wid - 2*e, glass_above_pcb - 2*e]); }
module pcb()    { translate([0,0,e]) linear_extrude(pcb_t - 2*e) offset(r=board_corner_r - e) square([board_len-2*board_corner_r, board_wid-2*board_corner_r], center=true); }
module screws() { for (h = holes) translate([h[0], h[1], -4.1]) cylinder(d=3, h=z_glass_top + 4.1 + head_t); }
// a 1.6 mm pin pushed from outside straight through the floor to each button
module pins()   { for (b = btns) translate([b[0], b[1] + bump, z_bottom - 10]) cylinder(d=1.6, h=10 - z_bottom + btn_top + 0.3); }
// a USB-C plug body (overmould) seated in the receptacle
module plug()   { translate([x_in_max - 8, -usb_cut_w/2 + 0.2, usb_z_center - usb_cut_h/2 + 0.2]) cube([8 + wall_t + 6, usb_cut_w - 0.4, usb_cut_h - 0.4]); }

if (check == "glass_top")        intersection() { glass(); top_half(); }
if (check == "glass_bottom")     intersection() { glass(); bottom_half(); }
if (check == "pcb_top")          intersection() { pcb(); top_half(); }
if (check == "pcb_bottom")       intersection() { pcb(); bottom_half(); }
if (check == "screws_bottom")    intersection() { screws(); bottom_half(); }
if (check == "screws_top")       intersection() { screws(); top_half(); }
if (check == "keepout_bottom")   intersection() { keepouts(); bottom_half(); }
if (check == "keepout_top")      intersection() { keepouts(); top_half(); }
if (check == "pin_bottom")       intersection() { pins(); bottom_half(); }   // the pin path must be clear
if (check == "pin_reaches")      intersection() { pins(); keepouts(); }      // ...and must touch the buttons (non-empty is wanted)
if (check == "plug_top")         intersection() { plug(); top_half(); }
if (check == "plug_bottom")      intersection() { plug(); bottom_half(); }
if (check == "top_bottom")       intersection() { top_half(); bottom_half(); }
