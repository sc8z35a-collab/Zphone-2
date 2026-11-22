#!/usr/bin/env python3
"""
菅ルンバ II Rev 4.0 (Hyper-Genesis Industrial-Fab Edition)
OpenSCAD STL Compiler, Trimesh CoG/Mass & JLC3DP Resin-Volume Cost Verifier,
JLC3DP 1-Click Upload Packager (ZIP), and Compact WebGL2 Mesh Bundler.
"""

import base64
import concurrent.futures
import json
import os
import subprocess
import zipfile
import numpy as np
import trimesh

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAD_FILE = os.path.join(ROOT, "cad", "suga_roomba_ii.scad")
STL_DIR = os.path.join(ROOT, "cad", "stl")
JLC_DIR = os.path.join(ROOT, "cad", "jlc3dp_order_pack")
VIEWER_DIR = os.path.join(ROOT, "cad-viewer")
os.makedirs(STL_DIR, exist_ok=True)
os.makedirs(JLC_DIR, exist_ok=True)
os.makedirs(VIEWER_DIR, exist_ok=True)

# 12 Modular Subsystems (Rev 4.0 exact 405.1 g BOM mass accounting!)
# (id, real BOM mass [g], PBR hex color, opacity, metalness, roughness, explode_vec [mm])
PARTS = [
    ("01_lower_monocoque_chassis",  36.0, "#eef2f6", 1.00, 0.08, 0.42, [0.0,   0.0, -24.0]),
    ("02_upper_aero_shell",         28.0, "#1e293b", 1.00, 0.16, 0.34, [0.0,   0.0,  72.0]),
    ("03_volute_blower_housing",    92.0, "#2563eb", 1.00, 0.35, 0.32, [0.0, -10.0,  24.0]),
    ("04_logarithmic_impeller",      9.0, "#e2e8f0", 1.00, 0.55, 0.22, [0.0, -10.0,  52.0]),
    ("05_pet_cyclone_chamber",      18.0, "#38bdf8", 0.56, 0.08, 0.14, [0.0,  18.0,  46.0]),
    ("06_friction_drive_wheels",    52.0, "#ea580c", 1.00, 0.22, 0.44, [0.0,   0.0, -10.0]),
    ("07_pto_brush_system",          5.0, "#eab308", 1.00, 0.15, 0.50, [0.0,  30.0, -22.0]),
    ("08_battery_pack_5aa",        131.0, "#15803d", 1.00, 0.40, 0.35, [0.0, -26.0,  14.0]),
    ("09_main_pcb_assembly",        13.2, "#0f766e", 1.00, 0.28, 0.40, [0.0, -32.0,  52.0]),
    ("10_sensor_array",             12.9, "#dc2626", 1.00, 0.22, 0.35, [0.0,  16.0, -12.0]),
    ("11_front_bumper_skirt",        5.0, "#334155", 1.00, 0.10, 0.50, [0.0,  44.0,  -6.0]),
    ("12_rear_caster_skid",          3.0, "#94a3b8", 1.00, 0.50, 0.25, [0.0, -28.0, -28.0]),
]

# Wait: check exact BOM mass sum!
# 36+28+92+9+18+52+5+131+13.2+12.9+5+3 = 405.1 g EXACTLY!

JLC_PARTS = [
    ("jlc_01_fluid_unibody",       "01_monolithic_fluid_chassis.stl"),
    ("jlc_02_canopy_and_wheels",   "02_aero_canopy_and_dustcup.stl"),
    ("jlc_03_3d_twisted_impeller", "03_3d_twisted_shrouded_impeller.stl"),
]


def compile_one(args):
    part_id, out_stl, extra_defs = args
    cmd = ["openscad", "-o", out_stl, "-D", f'PART="{part_id}"']
    for k, v in extra_defs.items():
        cmd += ["-D", f"{k}={v}"]
    cmd.append(SCAD_FILE)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"OpenSCAD failed on {part_id}:\n{proc.stderr}")
    return part_id, out_stl


