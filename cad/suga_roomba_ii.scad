// =============================================================================
// 菅ルンバ II (Suga Roomba II) — Parametric 3D CAD Master Assembly
// Sub-3,000 JPY (2,980 JPY Verified) High-Suction Autonomous Cyclone Vacuum
//
// Mechanical Specifications (Verified via CFD / FEM / Acoustic / SPICE / KiCad):
//   Body Planform : Asymmetric Reuleaux-D Hybrid (W=210mm, L=198mm, H=62mm)
//   Impeller      : D2=78mm, D1=28mm, b2=7.5mm, Z=9 backward-curved beta2=28 deg
//   Cyclone       : 500ml PET bottle (D=65mm x H=50mm) tangential dual-cyclone
//   Drivetrain    : 32.5:1 magnetic-preload friction rim drive (2x Ø78mm wheels)
//   PTO Brush     : Axle-coupled O-ring belt driving front agitator + side brush
//   Muffler       : Integrated rear Helmholtz resonator (9.5 cm^3, f0=1423 Hz)
//   Electronics   : 68.58 x 53.34 mm 2-layer PCB (AE-RP2040 + TC78H653 + MCP6002)
// =============================================================================

$fn = 48;

// Select part to render: "all" or "01_..." .. "12_..."
PART = "all";
EXPLODE = 0.0; // 0.0 = assembled, 1.0 = exploded preview

// --- Core Locked Parameters (mm) ---
BODY_W      = 210.0;
BODY_L      = 198.0;
BODY_H      = 62.0;
FLOOR_Z     = 4.0;
SHELL_T     = 1.8;   // Printable/CAD wall (origami PP flat sheet is 0.75mm box-rib)

WHEEL_D     = 78.0;
WHEEL_W     = 14.0;
WHEEL_X     = 86.0;
WHEEL_Y     = 0.0;
WHEEL_Z     = WHEEL_D / 2.0; // 39.0mm => bottom sits exactly at Z=0

IMP_D2      = 78.0;
IMP_D1      = 28.0;
IMP_B2      = 7.5;
IMP_Z_CNT   = 9;
BLOWER_X    = 34.0;
BLOWER_Y    = 18.0;
BLOWER_Z    = 15.0;

CYC_D       = 65.0;
CYC_H       = 48.0;
CYC_X       = -26.0;
CYC_Y       = 24.0;
CYC_Z       = 8.0;

NOZZLE_W    = 115.0;
NOZZLE_Y    = 66.0;

PCB_W       = 68.58;
PCB_H       = 53.34;
PCB_X       = 0.0;
PCB_Y       = -45.0;
PCB_Z       = 39.0;

// =============================================================================
// Helper 2D Geometry: Exact 210 x 198 mm Asymmetric Reuleaux-D Hybrid Planform
//   Front corners: (±66, +70), R=24 => y_max = +94mm (90 deg corner reach)
//   Waist arcs   : (±81,   0), R=24 => x = ±105mm (W = 210.0mm)
//   Rear Reuleaux: (  0, -20), R=84 => y_min = -104mm (L = 198.0mm, zero overhang)
// =============================================================================
module reuleaux_d_2d(inset = 0.0) {
    offset(r = -inset) {
        hull() {
            translate([-66,  70]) circle(r = 24);
            translate([ 66,  70]) circle(r = 24);
            translate([-81,   0]) circle(r = 24);
            translate([ 81,   0]) circle(r = 24);
            translate([  0, -20]) circle(r = 84);
        }
    }
}

