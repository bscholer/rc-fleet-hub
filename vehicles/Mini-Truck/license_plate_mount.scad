// Plain L-bracket for mounting the license plate to the back of the truck/trailer.
// Horizontal leg sits flat on the ~10 mm horizontal mounting surface;
// vertical leg hangs down and the plate glues onto its outward face.

// ---------- PARAMETERS ----------
plate_width       = 20;       // mm, width of the plate
plate_height      = 10;       // mm, height of the plate
mount_depth       = 10;       // mm, horizontal-leg length
bracket_thickness = 1.5;      // mm, wall thickness for both legs

// ---------- MODULES ----------
module vertical_leg() {
    cube([plate_width, bracket_thickness, plate_height]);
}

module horizontal_leg() {
    translate([0, 0, plate_height - bracket_thickness])
        cube([plate_width, mount_depth, bracket_thickness]);
}

module bracket() {
    vertical_leg();
    horizontal_leg();
}

bracket();