def main():
    tasks = []
    for pid, *_ in PARTS:
        tasks.append((pid, os.path.join(STL_DIR, f"{pid}.stl"), {}))
    tasks.append(("all", os.path.join(STL_DIR, "suga_roomba_ii_full_assembly.stl"), {"EXPLODE": 0.0}))
    for jpid, jfname in JLC_PARTS:
        tasks.append((jpid, os.path.join(JLC_DIR, jfname), {}))

    print(f"[CAD REV 4.0] Compiling {len(tasks)} OpenSCAD STL targets (12 viz + full + 3 JLC3DP prod) in parallel...")
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as ex:
        for pid, stl_path in ex.map(compile_one, tasks):
            sz_kb = os.path.getsize(stl_path) / 1024.0
            print(f"  [OK] {pid:30s} -> {sz_kb:7.1f} KB")

    # 1. Verify 12 Visualization Meshes, 3D CoG, Envelope, and Pack WebGL2 Bundle
    part_reports = []
    bundle_parts = []
    total_mass = 0.0
    weighted_cog = np.zeros(3)
    overall_min = np.array([1e9, 1e9, 1e9])
    overall_max = np.array([-1e9, -1e9, -1e9])

    for idx, (pid, mass_g, color_hex, opacity, metal, rough, explode_v) in enumerate(PARTS):
        stl_path = os.path.join(STL_DIR, f"{pid}.stl")
        mesh = trimesh.load(stl_path, force="mesh")
        bounds = mesh.bounds
        dims = bounds[1] - bounds[0]
        centroid = mesh.center_mass if mesh.is_watertight else mesh.centroid
        vol_cm3 = abs(float(mesh.volume)) / 1000.0

        overall_min = np.minimum(overall_min, bounds[0])
        overall_max = np.maximum(overall_max, bounds[1])
        total_mass += mass_g
        weighted_cog += centroid * mass_g

        part_reports.append({
            "step": idx + 1, "id": pid, "mass_g": mass_g,
            "triangles": int(len(mesh.faces)),
            "watertight": bool(mesh.is_watertight),
            "volume_cm3": round(vol_cm3, 2),
            "bounds_mm": [[round(float(v), 2) for v in bounds[0]], [round(float(v), 2) for v in bounds[1]]],
            "dims_mm": [round(float(v), 2) for v in dims],
            "centroid_mm": [round(float(v), 2) for v in centroid],
        })

        tris = mesh.triangles.astype(np.float32)
        fnorms = np.repeat(mesh.face_normals.astype(np.float32)[:, None, :], 3, axis=1)
        bundle_parts.append({
            "step": idx + 1, "id": pid, "color": color_hex, "opacity": opacity,
            "metalness": metal, "roughness": rough, "explode": explode_v,
            "triCount": int(len(mesh.faces)),
            "posB64": base64.b64encode(tris.reshape(-1).tobytes()).decode("ascii"),
            "nrmB64": base64.b64encode(fnorms.reshape(-1).tobytes()).decode("ascii"),
        })

    # 2. Verify 3 JLC3DP Production Print Parts (Watertightness, Exact Solid Resin cm3 & JPY Quote!)
    jlc_reports = []
    jlc_total_vol = 0.0
    zip_path = os.path.join(JLC_DIR, "SugaRoomba2_Rev4_JLC3DP_Pack.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for jpid, jfname in JLC_PARTS:
            fpath = os.path.join(JLC_DIR, jfname)
            m = trimesh.load(fpath, force="mesh")
            vol_cm3 = abs(float(m.volume)) / 1000.0
            mass_resin_g = vol_cm3 * 1.15  # 9600 Resin cured density ~1.15 g/cm^3
            dims = m.bounds[1] - m.bounds[0]
            bodies = int(m.body_count)
            jlc_total_vol += vol_cm3
            jlc_reports.append({
                "file": jfname,
                "watertight": bool(m.is_watertight),
                "connected_bodies": bodies,
                "triangles": int(len(m.faces)),
                "volume_cm3": round(vol_cm3, 2),
                "resin_mass_g": round(mass_resin_g, 1),
                "dims_mm": [round(float(v), 2) for v in dims],
            })
            zf.write(fpath, arcname=jfname)

    # JLC3DP 9600 Resin SLA benchmark cost model ($0.30/part base + ~$0.042/cm3, 150 JPY/USD)
    jlc_est_usd = 0.30 * len(JLC_PARTS) + jlc_total_vol * 0.042
    jlc_est_jpy = int(round(jlc_est_usd * 150.0))

    cog = weighted_cog / total_mass
    overall_dims = overall_max - overall_min

    # Static wheel-traction & stability triangle check (Rev 4.0: wheels at ±73, -4; caster at 0, -80)
    y_caster = -80.0
    y_wheels = -4.0
    wheel_load_frac = float(np.clip((cog[1] - y_caster) / (y_wheels - y_caster), 0.0, 1.0))
    inside_triangle = (abs(cog[0]) < 12.0) and (y_caster + 10.0 < cog[1] < 20.0)

    summary = {
        "revision": "Rev 4.0 Hyper-Genesis Industrial-Fab",
        "envelope_mm": {
            "width_X": round(float(overall_dims[0]), 2),
            "length_Y": round(float(overall_dims[1]), 2),
            "height_Z": round(float(overall_dims[2]), 2),
        },
        "total_mass_g": round(total_mass, 1),
        "center_of_gravity_mm": {
            "x": round(float(cog[0]), 2),
            "y": round(float(cog[1]), 2),
            "z": round(float(cog[2]), 2),
        },
        "drive_wheel_weight_fraction": round(wheel_load_frac, 4),
        "cog_inside_stability_triangle": bool(inside_triangle),
        "all_12_viz_watertight": all(p["watertight"] for p in part_reports),
        "all_watertight": all(p["watertight"] for p in part_reports) and all(j["watertight"] for j in jlc_reports),
        "total_triangles": sum(p["triangles"] for p in part_reports),
        "jlc3dp_order_pack": {
            "zip_file": "cad/jlc3dp_order_pack/SugaRoomba2_Rev4_JLC3DP_Pack.zip",
            "material_recommended": "JLC3DP SLA 9600 Resin (Matte White, General Sanding)",
            "total_resin_volume_cm3": round(jlc_total_vol, 2),
            "total_resin_mass_g": round(jlc_total_vol * 1.15, 1),
            "estimated_fab_usd": round(jlc_est_usd, 2),
            "estimated_fab_jpy": jlc_est_jpy,
            "all_3_prod_watertight": all(j["watertight"] for j in jlc_reports),
            "parts": jlc_reports,
        },
        "parts": part_reports,
    }

    with open(os.path.join(ROOT, "cad", "cad_verification.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    bundle_js = os.path.join(VIEWER_DIR, "stl_bundle.js")
    with open(bundle_js, "w", encoding="utf-8") as f:
        f.write("window.SUGA_CAD_BUNDLE = ")
        json.dump({"parts": bundle_parts, "meta": summary["envelope_mm"]}, f, separators=(",", ":"))
        f.write(";\n")

    print("\n=== REV 4.0 CAD & JLC3DP PRODUCTION PACK VERIFICATION ===")
    print(f"  Envelope (W x L x H) : {summary['envelope_mm']['width_X']} x {summary['envelope_mm']['length_Y']} x {summary['envelope_mm']['height_Z']} mm")
    print(f"  Total Mass           : {summary['total_mass_g']} g")
    print(f"  3D Center of Gravity : X={cog[0]:+.2f} mm, Y={cog[1]:+.2f} mm, Z={cog[2]:.2f} mm")
    print(f"  Wheel Traction Load  : {wheel_load_frac*100:.1f}% on drive wheels (inside triangle={inside_triangle})")
    print(f"  All 12 Viz Watertight: {summary['all_12_viz_watertight']} ({summary['total_triangles']} tris)")
    print(f"  JLC3DP 3-Part Pack   : {jlc_total_vol:.2f} cm3 ({jlc_total_vol*1.15:.1f} g) | Est. ${jlc_est_usd:.2f} (~{jlc_est_jpy} JPY) | All Watertight={summary['jlc3dp_order_pack']['all_3_prod_watertight']}")
    for jr in jlc_reports:
        print(f"    - {jr['file']:36s}: {jr['volume_cm3']:6.2f} cm3 ({jr['resin_mass_g']:5.1f} g) | watertight={jr['watertight']} | bodies={jr['connected_bodies']} | {jr['dims_mm']}")
    print(f"  JLC3DP Ready ZIP     : {os.path.getsize(zip_path)/1024:.1f} KB -> {zip_path}")
    print(f"  WebGL Bundle Size    : {os.path.getsize(bundle_js)/1024:.1f} KB -> {bundle_js}")


if __name__ == "__main__":
    main()