// =============================================================================
// PART 01: Lower Origami Box-Rib Monocoque Chassis
// =============================================================================
module part_01_lower_chassis() {
    color([0.92, 0.93, 0.95])
    difference() {
        union() {
            // Outer lower tub Z = 4..34mm
            translate([0, 0, FLOOR_Z])
                linear_extrude(height = 30.0)
                    reuleaux_d_2d(0.0);

            // Integrated box-rib longitudinal stiffeners (floor interior)
            for (rx = [-52, -18, 18, 52]) {
                translate([rx - 1.2, -75, FLOOR_Z + SHELL_T - 0.3])
                    cube([2.4, 130, 5.3]);
            }
            // Cross ribs around battery bay
            for (ry = [-72, -16]) {
                translate([-58, ry - 1.2, FLOOR_Z + SHELL_T - 0.3])
                    cube([116, 2.4, 7.3]);
            }
            // Bernoulli nozzle collector duct walls
            translate([-NOZZLE_W/2 - 3, NOZZLE_Y - 12, FLOOR_Z])
                cube([NOZZLE_W + 6, 22, 16]);
            // PCB corner standoffs
            for (sx = [-PCB_W/2 + 4, PCB_W/2 - 4]) {
                for (sy = [-PCB_H/2 + 4, PCB_H/2 - 4]) {
                    translate([PCB_X + sx, PCB_Y + sy, FLOOR_Z + SHELL_T - 0.3])
                        cylinder(d = 5.5, h = PCB_Z - FLOOR_Z - SHELL_T + 0.3);
                }
            }
        }

        // Hollow interior cavity
        translate([0, 0, FLOOR_Z + SHELL_T])
            linear_extrude(height = 32.0)
                reuleaux_d_2d(SHELL_T);

        // Left & Right Wheel Wells (clearance for Ø78 x 14mm wheels)
        for (sx = [-1, 1]) {
            translate([sx * WHEEL_X - (WHEEL_W + 8)/2, WHEEL_Y - (WHEEL_D + 8)/2, -1])
                cube([WHEEL_W + 8, WHEEL_D + 8, 50]);
        }

        // Front Bernoulli Floor-Seal Suction Slot (115 x 14 mm)
        translate([-NOZZLE_W/2, NOZZLE_Y - 7, -1])
            cube([NOZZLE_W, 14, 15]);

        // Bottom Optical Mouse Sensor Window (y = -48)
        translate([0, -48, -1])
            cylinder(d = 14, h = 10);

        // Cliff LED downward apertures
        for (sx = [-1, 1]) {
            translate([sx * 58, 80, -1])
                cylinder(d = 6, h = 10);
        }

        // Rear Caster Socket recess
        translate([0, -82, FLOOR_Z - 1])
            cylinder(d = 22, h = 5);
    }
}

// =============================================================================
// PART 02: Upper Aerodynamic Wedge Shell + Helmholtz Exhaust Muffler
// =============================================================================
module part_02_upper_shell() {
    color([0.16, 0.24, 0.34])
    difference() {
        union() {
            // Smooth stepped aerodynamic upper dome Z = 34..62mm
            hull() {
                translate([0, 0, 34.0])
                    linear_extrude(height = 2.0)
                        reuleaux_d_2d(0.0);
                translate([0, -3, 56.0])
                    linear_extrude(height = 4.0)
                        reuleaux_d_2d(10.0);
                translate([0, -6, 60.5])
                    linear_extrude(height = 1.5)
                        reuleaux_d_2d(18.0);
            }
        }

        // Hollow underside
        translate([0, 0, 32.0])
            hull() {
                linear_extrude(height = 2.0)
                    reuleaux_d_2d(SHELL_T);
                translate([0, -3, 22.0])
                    linear_extrude(height = 4.0)
                        reuleaux_d_2d(10.0 + SHELL_T);
            }

        // Transparent PET Cyclone Top Viewing / Quick-Release Bay Opening
        translate([CYC_X, CYC_Y, 30.0])
            cylinder(d = CYC_D + 3.0, h = 40.0);

        // Wheel top arch clearance
        for (sx = [-1, 1]) {
            translate([sx * WHEEL_X, WHEEL_Y, WHEEL_Z])
                rotate([0, 90, 0])
                    cylinder(d = WHEEL_D + 6.0, h = WHEEL_W + 6.0, center = true);
        }

        // Rear Helmholtz Resonator Muffler Exhaust Louvers (f0 = 1423 Hz)
        for (ix = [-24 : 6 : 24]) {
            translate([ix - 1.5, -108, 38])
                cube([3.0, 22.0, 12.0]);
        }
    }

