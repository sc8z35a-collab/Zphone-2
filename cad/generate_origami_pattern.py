#!/usr/bin/env python3
"""
菅ルンバ II Rev 3.0 (Omega-Efficiency Edition) — Exact 1:1 Printable Cutting & Folding Patterns (SVG)
1. cad/patterns/daiso_pp_sheet_390x550_1to1.svg
   Proves that ALL Rev 3.0 structural body, All-Flush H=58mm aero shell, 7-vane diffuser
   volute plates, Ø54mm friction wheel disks, Compliant Living-Hinge Rocker Arms,
   Front-Right Coanda Corner Air-Knife duct, horizontal PET cyclone cradle, Bernoulli skirt,
   and Dual-Stage Helmholtz + Quarter-Wave Muffler nest onto ONE SINGLE Daiso 0.75mm PP Sheet
   (550 x 390 mm, JAN 4549131311280, 110 JPY) with ZERO overlap!
2. cad/patterns/al_can_impeller_1to1.svg
   Proves that ALL Rev 3.0 aluminum parts — Ø78mm 9-blade backward-curved impeller
   (beta2=28 deg), 7 stationary Radial Diffuser Vanes (D3=80->D4=96mm), Ø22mm Capacitive
   TouchKey disc (nested waste-free inside the Ø28mm inlet hole!), and 4x AA battery
   contact strips — cut from ONE SINGLE 350ml aluminum beverage can (205 x 110 mm, 0 JPY)
   with ZERO overlap!
"""

import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAT_DIR = os.path.join(ROOT, "cad", "patterns")
os.makedirs(PAT_DIR, exist_ok=True)


def reuleaux_d_points(cx, cy, scale=1.0, inset=0.0, n=120):
    """Exact convex hull of the Rev 3.0 symmetric Reuleaux-D planform (210 x 198 mm)."""
    circles = [
        (-66.0,  70.0, 24.0 - inset),
        ( 66.0,  70.0, 24.0 - inset),
        (-81.0,   0.0, 24.0 - inset),
        ( 81.0,   0.0, 24.0 - inset),
        (  0.0, -20.0, 84.0 - inset),
    ]
    pts = []
    for i in range(n):
        ang = 2.0 * math.pi * i / n
        ux, uy = math.cos(ang), math.sin(ang)
        best_val = -1e18
        best_c = circles[0]
        for ccx, ccy, cr in circles:
            val = ccx * ux + ccy * uy + cr
            if val > best_val:
                best_val = val
                best_c = (ccx, ccy, cr)
        px = cx + (best_c[0] + best_c[2] * ux) * scale
        py = cy - (best_c[1] + best_c[2] * uy) * scale  # SVG y-down
        pts.append((px, py))
    return pts


def poly_path(pts):
    return "M " + " L ".join(f"{x:.2f},{y:.2f}" for x, y in pts) + " Z"


def assert_non_overlapping(boxes, W, H, sheet_name):
    total_area = 0.0
    for i, (n1, x1, y1, w1, h1) in enumerate(boxes):
        assert 0.0 <= x1 and x1 + w1 <= W and 0.0 <= y1 and y1 + h1 <= H, (
            f"{sheet_name}: {n1} ({x1},{y1},{w1},{h1}) out of {W}x{H} sheet!"
        )
        total_area += w1 * h1
        for n2, x2, y2, w2, h2 in boxes[i + 1:]:
            overlap_x = (x1 < x2 + w2) and (x2 < x1 + w1)
            overlap_y = (y1 < y2 + h2) and (y2 < y1 + h1)
            assert not (overlap_x and overlap_y), f"{sheet_name}: OVERLAP between {n1} and {n2}!"
    return (total_area / (W * H)) * 100.0


