#!/usr/bin/env python3
"""
菅ルンバ II (Suga Roomba II) — OpenSCAD STL Compiler, Trimesh CoG/Mass Verifier,
and Compact Binary-Base64 WebGL Mesh Packager.

Compiles all 12 parametric CAD subsystems + full assembly via real OpenSCAD,
verifies watertightness, bounding envelope, mass budget (494.9g), static Center of
Gravity (CoG) stability triangle & drive-wheel traction fraction, and packages
exact compiled triangle geometry for the zero-text WebGL 3D CAD Viewer.
"""

import base64
import concurrent.futures
import json
import os
import struct
import subprocess
import numpy as np
import trimesh

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAD_FILE = os.path.join(ROOT, "cad", "suga_roomba_ii.scad")
STL_DIR = os.path.join(ROOT, "cad", "stl")
VIEWER_DIR = os.path.join(ROOT, "cad-viewer")
os.makedirs(STL_DIR, exist_ok=True)
os.makedirs(VIEWER_DIR, exist_ok=True)

# 12 Modular Subsystems: (id, real BOM mass [g], PBR hex color, opacity, metalness, roughness, explode_vec [mm])
PARTS = [
    ("01_lower_monocoque_chassis", 68.0, "#e8edf2", 1.00, 0.05, 0.55, [0.0,   0.0, -32.0]),
    ("02_upper_aero_shell",        46.0, "#1f3247", 1.00, 0.12, 0.38, [0.0,   0.0,  78.0]),
    ("03_volute_blower_housing",   82.0, "#3b82c4", 1.00, 0.35, 0.35, [28.0,  6.0,  34.0]),
    ("04_logarithmic_impeller",     6.5, "#d7dce2", 1.00, 0.88, 0.22, [28.0,  6.0,  64.0]),
    ("05_pet_cyclone_chamber",     28.0, "#7dd3fc", 0.52, 0.05, 0.12, [-34.0, 14.0, 52.0]),
    ("06_friction_drive_wheels",   64.0, "#ea580c", 1.00, 0.25, 0.45, [0.0,   0.0, -10.0]),
    ("07_pto_brush_system",        22.0, "#eab308", 1.00, 0.15, 0.50, [0.0,  34.0, -28.0]),
    ("08_battery_pack_5aa",       130.0, "#15803d", 1.00, 0.40, 0.35, [0.0, -18.0,  16.0]),
    ("09_main_pcb_assembly",       24.4, "#0f766e", 1.00, 0.25, 0.42, [0.0, -34.0,  56.0]),
    ("10_sensor_array",            11.0, "#dc2626", 1.00, 0.20, 0.35, [0.0,  22.0, -16.0]),
    ("11_front_bumper_skirt",       9.0, "#334155", 1.00, 0.08, 0.55, [0.0,  50.0,  -6.0]),
    ("12_rear_caster_skid",         4.0, "#94a3b8", 1.00, 0.50, 0.25, [0.0, -36.0, -34.0]),
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

    print(f"[CAD] Compiling {len(tasks)} OpenSCAD STL targets in parallel...")
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as ex:
        for pid, stl_path in ex.map(compile_one, tasks):
            sz_kb = os.path.getsize(stl_path) / 1024.0
            print(f"  [OK] {pid:30s} -> {sz_kb:7.1f} KB")

    # Verify meshes, compute real mass-weighted 3D CoG, and pack Float32 vertices+normals for WebGL
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
            "step": idx + 1,
            "id": pid,
            "mass_g": mass_g,
            "triangles": int(len(mesh.faces)),
            "watertight": bool(mesh.is_watertight),
            "volume_cm3": round(vol_cm3, 2),
            "bounds_mm": [[round(float(v), 2) for v in bounds[0]], [round(float(v), 2) for v in bounds[1]]],
            "dims_mm": [round(float(v), 2) for v in dims],
            "centroid_mm": [round(float(v), 2) for v in centroid],
        })

        # Pack exact flat-shaded / smooth triangle soup (Float32 positions + Float32 normals)
        tris = mesh.triangles.astype(np.float32)  # (N, 3, 3)
        fnorms = np.repeat(mesh.face_normals.astype(np.float32)[:, None, :], 3, axis=1)  # (N, 3, 3)
        pos_b64 = base64.b64encode(tris.reshape(-1).tobytes()).decode("ascii")
        nrm_b64 = base64.b64encode(fnorms.reshape(-1).tobytes()).decode("ascii")

        bundle_parts.append({
            "step": idx + 1,
            "id": pid,
            "color": color_hex,
            "opacity": opacity,
            "metalness": metal,
            "roughness": rough,
            "explode": explode_v,
            "triCount": int(len(mesh.faces)),
            "posB64": pos_b64,
            "nrmB64": nrm_b64,
        })

    cog = weighted_cog / total_mass
    overall_dims = overall_max - overall_min

    # Static wheel-traction & stability triangle check
    # Drive wheels at (±86, 0), rear caster at (0, -82).
    # Moment balance about rear caster (y = -82): F_wheels / W = (y_cog - (-82)) / (0 - (-82))
    y_caster = -82.0
    y_wheels = 0.0
    wheel_load_frac = float(np.clip((cog[1] - y_caster) / (y_wheels - y_caster), 0.0, 1.0))
    inside_triangle = (abs(cog[0]) < 25.0) and (y_caster + 10.0 < cog[1] < 20.0)

    summary = {
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
        "all_watertight": all(p["watertight"] for p in part_reports),
        "total_triangles": sum(p["triangles"] for p in part_reports),
        "parts": part_reports,
    }

    verif_path = os.path.join(ROOT, "cad", "cad_verification.json")
    with open(verif_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    bundle_js = os.path.join(VIEWER_DIR, "stl_bundle.js")
    with open(bundle_js, "w", encoding="utf-8") as f:
        f.write("window.SUGA_CAD_BUNDLE = ")
        json.dump({"parts": bundle_parts, "meta": summary["envelope_mm"]}, f, separators=(",", ":"))
        f.write(";\n")

    print("\n=== CAD VERIFICATION SUMMARY ===")
    print(f"  Envelope (W x L x H) : {summary['envelope_mm']['width_X']} x {summary['envelope_mm']['length_Y']} x {summary['envelope_mm']['height_Z']} mm")
    print(f"  Total Mass           : {summary['total_mass_g']} g")
    print(f"  3D Center of Gravity : X={cog[0]:+.2f} mm, Y={cog[1]:+.2f} mm, Z={cog[2]:.2f} mm")
    print(f"  Wheel Traction Load  : {wheel_load_frac*100:.1f}% on drive wheels (inside triangle={inside_triangle})")
    print(f"  All 12 Watertight    : {summary['all_watertight']} ({summary['total_triangles']} total tris)")
    print(f"  WebGL Bundle Size    : {os.path.getsize(bundle_js)/1024:.1f} KB -> {bundle_js}")


if __name__ == "__main__":
    main()