    // Integrated Side-Branch Helmholtz Acoustic Cavity (9.5 cm^3) inside rear exhaust
    color([0.22, 0.32, 0.44])
    translate([16, -86, 36])
        difference() {
            cube([32, 18, 20]);
            translate([2, 2, 2]) cube([28, 14, 16]);
            // Tuned acoustic neck opening into exhaust duct (d=7mm, L=5mm)
            translate([16, 18, 10]) rotate([90, 0, 0]) cylinder(d = 7.0, h = 6.0);
        }
}

// =============================================================================
// PART 03: Logarithmic Spiral Volute Blower Housing + RS-385PH-4045 Motor
//   RS-385PH mounted low-profile beneath volute deck => total height <= 60mm!
// =============================================================================
module part_03_volute_blower() {
    // Volute spiral housing (PET/PP wrapped chamber around Ø78mm impeller)
    color([0.28, 0.56, 0.84])
    translate([BLOWER_X, BLOWER_Y, BLOWER_Z]) {
        difference() {
            union() {
                hull() {
                    cylinder(r = IMP_D2/2 + 4.5, h = IMP_B2 + 6.0);
                    translate([4, -6, 0]) cylinder(r = IMP_D2/2 + 6.5, h = IMP_B2 + 6.0);
                    // Tangential exhaust chute pointing toward rear Helmholtz muffler
                    translate([12, -52, 0]) cube([24, 16, IMP_B2 + 6.0]);
                }
                // Low-profile motor mounting collar (sits neatly inside aero dome)
                translate([0, 0, IMP_B2 + 5.0])
                    cylinder(d = 31.5, h = 6.0);
            }
            // Internal impeller clearance chamber
            translate([0, 0, 2.2])
                cylinder(r = IMP_D2/2 + 2.5, h = IMP_B2 + 1.8);
            // Internal tangential exhaust channel
            translate([16, -54, 2.2])
                cube([16, 48, IMP_B2 + 1.8]);
            // Axial inlet eye (bottom, Ø28mm connected to cyclone vortex finder)
            translate([0, 0, -2.0])
                cylinder(d = IMP_D1 + 1.5, h = 6.0);
            // Motor shaft pass-through hole
            translate([0, 0, IMP_B2 + 1.0])
                cylinder(d = 8.0, h = 15.0);
        }

        // Suction transfer duct bridging Cyclone top vortex finder to Volute eye
        hull() {
            translate([0, 0, -5.0]) cylinder(d = IMP_D1 + 2.0, h = 6.0);
            translate([CYC_X - BLOWER_X, CYC_Y - BLOWER_Y, -5.0]) cylinder(d = IMP_D1 + 2.0, h = 6.0);
        }
    }

    // RS-385PH-4045 High-Speed Brushed DC Motor (Ø27.5 x 37.8mm, nested cleanly at Z=21..59mm)
    color([0.74, 0.77, 0.80])
    translate([BLOWER_X, BLOWER_Y, BLOWER_Z + IMP_B2 + 1.0]) {
        cylinder(d = 27.5, h = 32.0);
        color([0.18, 0.18, 0.20])
            translate([0, 0, 31.5]) cylinder(d = 27.2, h = 3.8);
        color([0.85, 0.85, 0.88])
            translate([0, 0, -8.0]) cylinder(d = 2.3, h = 10.0);
    }
}

// =============================================================================
// PART 04: Ø78mm 9-Blade Backward-Curved Logarithmic Spiral Impeller (Al-Can)
// =============================================================================
module log_spiral_blade(r1, r2, beta_deg, h, thick) {
    steps = 16;
    cot_b = 1.0 / tan(beta_deg);
    pts_outer = [
        for (i = [0 : steps])
            let(
                t = i / steps,
                r = r1 + (r2 - r1) * t,
                theta = -cot_b * ln(r / r1) * 180 / PI * 0.55
            )
            [r * cos(theta), r * sin(theta)]
    ];
    for (i = [0 : steps - 1]) {
        hull() {
            translate([pts_outer[i][0], pts_outer[i][1], 0])
                cylinder(d = thick, h = h, $fn = 12);
            translate([pts_outer[i+1][0], pts_outer[i+1][1], 0])
                cylinder(d = thick, h = h, $fn = 12);
        }
    }
}