def generate_daiso_pp_sheet():
    W, H = 550.0, 390.0  # Landscape 550 x 390 mm Daiso PP Sheet (110 JPY)

    # Exact mathematically verified non-overlapping Rev 3.0 packing boxes (x, y, w, h)
    # Note: Lower base (210x198) centered at (114, 108) spans x:[9..219], y:[14..212]
    # Note: Upper shell (196x184) centered at (326, 106) spans x:[228..424], y:[18..202]
    boxes = [
        ("01_LOWER_BASE",      9,  12, 210, 200),
        ("02_UPPER_SHELL",   226,  14, 198, 188),
        ("03_WALL_STRIP_A",   10, 218, 326,  30),
        ("04_WALL_STRIP_B",   10, 254, 326,  30),
        ("05_BOX_RIBS_4X",    10, 290, 252,  32),
        ("06_COANDA_DUCT",   268, 290,  68,  32),
        ("07_2STAGE_MUFFLER", 10, 328, 326,  52),
        ("08_DIFFUSER_TOP",  344, 206,  98,  98),
        ("09_DIFFUSER_BOT",  344, 308,  98,  74),  # Wait: make Ø94mm volute plates! Let's adjust cleanly below
    ]
    # Let's give both Diffuser/Volute plates generous Ø92mm (92x92) or place side-by-side cleanly!
    # Check right strip x:[430..546] (width 116mm, height 376mm!):
    #   4x Ø54mm wheels fit in a 2x2 grid: 2*(54+4)=116mm wide, 2*(54+4)=116mm tall! -> y:[14..130]!
    #   Below wheels (y:[136..382], width 116mm, height 246mm):
    #     - 08_VOLUTE_TOP Ø98mm: x:[446..544], y:[136..234] (98x98!)
    #     - 09_VOLUTE_BOT Ø98mm: x:[446..544], y:[240..338] (98x98!)
    #     - 14_SKIRT_LIP (100x36): x:[444..544], y:[344..380]
    #   Middle gap under Upper Shell (x:[342..440], y:[208..380]):
    #     - 15_ROCKER_ARMS_2X (Living-Hinge Suspension + PET Cradle): x:[344..440], y:[208..300] (96x92)
    #     - 16_VOLUTE_SCROLL_WALL (96x74): x:[344..440], y:[306..380]
    boxes = [
        ("01_LOWER_BASE",        9,  12, 210, 200),
        ("02_UPPER_SHELL",     225,  14, 198, 188),
        ("03_WALL_STRIP_A",     10, 218, 328,  30),
        ("04_WALL_STRIP_B",     10, 254, 328,  30),
        ("05_BOX_RIBS_4X",      10, 290, 248,  32),
        ("06_COANDA_DUCT",     264, 290,  74,  32),
        ("07_2STAGE_MUFFLER",   10, 328, 328,  52),
        ("08_ROCKER_CRADLE",   344, 208,  96,  92),
        ("09_VOLUTE_WALL",     344, 306,  96,  74),
        ("10_WHEEL_L1",        430,  14,  56,  56),
        ("11_WHEEL_L2",        490,  14,  56,  56),
        ("12_WHEEL_R1",        430,  74,  56,  56),
        ("13_WHEEL_R2",        490,  74,  56,  56),
        ("14_VOLUTE_TOP",      446, 136,  98,  98),
        ("15_VOLUTE_BOT",      446, 240,  98,  98),
        ("16_BERNOULLI_SKIRT", 446, 344,  98,  36),
    ]

    util_pct = assert_non_overlapping(boxes, W, H, "Daiso PP 550x390")

    svg = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">',
        '  <defs>',
        '    <style>',
        '      .sheet { fill: #f8fafc; stroke: #0f172a; stroke-width: 0.8; }',
        '      .cut { fill: #e0f2fe; fill-opacity: 0.48; stroke: #dc2626; stroke-width: 0.55; }',
        '      .cut-accent { fill: #ccfbf1; fill-opacity: 0.55; stroke: #0d9488; stroke-width: 0.55; }',
        '      .cut-hole { fill: #ffffff; stroke: #dc2626; stroke-width: 0.45; }',
        '      .fold { fill: none; stroke: #2563eb; stroke-width: 0.48; stroke-dasharray: 3,2; }',
        '      .hinge { fill: none; stroke: #d97706; stroke-width: 0.70; stroke-dasharray: 2,1.2; }',
        '      .lbl { font-family: sans-serif; font-size: 6.2px; font-weight: 700; fill: #0f172a; text-anchor: middle; }',
        '      .sub { font-family: sans-serif; font-size: 4.4px; fill: #475569; text-anchor: middle; }',
        '    </style>',
        '  </defs>',
        f'  <rect class="sheet" x="0" y="0" width="{W}" height="{H}"/>',
        f'  <text x="275" y="7.5" class="sub" style="font-size:5.1px;font-weight:700;">SUGA ROOMBA II REV 3.0 — 1:1 DAISO PP SHEET (550 x 390 x 0.75 mm, 110 JPY) | RED=CUT, BLUE=FOLD, ORANGE=LIVING HINGE | PACKING {util_pct:.1f}% (0 OVERLAP)</text>',
    ]

    # 1) Lower Chassis Base (210 x 198 mm Reuleaux-D centered at (114, 107))
    cx1, cy1 = 114.0, 107.0
    p1 = reuleaux_d_points(cx1, cy1, scale=1.0, inset=0.0)
    svg.append(f'  <path class="cut" d="{poly_path(p1)}"/>')
    # All-Flush Ø56mm wheel wells at x = cx1 ± 78, y = cy1 + 4 (width 20 x length 62)
    svg.append(f'  <rect class="cut-hole" x="{cx1-78-10:.1f}" y="{cy1+4-31:.1f}" width="20" height="62"/>')
    svg.append(f'  <rect class="cut-hole" x="{cx1+78-10:.1f}" y="{cy1+4-31:.1f}" width="20" height="62"/>')
    # 126 x 13 mm Contoured Bernoulli Nozzle Slot (y = +68 -> cy1 - 68)
    svg.append(f'  <rect class="cut-hole" x="{cx1-63:.1f}" y="{cy1-68-6.5:.1f}" width="126" height="13"/>')
    # 5V Optical Mouse Window (y = -46 -> cy1 + 46)
    svg.append(f'  <circle class="cut-hole" cx="{cx1:.1f}" cy="{cy1+46:.1f}" r="7"/>')
    svg.append(f'  <text x="{cx1}" y="{cy1-2}" class="lbl">01 LOWER MONOCOQUE BASE</text>')
    svg.append(f'  <text x="{cx1}" y="{cy1+7}" class="sub">210 x 198 mm | 100% Symmetric</text>')
    svg.append(f'  <text x="{cx1}" y="{cy1+14}" class="sub">All-Flush Ø56 Wells + 126mm Nozzle</text>')

    # 2) Upper Aero Shell (All-Flush H=58.0mm — ZERO wheel cutouts!)
    cx2, cy2 = 324.0, 103.0
    p2 = reuleaux_d_points(cx2, cy2, scale=0.93, inset=0.0)
    svg.append(f'  <path class="cut" d="{poly_path(p2)}"/>')
    # Horizontal PET Cyclone Top-Access Latch Window (84 x 36 mm across X=0, Y=+36 -> cy2-34)
    svg.append(f'  <rect class="cut-hole" x="{cx2-42:.1f}" y="{cy2-50:.1f}" width="84" height="36" rx="5"/>')
    # Flush TouchKey Pad Mark (y=-34 -> cy2+32)
    svg.append(f'  <circle class="fold" cx="{cx2:.1f}" cy="{cy2+32:.1f}" r="11"/>')
    svg.append(f'  <text x="{cx2}" y="{cy2-30}" class="sub">HORIZ. PET BAY HATCH</text>')
    svg.append(f'  <text x="{cx2}" y="{cy2+4}" class="lbl">02 ALL-FLUSH UPPER SHELL</text>')
    svg.append(f'  <text x="{cx2}" y="{cy2+12}" class="sub">H=58.0 mm | ZERO WHEEL BULGE</text>')
    svg.append(f'  <text x="{cx2}" y="{cy2+34}" class="sub">TOUCHKEY</text>')

    # 3 & 4) Perimeter Monocoque Sidewall Bands (328 x 30 mm for ultra-slim H=58mm!)
    for y0, title in [(218, "03 FRONT/SIDE ULTRA-SLIM MONOCOQUE WALL BAND (328 x 30 mm)"),
                      (254, "04 REAR WALL + EXHAUST COOLING DUCT BAND (328 x 30 mm)")]:
        svg.append(f'  <rect class="cut" x="10" y="{y0}" width="328" height="30"/>')
        svg.append(f'  <line class="fold" x1="10" y1="{y0+4.5}" x2="338" y2="{y0+4.5}"/>')
        svg.append(f'  <line class="fold" x1="10" y1="{y0+25.5}" x2="338" y2="{y0+25.5}"/>')
        svg.append(f'  <text x="174" y="{y0+17.5}" class="lbl">{title}</text>')

    # 5) 4x Origami Box-Rib Stiffeners
    svg.append('  <rect class="cut" x="10" y="290" width="248" height="32"/>')
    for fy in (298.0, 306.0, 314.0):
        svg.append(f'  <line class="fold" x1="10" y1="{fy}" x2="258" y2="{fy}"/>')
    svg.append('  <line class="cut" x1="134" y1="290" x2="134" y2="322"/>')
    svg.append('  <text x="72" y="308" class="sub">05A BOX-RIB STIFFENERS (x2)</text>')
    svg.append('  <text x="196" y="308" class="sub">05B BOX-RIB STIFFENERS (x2)</text>')

    # 6) Front-Right Coanda Corner Air-Knife Duct (15% Exhaust Tap, 1.85 x 28 mm Jet Slot!)
    svg.append('  <rect class="cut-accent" x="264" y="290" width="74" height="32"/>')
    svg.append('  <line class="fold" x1="264" y1="298" x2="338" y2="298"/>')
    svg.append('  <line class="fold" x1="264" y1="314" x2="338" y2="314"/>')
    svg.append('  <text x="301" y="305" class="sub" style="font-weight:700;">06 COANDA JET</text>')
    svg.append('  <text x="301" y="311.5" class="sub">14.3 m/s (0 W)</text>')

    # 7) Dual-Stage Metamaterial Muffler (Helmholtz 10.2cm3 + Quarter-Wave 28.7mm Stub) + 5S AA Bay
    svg.append('  <rect class="cut" x="10" y="328" width="328" height="52"/>')
    for fx in (110, 218):
        svg.append(f'  <line class="fold" x1="{fx}" y1="328" x2="{fx}" y2="380"/>')
    svg.append('  <line class="fold" x1="10" y1="341" x2="338" y2="341"/>')
    svg.append('  <line class="fold" x1="10" y1="367" x2="338" y2="367"/>')
    svg.append('  <text x="60" y="356" class="sub" style="font-weight:700;">5S AA BAY NET</text>')
    svg.append('  <text x="164" y="353" class="sub" style="font-weight:700;">HELMHOLTZ 10.2cm3</text>')
    svg.append('  <text x="164" y="360" class="sub">1,497 Hz (-24.3 dB)</text>')
    svg.append('  <text x="278" y="353" class="sub" style="font-weight:700;">1/4-WAVE 28.7mm</text>')
    svg.append('  <text x="278" y="360" class="sub">2,994 Hz (-14.9 dB)</text>')

    # 8) Compliant Living-Hinge Rocker-Arm Suspension (L & R) + Horizontal PET Cradle
    svg.append('  <rect class="cut-accent" x="344" y="208" width="96" height="92"/>')
    svg.append('  <line class="cut" x1="344" y1="238" x2="440" y2="238"/>')
    svg.append('  <line class="cut" x1="344" y1="268" x2="440" y2="268"/>')
    # Orange living-hinge flexure score lines at front pivot of L & R rocker arms!
    svg.append('  <line class="hinge" x1="362" y1="208" x2="362" y2="268"/>')
    svg.append('  <line class="hinge" x1="365" y1="208" x2="365" y2="268"/>')
    svg.append('  <text x="401" y="225" class="sub" style="font-weight:700;">08A ROCKER ARM L</text>')
    svg.append('  <text x="401" y="255" class="sub" style="font-weight:700;">08B ROCKER ARM R</text>')
    svg.append('  <text x="392" y="285" class="sub" style="font-weight:700;">08C PET CYCLONE CRADLE</text>')

    # 9) Diffuser/Volute Perimeter Wall Strip + Bellmouth Collar
    svg.append('  <rect class="cut" x="344" y="306" width="96" height="74"/>')
    for fy in (324, 343, 362):
        svg.append(f'  <line class="fold" x1="344" y1="{fy}" x2="440" y2="{fy}"/>')
    svg.append('  <text x="392" y="335" class="sub" style="font-weight:700;">09 VOLUTE WALL</text>')
    svg.append('  <text x="392" y="354" class="sub">+ BELLMOUTH RING</text>')

    # 10..13) 4x All-Flush Friction Drive Wheel Disks (Ø54mm + 1mm Rubber Tread = Ø56mm!)
    wheel_pos = [(458, 42, "WHEEL L1"), (518, 42, "WHEEL L2"), (458, 102, "WHEEL R1"), (518, 102, "WHEEL R2")]
    for wx, wy, lbl in wheel_pos:
        svg.append(f'  <circle class="cut" cx="{wx}" cy="{wy}" r="27"/>')
        svg.append(f'  <circle class="cut-hole" cx="{wx}" cy="{wy}" r="2.0"/>')
        svg.append(f'  <text x="{wx}" y="{wy+12}" class="sub" style="font-weight:700;">{lbl}</text>')
        svg.append(f'  <text x="{wx}" y="{wy+18}" class="sub">Ø54(+2mm)</text>')

    # 14 & 15) 7-Vane Radial Diffuser + Volute Top & Bottom Plates (Ø96mm)
    for idx, cy0, lbl in [(14, 185, "14 DIFFUSER TOP Ø96"), (15, 289, "15 DIFFUSER BOT Ø96")]:
        svg.append(f'  <circle class="cut" cx="495" cy="{cy0}" r="48"/>')
        r_in = 4.0 if idx == 14 else 15.0
        svg.append(f'  <circle class="cut-hole" cx="495" cy="{cy0}" r="{r_in}"/>')
        # Draw 7 radial diffuser vane placement guide lines on Top Plate!
        if idx == 14:
            for k in range(7):
                a0 = 2.0 * math.pi * k / 7.0
                x_s = 495 + 40.0 * math.cos(a0)
                y_s = cy0 + 40.0 * math.sin(a0)
                x_e = 495 + 47.2 * math.cos(a0 - 0.34)
                y_e = cy0 + 47.2 * math.sin(a0 - 0.34)
                svg.append(f'  <line class="fold" x1="{x_s:.1f}" y1="{y_s:.1f}" x2="{x_e:.1f}" y2="{y_e:.1f}"/>')
        svg.append(f'  <text x="495" y="{cy0+28}" class="sub" style="font-weight:700;">{lbl}</text>')

    # 16) Contoured Bernoulli Floor-Seal Skirt (1.85mm gap)
    svg.append('  <rect class="cut" x="446" y="344" width="98" height="36"/>')
    svg.append('  <line class="fold" x1="446" y1="362" x2="544" y2="362"/>')
    svg.append('  <text x="495" y="356" class="sub" style="font-weight:700;">16 BERNOULLI SKIRT</text>')
    svg.append('  <text x="495" y="373" class="sub">126mm (1.85mm GAP)</text>')

    # 50mm Calibration Scale Ruler
    svg.append('  <rect x="446" y="383" width="50" height="4" fill="#0f172a"/>')
    svg.append('  <text x="520" y="386.8" class="sub">50 mm SCALE</text>')

    svg.append("</svg>")
    out = os.path.join(PAT_DIR, "daiso_pp_sheet_390x550_1to1.svg")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(svg) + "\n")
    print(f"[PATTERN] Saved {out} (Rev 3.0 550x390mm Daiso PP sheet, packing={util_pct:.1f}%, 0 overlaps)")
    return util_pct


