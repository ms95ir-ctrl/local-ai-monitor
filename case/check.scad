// Interference checks: each check intersects two things that must not overlap.
// Run, for example:  openscad -D 'check="battery_bottom"' -D 'part="none"' -o /tmp/x.stl check.scad
// "Current top level object is empty" means NO overlap, which is the wanted result.
include <case.scad>
check = "";
bump = 0;   // test only: shifts the battery in Y to prove the checker can see a collision
kz = 0;     // test only: shifts the board keep-outs in Z

e = 0.05;   // test shapes are shrunk by this much so surfaces that only touch (sleeve on PCB, boss under PCB) do not count
module box(x0,x1,y0,y1,z0,z1) { translate([x0+e,y0+e,z0+e]) cube([x1-x0-2*e, y1-y0-2*e, z1-z0-2*e]); }
// Tallest back-of-board parts, positions read from the vendor drawing (back view, page 13). Approximate.
module keepouts() { translate([0,0,kz]) {
    box(17, 23, -23.2, -18.4, -4.7, 0);      // BAT connector
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
module bat()    { translate([0, bump, 0]) translate([-bat_len/2 + e, -bat_wid/2 + e, z_floor_top + 0.2 + e]) cube([bat_len - 2*e, bat_wid - 2*e, bat_thick - 2*e]); }
module spk()    { translate([speaker_x - spk_len/2 + e, speaker_y - spk_wid/2 + e, z_bat_top + 0.2 + e]) cube([spk_len - 2*e, spk_wid - 2*e, spk_thick - 2*e]); }
module swall()  { translate([switch_x - sw_len/2 + e, sw_box_y0 + e, sw_z0 + 0.1 + e]) cube([sw_len - 2*e, sw_dep - 2*e, sw_hgt - 0.2 - 2*e]);
                  translate([switch_x - sw_knob_w/2 - sw_travel/2, sw_face_y, sw_z0 + sw_hgt/2 - sw_knob_t/2]) cube([sw_knob_w + sw_travel, sw_knob_out, sw_knob_t]); }
module screws() { for (h = holes) translate([h[0], h[1], -4.1]) cylinder(d=3, h=z_glass_top + 4.1 + head_t); }

if (check == "glass_top")        intersection() { glass(); top_half(); }
if (check == "glass_bottom")     intersection() { glass(); bottom_half(); }
if (check == "pcb_top")          intersection() { pcb(); top_half(); }
if (check == "pcb_bottom")       intersection() { pcb(); bottom_half(); }
if (check == "battery_bottom")   intersection() { bat(); bottom_half(); }
if (check == "battery_top")      intersection() { bat(); top_half(); }
if (check == "speaker_bottom")   intersection() { spk(); bottom_half(); }
if (check == "speaker_top")      intersection() { spk(); top_half(); }
if (check == "speaker_battery")  intersection() { spk(); bat(); }
if (check == "switch_bottom")    intersection() { swall(); bottom_half(); }
if (check == "switch_top")       intersection() { swall(); top_half(); }
if (check == "switch_speaker")   intersection() { swall(); spk(); }
if (check == "screws_bottom")    intersection() { screws(); bottom_half(); }
if (check == "screws_top")       intersection() { screws(); top_half(); }
if (check == "keepout_bottom")   intersection() { keepouts(); bottom_half(); }
if (check == "keepout_speaker")  intersection() { keepouts(); spk(); }
if (check == "keepout_switch")   intersection() { keepouts(); swall(); }
if (check == "keepout_top")      intersection() { keepouts(); top_half(); }
if (check == "top_bottom")       intersection() { top_half(); bottom_half(); }