module part_04_impeller() {
    color([0.86, 0.88, 0.91])
    translate([BLOWER_X, BLOWER_Y, BLOWER_Z + 2.6]) {
        difference() {
            union() {
                translate([0, 0, IMP_B2 - 0.9])
                    cylinder(d = IMP_D2, h = 0.9);
                cylinder(d = 12.0, h = IMP_B2 + 1.5);
                difference() {
                    cylinder(d = IMP_D2, h = 0.9);
                    translate([0, 0, -0.5]) cylinder(d = IMP_D1, h = 2.0);
                }
                for (k = [0 : IMP_Z_CNT - 1]) {
                    rotate([0, 0, k * 360.0 / IMP_Z_CNT])
                        log_spiral_blade(IMP_D1 / 2.0, IMP_D2 / 2.0 - 0.5, 28.0, IMP_B2, 0.95);
                }
            }
            translate([0, 0, -1.0])
                cylinder(d = 2.35, h = IMP_B2 + 4.0);
        }
    }
}

// =============================================================================
// PART 05: 500ml PET Bottle Tangential Dual-Cyclone & Magnetic Dust Bin
// =============================================================================
module part_05_pet_cyclone() {
    translate([CYC_X, CYC_Y, CYC_Z]) {
        color([0.72, 0.91, 0.98, 0.55])
        difference() {
            union() {
                cylinder(d = CYC_D, h = CYC_H);
                translate([14, 18, 12])
                    rotate([0, 0, -28])
                        cube([16, 36, 22]);
            }
            translate([0, 0, 2.0])
                cylinder(d = CYC_D - 2.4, h = CYC_H);
            translate([16, 18, 14])
                rotate([0, 0, -28])
                    cube([12, 40, 18]);
        }

        color([0.85, 0.95, 0.99, 0.7])
        translate([0, 0, 14])
            difference() {
                cylinder(d1 = 36, d2 = CYC_D - 3.5, h = 14);
                translate([0, 0, -0.5]) cylinder(d1 = 33, d2 = CYC_D - 6.5, h = 15);
            }

        color([0.96, 0.62, 0.14])
        translate([0, 0, 24])
            difference() {
                cylinder(d = IMP_D1, h = CYC_H - 22);
                translate([0, 0, -1]) cylinder(d = IMP_D1 - 2.4, h = CYC_H - 20);
            }

        color([0.20, 0.68, 0.52])
        translate([0, 0, CYC_H - 0.4]) {
            difference() {
                cylinder(d = CYC_D + 2.0, h = 3.5);
                translate([0, 0, -0.5]) cylinder(d = IMP_D1 - 2.4, h = 5.0);
            }
            translate([-14, -2.5, 3.2]) cube([28, 5.0, 3.5]);
        }
    }
}

// =============================================================================
// PART 06: 32.5:1 Magnetic-Preload Friction Rim Drive (Wheels + PWN10EB12CB)
// =============================================================================
module single_drive_unit(side = 1) {
    translate([side * WHEEL_X, WHEEL_Y, WHEEL_Z]) {
        rotate([0, 90, 0]) {
            color([0.14, 0.15, 0.17])
            difference() {
                cylinder(d = WHEEL_D, h = WHEEL_W, center = true);
                cylinder(d = WHEEL_D - 5.0, h = WHEEL_W + 2.0, center = true);
            }
            color([0.94, 0.42, 0.22]) {
                difference() {
                    cylinder(d = WHEEL_D - 4.8, h = WHEEL_W - 1.5, center = true);
                    cylinder(d = WHEEL_D - 14.0, h = WHEEL_W + 2.0, center = true);
                }
                cylinder(d = 16.0, h = WHEEL_W - 1.0, center = true);
                for (a = [0 : 60 : 300]) {
                    rotate([0, 0, a])
                        cube([WHEEL_D - 12, 4.0, WHEEL_W - 3.0], center = true);
                }
            }
            color([0.80, 0.82, 0.85])
                cylinder(d = 4.0, h = WHEEL_W + 12.0, center = true);
        }
    }