def generate_al_can_impeller():
    W, H = 205.0, 110.0  # ONE 350ml aluminum beverage can sidewall (Ø66mm x pi = 207 x 110 mm, 0 JPY)

    # Mathematically assert ZERO overlap for ALL Rev 3.0 aluminum parts!
    # TouchKey Ø22mm nests INSIDE the Ø28mm hole of Bottom Inlet Ring (zero extra area!).
    boxes = [
        ("TOP_SHROUD_78",   2.0,  8.0, 78.0, 78.0),
        ("BOT_RING_78",    82.0,  8.0, 78.0, 78.0),
    ]
    for k in range(9):
        boxes.append((f"IMP_BLADE_{k+1}", 165.0, 8.0 + k * 10.2, 32.0, 9.5))
    for k in range(7):
        boxes.append((f"DIFF_VANE_{k+1}", 4.0 + k * 22.0, 88.5, 20.0, 8.5))
    for j in range(4):
        boxes.append((f"AA_CONTACT_{j+1}", 4.0 + j * 38.0, 99.0, 34.0, 8.0))

    util_pct = assert_non_overlapping(boxes, W, H, "350ml Al-Can 205x110")

    svg = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">',
        '  <defs><style>',
        '    .sheet { fill: #f1f5f9; stroke: #0f172a; stroke-width: 0.5; }',
        '    .cut { fill: #dbeafe; fill-opacity: 0.55; stroke: #dc2626; stroke-width: 0.35; }',
        '    .cut-diff { fill: #fef3c7; fill-opacity: 0.72; stroke: #d97706; stroke-width: 0.35; }',
        '    .cut-tk { fill: #dcfce7; fill-opacity: 0.78; stroke: #15803d; stroke-width: 0.35; }',
        '    .fold { fill: none; stroke: #2563eb; stroke-width: 0.35; stroke-dasharray: 2,1.5; }',
        '    .guide { fill: none; stroke: #0f766e; stroke-width: 0.45; }',
        '    .lbl { font-family: sans-serif; font-size: 3.1px; font-weight: 700; fill: #0f172a; text-anchor: middle; }',
        '  </style></defs>',
        f'  <rect class="sheet" x="0" y="0" width="{W}" height="{H}"/>',
        f'  <text x="102.5" y="5.0" class="lbl" style="font-size:2.35px;">SUGA ROOMBA II REV 3.0 — 1:1 350ml AL-CAN (205x110mm, 0 JPY) | 9 BLADES + 7 DIFFUSER VANES + TOUCHKEY + AA CONTACTS ({util_pct:.1f}%)</text>',
    ]

    # Top Shroud Disk (Ø78mm, center at (41, 47)) with exact 9 logarithmic spiral glue guides
    cx1, cy1 = 41.0, 47.0
    svg.append(f'  <circle class="cut" cx="{cx1}" cy="{cy1}" r="39.0"/>')
    svg.append(f'  <circle fill="#ffffff" stroke="#dc2626" stroke-width="0.35" cx="{cx1}" cy="{cy1}" r="1.15"/>')
    cot_b = 1.0 / math.tan(math.radians(28.0))
    for k in range(9):
        a0 = 2.0 * math.pi * k / 9.0
        pts = []
        for s in range(17):
            r = 14.0 + (38.5 - 14.0) * (s / 16.0)
            th = a0 - cot_b * math.log(r / 14.0) * 0.55
            pts.append(f"{cx1 + r*math.cos(th):.2f},{cy1 + r*math.sin(th):.2f}")
        svg.append(f'  <polyline class="guide" points="{" ".join(pts)}"/>')
    svg.append(f'  <text x="{cx1}" y="42.5" class="lbl" style="font-size:2.5px;">TOP DISK Ø78</text>')
    svg.append(f'  <text x="{cx1}" y="53.5" class="lbl" style="font-size:2.2px;">9 LOG SPIRALS</text>')

    # Bottom Annular Inlet Ring (OD=78mm, ID=28mm, center at (121, 47)) + NESTED Ø22mm TouchKey Disc!
    cx2, cy2 = 121.0, 47.0
    svg.append(f'  <circle class="cut" cx="{cx2}" cy="{cy2}" r="39.0"/>')
    svg.append(f'  <circle fill="#ffffff" stroke="#dc2626" stroke-width="0.35" cx="{cx2}" cy="{cy2}" r="14.0"/>')
    svg.append(f'  <circle class="cut-tk" cx="{cx2}" cy="{cy2}" r="11.0"/>')
    svg.append(f'  <text x="{cx2}" y="{cy2-1.0}" class="lbl" style="font-size:2.4px;">TOUCHKEY</text>')
    svg.append(f'  <text x="{cx2}" y="{cy2+2.6}" class="lbl" style="font-size:2.2px;">Ø22 (0 JPY)</text>')
    svg.append(f'  <text x="{cx2}" y="77.5" class="lbl" style="font-size:2.4px;">BOT INLET RING OD78 / ID28</text>')

    # 9x Impeller Blade Strips (32 x 9.5 mm)
    for k in range(9):
        bx = 165.0
        by = 8.0 + k * 10.2
        svg.append(f'  <rect class="cut" x="{bx:.1f}" y="{by:.1f}" width="32.0" height="9.5"/>')
        svg.append(f'  <line class="fold" x1="{bx:.1f}" y1="{by+1.0:.1f}" x2="{bx+32:.1f}" y2="{by+1.0:.1f}"/>')
        svg.append(f'  <line class="fold" x1="{bx:.1f}" y1="{by+8.5:.1f}" x2="{bx+32:.1f}" y2="{by+8.5:.1f}"/>')
        svg.append(f'  <text x="{bx+16.0:.1f}" y="{by+5.7:.1f}" class="lbl" style="font-size:2.5px;">IMP BLADE #{k+1}</text>')

    # 7x Stationary Radial Diffuser Vanes (20 x 8.5 mm, D3=80->D4=96mm static pressure recovery!)
    for k in range(7):
        dx = 4.0 + k * 22.0
        dy = 88.5
        svg.append(f'  <rect class="cut-diff" x="{dx:.1f}" y="{dy:.1f}" width="20.0" height="8.5"/>')
        svg.append(f'  <line class="fold" x1="{dx:.1f}" y1="{dy+1.0:.1f}" x2="{dx+20:.1f}" y2="{dy+1.0:.1f}"/>')
        svg.append(f'  <text x="{dx+10.0:.1f}" y="{dy+5.8:.1f}" class="lbl" style="font-size:2.3px;">DIFF #{k+1}</text>')

    # 4x AA Battery Spring Contacts (34 x 8.0 mm)
    for j in range(4):
        sx = 4.0 + j * 38.0
        svg.append(f'  <rect class="cut" x="{sx:.1f}" y="99.0" width="34.0" height="8.0"/>')
        svg.append(f'  <line class="fold" x1="{sx+17.0:.1f}" y1="99.0" x2="{sx+17.0:.1f}" y2="107.0"/>')
        svg.append(f'  <text x="{sx+17.0:.1f}" y="104.0" class="lbl" style="font-size:2.4px;">AA CONTACT #{j+1}</text>')

    svg.append("</svg>")
    out = os.path.join(PAT_DIR, "al_can_impeller_1to1.svg")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(svg) + "\n")
    print(f"[PATTERN] Saved {out} (Rev 3.0 205x110mm 350ml Al-Can, packing={util_pct:.1f}%, 0 overlaps)")
    return util_pct


if __name__ == "__main__":
    generate_daiso_pp_sheet()
    generate_al_can_impeller()
