// Oregon "Pacific Wonderland" style license plate
// Scaled for ~1:24 (truck ≈ 4" wide)
// Real US plate is 12" x 6" → ~12.7 x 6.35 mm at 1:24. Default here is
// slightly oversize to make the small text / stickers actually printable.

// ---------- PARAMETERS ----------
// Plate number is split around a manually-drawn dash bar because the
// LICENSE PLATE USA font has a decorative glyph in place of the hyphen.
// Set plate_right = "" to render a single centered word with no dash.
plate_left       = "LIL";
plate_right      = "RIG";
dash_bar_width   = 1.6;              // mm, width of the dash between parts
dash_bar_height  = 0.45;             // mm, thickness of the dash bar
dash_gap         = 0.5;              // mm, gap between text and dash bar

plate_width      = 20;               // mm
plate_height     = 10;               // mm (keep 2:1 ratio for accuracy)
plate_thickness  = 1.2;              // mm, base
relief_depth     = 0.5;              // mm, all raised elements same height
                                     //   (lets you do one color-swap in slicer)

// Text sizes (cap height in mm)
oregon_size      = 1.6;              // "OREGON" banner at top
number_size      = 3.5;              // big main plate number
motto_size       = 0.85;             // "PACIFIC WONDERLAND" bottom banner

// Vertical placement (center-y of each element, in mm from bottom)
oregon_y         = 8.0;
number_y         = 4.3;
motto_y          = 0.85;

// Inner border (set border_inset = 0 to sit flush with the plate edge)
border_inset     = 0;
border_width     = 0.25;

// Mounting holes
mounting_holes   = true;
hole_diameter    = 0.9;
hole_inset       = 0.7;

// Font for the big plate number. Oregon uses "Driver Gothic" (Canada Type,
// paid, ~$45 — the real deal used on ~22 US state plates). The free lookalike
// is "License Plate" by Dave Hansen — search "License Plate font dafont".
// Install to ~/Library/Fonts/ on macOS, then the OpenSCAD name is below.
//   - Free:  "License Plate"
//   - Paid:  "Driver Gothic Pro:style=Regular"
//   - Fallback if neither installed: any condensed bold sans
font_number      = "LICENSE PLATE USA";
// Banner text ("OREGON", "PACIFIC WONDERLAND") looks like plain Helvetica/Arial
// Bold on the real plate — not the plate font.
font_banner      = "Helvetica:style=Bold";
$fn              = 40;

// ---------- MODULES ----------
module plate_base() {
    cube([plate_width, plate_height, plate_thickness]);
}

module raised_text(str, size_mm, y_pos, font_name, halign_mode = "center", x_pos = undef) {
    translate([is_undef(x_pos) ? plate_width / 2 : x_pos, y_pos, 0])
        linear_extrude(relief_depth)
            text(str, size = size_mm, font = font_name,
                 halign = halign_mode, valign = "center");
}

module plate_number_block() {
    if (plate_right == "") {
        raised_text(plate_left, number_size, number_y, font_number);
    } else {
        cx = plate_width / 2;
        // left text, right-aligned to just before the dash
        raised_text(plate_left, number_size, number_y, font_number,
                    "right", cx - dash_bar_width / 2 - dash_gap);
        // manual dash bar in the middle
        translate([cx - dash_bar_width / 2,
                   number_y - dash_bar_height / 2, 0])
            cube([dash_bar_width, dash_bar_height, relief_depth]);
        // right text, left-aligned just after the dash
        raised_text(plate_right, number_size, number_y, font_number,
                    "left", cx + dash_bar_width / 2 + dash_gap);
    }
}

module inner_border() {
    difference() {
        translate([border_inset, border_inset, 0])
            cube([plate_width  - 2 * border_inset,
                  plate_height - 2 * border_inset,
                  relief_depth]);
        translate([border_inset + border_width,
                   border_inset + border_width, -0.01])
            cube([plate_width  - 2 * (border_inset + border_width),
                  plate_height - 2 * (border_inset + border_width),
                  relief_depth + 0.02]);
    }
}

module raised_elements() {
    translate([0, 0, plate_thickness]) {
        inner_border();
        raised_text("OREGON",             oregon_size, oregon_y, font_banner);
        plate_number_block();
        raised_text("PACIFIC WONDERLAND", motto_size,  motto_y,  font_banner);
    }
}

module plate() {
    difference() {
        union() {
            color("navy") plate_base();
            color("gold") raised_elements();
        }
        if (mounting_holes) {
            for (x = [hole_inset, plate_width  - hole_inset])
                for (y = [hole_inset, plate_height - hole_inset])
                    translate([x, y, -0.5])
                        cylinder(d = hole_diameter,
                                 h = plate_thickness + relief_depth + 2);
        }
    }
}

plate();