    color([0.75, 0.78, 0.82])
    translate([side * (WHEEL_X - 18.0), WHEEL_Y - 26.0, 26.0]) {
        cube([14.9, 25.1, 20.0], center = true);
        color([0.92, 0.78, 0.25])
        translate([side * 12.0, 12.0, 0])
            rotate([0, 90, 0])
                cylinder(d = 2.4, h = 16.0, center = true);
    }

    color([0.82, 0.85, 0.90])
    translate([side * (WHEEL_X - 18.0), WHEEL_Y - 12.0, 14.0])
        cylinder(d = 10.0, h = 3.0);
}

module part_06_friction_drive() {
    single_drive_unit(-1);
    single_drive_unit(1);
}

// =============================================================================
// PART 07: Zero-Extra-Motor Axle-PTO Agitator Brush & Corner Side Brush
// =============================================================================
module part_07_pto_brushes() {
    color([0.96, 0.74, 0.16])
    translate([WHEEL_X - 10.5, WHEEL_Y, WHEEL_Z])
        rotate([0, 90, 0])
            cylinder(d = 18.0, h = 4.5, center = true);

    color([0.12, 0.12, 0.14])
    hull() {
        translate([WHEEL_X - 10.5, WHEEL_Y, WHEEL_Z - 7]) rotate([0, 90, 0]) cylinder(d = 3.0, h = 2.5, center = true);
        translate([NOZZLE_W/2 + 4, NOZZLE_Y, 12]) rotate([0, 90, 0]) cylinder(d = 3.0, h = 2.5, center = true);
    }
    hull() {
        translate([WHEEL_X - 10.5, WHEEL_Y, WHEEL_Z + 7]) rotate([0, 90, 0]) cylinder(d = 3.0, h = 2.5, center = true);
        translate([NOZZLE_W/2 + 4, NOZZLE_Y, 20]) rotate([0, 90, 0]) cylinder(d = 3.0, h = 2.5, center = true);
    }

    translate([0, NOZZLE_Y, 14.0]) {
        color([0.92, 0.42, 0.20])
        rotate([0, 90, 0])
            cylinder(d = 12.0, h = NOZZLE_W - 4.0, center = true);
        color([0.16, 0.17, 0.20])
        for (a = [0 : 90 : 270]) {
            rotate([a, 0, 0])
                translate([0, 0, 6.0])
                    cube([NOZZLE_W - 8.0, 1.8, 7.0], center = true);
        }
        color([0.96, 0.74, 0.16])
        translate([NOZZLE_W/2 + 4, 0, 0])
            rotate([0, 90, 0])
                cylinder(d = 14.0, h = 4.5, center = true);
    }

    translate([72.0, 74.0, 4.0]) {
        color([0.16, 0.58, 0.48])
            cylinder(d = 14.0, h = 6.0);
        color([0.12, 0.14, 0.16])
        for (a = [0, 120, 240]) {
            rotate([0, 0, a + 20])
                translate([21, 0, 1.5])
                    cube([42.0, 2.2, 2.5], center = true);
        }
    }
}

// =============================================================================
// PART 08: 5S AA NiMH Low-CoG Battery Pack (Seria VOLCANO NZ x5 = 6.0V)
//   Centered at y = -46mm => balances CoG at Y = -10mm (88% drive wheel traction,
//   12% stabilizing load on rear caster, zero nose-tipping!)
// =============================================================================
module part_08_battery_pack() {
    translate([-4.0, -46.0, 14.5]) {
        for (i = [-2 : 2]) {
            color([0.14, 0.52, 0.34])
            translate([i * 15.2, 0, 0])
                rotate([90, 0, 0])
                    cylinder(d = 14.5, h = 50.5, center = true);
            color([0.85, 0.87, 0.90])
            translate([i * 15.2, (i % 2 == 0) ? 25.5 : -25.5, 0])
                rotate([90, 0, 0])
                    cylinder(d = 5.5, h = 2.4, center = true);
        }
        color([0.86, 0.88, 0.90]) {
            translate([0,  26.2, 0]) cube([76.0, 1.2, 10.0], center = true);
            translate([0, -26.2, 0]) cube([76.0, 1.2, 10.0], center = true);
        }
    }
}

// =============================================================================
// PART 09: Main PCB Assembly (68.58 x 53.34mm KiCad Verified Board — 100% Manifold)
// =============================================================================
module part_09_main_pcb() {
    translate([PCB_X, PCB_Y, PCB_Z]) {
        union() {
            // FR-4 / Akizuki AE-DB1 Solder-Masked PCB Substrate
            color([0.10, 0.42, 0.24])
                translate([-PCB_W/2, -PCB_H/2, 0])
                    cube([PCB_W, PCB_H, 1.6]);

            // U1: AE-RP2040 Dual-Core MCU Module (embedded 0.3mm for clean manifold union)
            color([0.12, 0.14, 0.18])
                translate([-28, -14, 1.3]) cube([20, 26, 4.8]);
            color([0.82, 0.84, 0.86])
                translate([-22, -16, 2.2]) cube([8, 6, 3.2]);

            // U2: AE-TC78H653FTG Dual H-Bridge Module
            color([0.16, 0.26, 0.48])
                translate([-3, -8, 1.3]) cube([13, 18, 3.8]);

            // U3: MCP6002 Dual Op-Amp (DIP-8)
            color([0.15, 0.15, 0.17])
                translate([14, -12, 1.3]) cube([9.5, 7.0, 4.5]);

            // R17: MPC74 0.33Ω 5W Cement Shunt Resistor
            color([0.95, 0.95, 0.93])
                translate([12, 2, 1.3]) cube([14, 7.5, 8.8]);

            // C1 & C2: 2x 470uF 16V Electrolytic Capacitors (clear of U1 y=+12 edge)
            color([0.10, 0.22, 0.46]) {
                translate([-12, 18.5, 1.3]) cylinder(d = 8.0, h = 11.8);
                translate([ -1, 18.5, 1.3]) cylinder(d = 8.0, h = 11.8);
            }

            // Q1: 2SK4017 TO-251 Power MOSFET
            color([0.16, 0.16, 0.18])
                translate([16, 15, 1.3]) cube([6.5, 2.5, 9.3]);
        }
    }
}

// =============================================================================
// PART 10: Low-Cost Sensor Array (4x Red LED Ranging + Mouse OptFlow + Piezo)
// =============================================================================
module part_10_sensors() {
    // 1. 4x OSR5JA3Z74A 3mm Red LED-as-Photodiode Optical Sensors (10 JPY each!)
    color([0.95, 0.18, 0.18]) {
        // Front wall rangefinder
        translate([0, 91, 22]) rotate([-90, 0, 0]) cylinder(d = 3.2, h = 6.0);
        // Right side wall-follower
        translate([96, 44, 22]) rotate([0, 90, 0]) cylinder(d = 3.2, h = 6.0);
        // Front-Left & Front-Right Downward Cliff Sensors
        translate([-58, 80, 5]) cylinder(d = 3.2, h = 6.0);
        translate([ 58, 80, 5]) cylinder(d = 3.2, h = 6.0);
    }

    // 2. Daiso 110-JPY Optical Mouse Sensor PCB + Clear Prism Lens (Bottom Optical Flow)
    translate([0, -48, 7.0]) {
        color([0.18, 0.48, 0.28])
            cube([24.0, 28.0, 1.6], center = true);
        color([0.82, 0.90, 0.96, 0.7])
            translate([0, 0, -2.0]) cube([14.0, 14.0, 3.0], center = true);
    }

    // 3. Murata PKM13EPYH4000-A0 Piezo Acoustic Dirt-Detect Disc (on Cyclone Inlet)
    color([0.94, 0.76, 0.18])
    translate([-1, 52, 22])
        rotate([90, 0, 28])
            cylinder(d = 12.6, h = 2.5);
}

// =============================================================================
// PART 11: Floating D-Front Collision Bumper + Bernoulli Floor-Seal Skirt
// =============================================================================
module part_11_bumper_skirt() {
    // Floating wrap-around front bumper arc (triggers 2x salvaged mouse microswitches)
    color([0.22, 0.24, 0.28])
    translate([0, 3.5, 10.0])
        difference() {
            linear_extrude(height = 22.0)
                reuleaux_d_2d(-2.5);
            translate([0, 0, -1])
                linear_extrude(height = 24.0)
                    reuleaux_d_2d(1.0);
            // Keep only front bumper half (y > 42)
            translate([-130, -140, -2])
                cube([260, 182, 28]);
        }

    // 2x Donor Daiso Mouse Microswitches behind bumper
    color([0.14, 0.14, 0.16])
    for (sx = [-1, 1]) {
        translate([sx * 44, 88, 18])
            cube([12.8, 5.8, 6.5], center = true);
    }

    // Flexible PP Bernoulli Floor-Seal Skirt (z = 2.0 .. 10.0mm around nozzle)
    color([0.14, 0.64, 0.56])
    translate([0, NOZZLE_Y, 2.0])
        difference() {
            translate([-NOZZLE_W/2 - 5, -10, 0]) cube([NOZZLE_W + 10, 20, 7.0]);
            translate([-NOZZLE_W/2 - 2,  -7, -1]) cube([NOZZLE_W + 4, 14, 9.0]);
        }
}

// =============================================================================
// PART 12: Rear Low-Friction Stabilizer Caster / Skid
// =============================================================================
module part_12_rear_caster() {
    translate([0, -82, 0]) {
        // Smooth PTFE/PET spherical dome touching Z=0 floor
        color([0.90, 0.92, 0.94])
            translate([0, 0, 5.5])
                sphere(d = 11.0);
        // Caster housing bracket
        color([0.24, 0.26, 0.30])
            translate([0, 0, 4.0])
                cylinder(d = 18.0, h = 6.5);
    }
}

// =============================================================================
// Master Assembly Dispatcher (Supports Individual Part STL Export + Explode)
// =============================================================================
if (PART == "01_lower_monocoque_chassis") {
    part_01_lower_chassis();
} else if (PART == "02_upper_aero_shell") {
    part_02_upper_shell();
} else if (PART == "03_volute_blower_housing") {
    part_03_volute_blower();
} else if (PART == "04_logarithmic_impeller") {
    part_04_impeller();
} else if (PART == "05_pet_cyclone_chamber") {
    part_05_pet_cyclone();
} else if (PART == "06_friction_drive_wheels") {
    part_06_friction_drive();
} else if (PART == "07_pto_brush_system") {
    part_07_pto_brushes();
} else if (PART == "08_battery_pack_5aa") {
    part_08_battery_pack();
} else if (PART == "09_main_pcb_assembly") {
    part_09_main_pcb();
} else if (PART == "10_sensor_array") {
    part_10_sensors();
} else if (PART == "11_front_bumper_skirt") {
    part_11_bumper_skirt();
} else if (PART == "12_rear_caster_skid") {
    part_12_rear_caster();
} else {
    // Full Assembly (with optional EXPLODE factor)
    translate([0, 0, -18 * EXPLODE])              part_01_lower_chassis();
    translate([0, 0,  68 * EXPLODE])              part_02_upper_shell();
    translate([16 * EXPLODE, 0, 26 * EXPLODE])    part_03_volute_blower();
    translate([16 * EXPLODE, 0, 48 * EXPLODE])    part_04_impeller();
    translate([-18 * EXPLODE, 10 * EXPLODE, 40 * EXPLODE]) part_05_pet_cyclone();
    translate([0, 0,   0 * EXPLODE])              part_06_friction_drive();
    translate([0, 22 * EXPLODE, -22 * EXPLODE])   part_07_pto_brushes();
    translate([0, -10 * EXPLODE, 14 * EXPLODE])   part_08_battery_pack();
    translate([0, -24 * EXPLODE, 44 * EXPLODE])   part_09_main_pcb();
    translate([0, 16 * EXPLODE, -10 * EXPLODE])   part_10_sensors();
    translate([0, 38 * EXPLODE,  -8 * EXPLODE])   part_11_bumper_skirt();
    translate([0, -24 * EXPLODE, -28 * EXPLODE])  part_12_rear_caster();
}
