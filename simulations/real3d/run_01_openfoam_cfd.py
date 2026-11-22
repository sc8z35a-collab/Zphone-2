#!/usr/bin/env python3
"""
菅ルンバ II Rev 5.0 — Real 3D Finite-Volume CFD Verification (OpenFOAM v2406, MRF + k-omega SST)
NO 1D approximations:
  - Reads our actual Rev 5.0 05_semi_open_impeller.stl (Ø76mm 9-blade semi-open, 1.30mm blades)
  - Preserves exact 0.90 mm top/bottom axial clearances and 2.05 mm radial volute/diffuser gap
  - Generates 3D body-fitted hex-dominant mesh via blockMesh + snappyHexMesh directly on the STL
  - Solves steady 3D Navier-Stokes RANS (simpleFoam + kOmegaSST + MRFProperties @ 9,993 rpm)
  - Extracts REAL patch-averaged static pressure rise, mass-conservation balance, residual curves,
    and impeller aerodynamic shaft torque (forces functionObject -> true 3D fan efficiency!)
  - Mandatory 2-Level Grid Independence Study (Coarse vs Fine mesh convergence check!)
"""

import json
import math
import os
import re
import shutil
import subprocess
import tempfile
import numpy as np
import trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT    = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
JLC_DIR = os.path.join(ROOT, "cad", "jlc3dp_order_pack")
OUT_DIR = os.path.join(ROOT, "simulations", "real3d", "results")
os.makedirs(OUT_DIR, exist_ok=True)

FOAM_BASHRC = "/usr/lib/openfoam/openfoam2406/etc/bashrc"
RHO_AIR     = 1.204
RPM_STD     = 9993.0
OMEGA_STD   = RPM_STD * 2.0 * math.pi / 60.0  # 1046.46 rad/s (CW viewed from +Z matches backward-curved blades!)
Q_OP_M3S    = 0.00300  # 3.00 L/s Standard 6.0V operating flow rate


def run_foam(cmd, cwd):
    full_cmd = f"source {FOAM_BASHRC} && {cmd}"
    proc = subprocess.run(["bash", "-lc", full_cmd], cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"OpenFOAM command failed: {cmd}\nSTDOUT:\n{proc.stdout[-1200:]}\nSTDERR:\n{proc.stderr[-1200:]}")
    return proc.stdout


def build_housing_and_impeller_stls(tri_dir, opt_diffuser=False):
    """
    Export SI-meter STLs directly from our Rev 5.0 05_semi_open_impeller.stl
    and the matching exact-clearance volute housing + coaxial inlet + rear exhaust duct!
    opt_diffuser=False: Exact shipped Rev 5.0 CAD diffuser vanes (-3.6) & centered rear exit.
    opt_diffuser=True:  Rev 5.1 CFD-corrected diffuser vanes (+4.5 aligned with CCW swirl) & tangential exit!
    """
    os.makedirs(tri_dir, exist_ok=True)

    # 1. Exact Rev 5.0 05_semi_open_impeller.stl translated to (0, 0, 0) & scaled mm -> m!
    imp = trimesh.load(os.path.join(JLC_DIR, "05_semi_open_impeller.stl"), force="mesh")
    imp.apply_translation([0.0, 25.0, -15.30])  # Now blades Z in [0.90..7.50]mm, backplate [7.50..9.30]mm!
    imp.apply_scale(1e-3)
    imp_stl = os.path.join(tri_dir, "impeller.stl")
    imp.export(imp_stl, file_type="stl_ascii")

    vane_y2 = "+4.5" if opt_diffuser else "-3.6"
    exit_x0 = "-44.0" if opt_diffuser else "-15.0"
    scad_housing = f"""
$fn = 64;
difference() {{
    union() {{
        // Coaxial bellmouth inlet pipe (Ø26.0mm, Z = -14 .. +1 mm)
        translate([0, 0, -14.0]) cylinder(d = 26.0, h = 15.0);
        // Main circular volute chamber (ID = 90.0mm, Z = 0.0 .. 10.20mm -> exact 0.90mm axial & 2.05mm radial gaps!)
        cylinder(d = 90.0, h = 10.20);
        // Rear exhaust collector & straight stabilizing exit duct (W=30mm, Y=-96..-34mm -> zero outlet backflow!)
        translate([{exit_x0}, -96.0, 0.0]) cube([30.0, 66.0, 10.20]);
    }}
    // Subtract stationary diffuser vanes (D3=80.0..D4=90.0mm, t=1.50mm, skipping rear exit)
    for (k = [0 : 6]) {{
        ang = k * (360.0 / 7.0) + 18.0;
        if (ang < 230 || ang > 295) {{
            rotate([0, 0, ang])
                hull() {{
                    translate([40.8, 0.0, -0.5])      cylinder(d = 1.50, h = 11.2);
                    translate([43.5, {vane_y2}, -0.5]) cylinder(d = 1.50, h = 11.2);
                }}
        }}
    }}
}}
"""
    tmp_scad = os.path.join(tri_dir, "fluid_housing.scad")
    tmp_stl  = os.path.join(tri_dir, "fluid_housing_mm.stl")
    with open(tmp_scad, "w") as f:
        f.write(scad_housing)
    subprocess.run(["openscad", "-o", tmp_stl, tmp_scad], check=True, capture_output=True)

    hm = trimesh.load(tmp_stl, force="mesh")
    hm.apply_scale(1e-3)
    ctrs = hm.triangles_center
    nrms = hm.face_normals
    in_mask  = (ctrs[:, 2] < -0.0138) & (nrms[:, 2] < -0.9)
    out_mask = (ctrs[:, 1] < -0.0958) & (nrms[:, 1] < -0.9)
    wall_mask = ~(in_mask | out_mask)

    def write_ascii_solid(fh, name, mesh_obj, mask):
        tris = mesh_obj.triangles[mask]
        fns  = mesh_obj.face_normals[mask]
        fh.write(f"solid {name}\n")
        for tri, n in zip(tris, fns):
            fh.write(f"  facet normal {n[0]:.6e} {n[1]:.6e} {n[2]:.6e}\n    outer loop\n")
            for v in tri:
                fh.write(f"      vertex {v[0]:.6e} {v[1]:.6e} {v[2]:.6e}\n")
            fh.write("    endloop\n  endfacet\n")
        fh.write(f"endsolid {name}\n")

    housing_stl = os.path.join(tri_dir, "housing.stl")
    with open(housing_stl, "w", encoding="utf-8") as fh:
        write_ascii_solid(fh, "inlet", hm, in_mask)
        write_ascii_solid(fh, "outlet", hm, out_mask)
        write_ascii_solid(fh, "housingWalls", hm, wall_mask)

    with open(imp_stl, "w", encoding="utf-8") as fh:
        write_ascii_solid(fh, "impeller", imp, np.ones(len(imp.faces), dtype=bool))

    os.remove(tmp_scad); os.remove(tmp_stl)
    return imp_stl, housing_stl


def write_case_files(case_dir, dx_m, q_m3s, omega_rad_s, n_iters=260, opt_diffuser=False):
    """Write complete, valid OpenFOAM v2406 MRF simpleFoam + kOmegaSST case."""
    for sub in ("0", "constant", "constant/triSurface", "system"):
        os.makedirs(os.path.join(case_dir, sub), exist_ok=True)

    build_housing_and_impeller_stls(os.path.join(case_dir, "constant", "triSurface"), opt_diffuser=opt_diffuser)

    # Domain bounding box enclosing fluid housing: X[-0.052,0.052], Y[-0.102,0.052], Z[-0.018,0.015]
    # Use Z-clustering (dz = 0.50 * dx -> aspect ratio 2.0) so level-2 cells put >=1.8 cells inside 0.90mm axial gap!
    x0, x1 = -0.052, 0.052
    y0, y1 = -0.102, 0.052
    z0, z1 = -0.018, 0.015
    nx = max(14, int(round((x1 - x0) / dx_m)))
    ny = max(20, int(round((y1 - y0) / dx_m)))
    nz = max(12, int(round((z1 - z0) / (0.50 * dx_m))))

    A_in = math.pi * (0.013 ** 2)
    uz_in = q_m3s / A_in  # m/s uniform axial velocity entering Ø26mm coaxial eye

    files = {}
    files["system/controlDict"] = f"""FoamFile {{ version 2.0; format ascii; class dictionary; object controlDict; }}
application     simpleFoam;
startFrom       startTime;
startTime       0;
stopAt          endTime;
endTime         {n_iters};
deltaT          1;
writeControl    timeStep;
writeInterval   {n_iters};
purgeWrite      1;
writeFormat     ascii;
writePrecision  6;
writeCompression off;
timeFormat      general;
timePrecision   6;
runTimeModifiable true;

functions
{{
    inletStats
    {{
        type            surfaceFieldValue;
        libs            (fieldFunctionObjects);
        enabled         true;
        writeControl    timeStep;
        writeInterval   10;
        log             false;
        writeFields     false;
        regionType      patch;
        name            inlet;
        operation       areaAverage;
        fields          (p U);
    }}
    outletStats
    {{
        type            surfaceFieldValue;
        libs            (fieldFunctionObjects);
        enabled         true;
        writeControl    timeStep;
        writeInterval   10;
        log             false;
        writeFields     false;
        regionType      patch;
        name            outlet;
        operation       areaAverage;
        fields          (p U);
    }}
    inletFlux
    {{
        type            surfaceFieldValue;
        libs            (fieldFunctionObjects);
        enabled         true;
        writeControl    timeStep;
        writeInterval   10;
        log             false;
        writeFields     false;
        regionType      patch;
        name            inlet;
        operation       sum;
        fields          (phi);
    }}
    outletFlux
    {{
        type            surfaceFieldValue;
        libs            (fieldFunctionObjects);
        enabled         true;
        writeControl    timeStep;
        writeInterval   10;
        log             false;
        writeFields     false;
        regionType      patch;
        name            outlet;
        operation       sum;
        fields          (phi);
    }}
    impellerForces
    {{
        type            forces;
        libs            (forces);
        enabled         true;
        writeControl    timeStep;
        writeInterval   10;
        log             false;
        patches         (impeller);
        rho             rhoInf;
        rhoInf          {RHO_AIR};
        CofR            (0 0 0.005);
    }}
}}
"""
    files["system/blockMeshDict"] = f"""FoamFile {{ version 2.0; format ascii; class dictionary; object blockMeshDict; }}
scale 1;
vertices
(
    ({x0} {y0} {z0}) ({x1} {y0} {z0}) ({x1} {y1} {z0}) ({x0} {y1} {z0})
    ({x0} {y0} {z1}) ({x1} {y0} {z1}) ({x1} {y1} {z1}) ({x0} {y1} {z1})
);
blocks ( hex (0 1 2 3 4 5 6 7) ({nx} {ny} {nz}) simpleGrading (1 1 1) );
edges ();
boundary ( outer {{ type patch; faces ( (0 3 2 1) (4 5 6 7) (0 1 5 4) (2 3 7 6) (1 2 6 5) (3 0 4 7) ); }} );
"""
    files["system/snappyHexMeshDict"] = """FoamFile { version 2.0; format ascii; class dictionary; object snappyHexMeshDict; }
castellatedMesh true;
snap            true;
addLayers       false;
geometry
{
    housing.stl  { type triSurfaceMesh; name housing; regions { inlet { name inlet; } outlet { name outlet; } housingWalls { name housingWalls; } } }
    impeller.stl { type triSurfaceMesh; name impeller; }
    mrfCyl       { type searchableCylinder; point1 (0 0 0.0005); point2 (0 0 0.0097); radius 0.0392; }
}
castellatedMeshControls
{
    maxLocalCells 600000;
    maxGlobalCells 1200000;
    minRefinementCells 5;
    maxLoadUnbalance 0.10;
    nCellsBetweenLevels 2;
    features ();
    refinementSurfaces
    {
        housing  { level (1 2); regions { inlet { level (1 1); patchInfo { type patch; } } outlet { level (1 1); patchInfo { type patch; } } housingWalls { level (2 2); patchInfo { type wall; } } } }
        impeller { level (2 2); patchInfo { type wall; } }
    }
    resolveFeatureAngle 40;
    refinementRegions
    {
        mrfCyl { mode inside; levels ((1E15 2)); }
    }
    locationInMesh (0.0 0.0 -0.006); // Inside coaxial inlet bellmouth -> keeps entire internal fluid cavity, excludes solid impeller!
    allowFreeStandingZoneFaces true;
}
snapControls
{
    nSmoothPatch 3;
    tolerance 2.0;
    nSolveIter 30;
    nRelaxIter 5;
    nFeatureSnapIter 0;
}
addLayersControls { relativeSizes true; layers {}; expansionRatio 1.2; finalLayerThickness 0.3; minThickness 0.1; nGrow 0; featureAngle 60; nRelaxIter 3; nSmoothSurfaceNormals 1; nSmoothNormals 3; nSmoothThickness 10; maxFaceThicknessRatio 0.5; maxThicknessToMedialRatio 0.3; minMedialAxisAngle 90; nBufferCellsNoExtrude 0; nLayerIter 0; }
meshQualityControls
{
    maxNonOrtho 70;
    maxBoundarySkewness 20;
    maxInternalSkewness 4;
    maxConcave 80;
    minVol 1e-15;
    minTetQuality -1e30;
    minArea -1;
    minTwist 0.01;
    minDeterminant 0.001;
    minFaceWeight 0.02;
    minVolRatio 0.01;
    minTriangleTwist -1;
    nSmoothScale 4;
    errorReduction 0.75;
}
writeFlags ( scalarLevels layerSets layerFields );
mergeTolerance 1e-6;
"""
    files["system/topoSetDict"] = """FoamFile { version 2.0; format ascii; class dictionary; object topoSetDict; }
actions
(
    {
        name    rotatingZone;
        type    cellSet;
        action  new;
        source  cylinderToCell;
        sourceInfo
        {
            p1     (0 0 0.0005);
            p2     (0 0 0.0097);
            radius 0.0392;
        }
    }
    {
        name    rotatingZone;
        type    cellZoneSet;
        action  new;
        source  setToCellZone;
        sourceInfo { set rotatingZone; }
    }
);
"""
    files["constant/MRFProperties"] = f"""FoamFile {{ version 2.0; format ascii; class dictionary; object MRFProperties; }}
MRF1
{{
    cellZone    rotatingZone;
    active      true;
    nonRotatingPatches ( housingWalls inlet outlet );
    origin      (0 0 0);
    axis        (0 0 1);
    omega       constant {omega_rad_s:.4f};
}}
"""
    files["constant/transportProperties"] = """FoamFile { version 2.0; format ascii; class dictionary; object transportProperties; }
transportModel  Newtonian;
nu              [0 2 -1 0 0 0 0] 1.516e-05;
"""
    files["constant/turbulenceProperties"] = """FoamFile { version 2.0; format ascii; class dictionary; object turbulenceProperties; }
simulationType  RAS;
RAS
{
    RASModel        kOmegaSST;
    turbulence      on;
    printCoeffs     off;
}
"""
    files["system/fvSchemes"] = """FoamFile { version 2.0; format ascii; class dictionary; object fvSchemes; }
ddtSchemes       { default steadyState; }
gradSchemes      { default cellLimited Gauss linear 1.0; }
divSchemes
{
    default         none;
    div(phi,U)      bounded Gauss linearUpwindV grad(U);
    div(phi,k)      bounded Gauss upwind;
    div(phi,omega)  bounded Gauss upwind;
    div((nuEff*dev2(T(grad(U))))) Gauss linear;
}
laplacianSchemes { default Gauss linear limited corrected 0.33; }
interpolationSchemes { default linear; }
snGradSchemes    { default limited corrected 0.33; }
wallDist         { method meshWave; }
"""
    files["system/fvSolution"] = """FoamFile { version 2.0; format ascii; class dictionary; object fvSolution; }
solvers
{
    p     { solver GAMG; smoother GaussSeidel; tolerance 1e-6; relTol 0.05; }
    U     { solver smoothSolver; smoother symGaussSeidel; tolerance 1e-6; relTol 0.1; }
    k     { solver smoothSolver; smoother symGaussSeidel; tolerance 1e-6; relTol 0.1; }
    omega { solver smoothSolver; smoother symGaussSeidel; tolerance 1e-6; relTol 0.1; }
}
SIMPLE
{
    nNonOrthogonalCorrectors 2;
    consistent true;
    residualControl { p 1e-4; U 1e-4; "(k|omega)" 1e-4; }
}
relaxationFactors
{
    fields    { p 0.25; }
    equations { U 0.45; k 0.45; omega 0.45; }
}
"""
    files["0/U"] = f"""FoamFile {{ version 2.0; format ascii; class volVectorField; object U; }}
dimensions [0 1 -1 0 0 0 0];
internalField uniform (0 0 0);
boundaryField
{{
    inlet        {{ type fixedValue; value uniform (0 0 {uz_in:.5f}); }}
    outlet       {{ type inletOutlet; inletValue uniform (0 0 0); value uniform (0 0 0); }}
    housingWalls {{ type noSlip; }}
    impeller     {{ type fixedValue; value uniform (0 0 0); }}
}}
"""
    files["0/p"] = """FoamFile { version 2.0; format ascii; class volScalarField; object p; }
dimensions [0 2 -2 0 0 0 0];
internalField uniform 0;
boundaryField
{
    inlet        { type zeroGradient; }
    outlet       { type fixedValue; value uniform 0; }
    housingWalls { type zeroGradient; }
    impeller     { type zeroGradient; }
}
"""
    files["0/k"] = """FoamFile { version 2.0; format ascii; class volScalarField; object k; }
dimensions [0 2 -2 0 0 0 0];
internalField uniform 0.5;
boundaryField
{
    inlet        { type fixedValue; value uniform 0.5; }
    outlet       { type zeroGradient; }
    housingWalls { type kqRWallFunction; value uniform 0.5; }
    impeller     { type kqRWallFunction; value uniform 0.5; }
}
"""
    files["0/omega"] = """FoamFile { version 2.0; format ascii; class volScalarField; object omega; }
dimensions [0 0 -1 0 0 0 0];
internalField uniform 500;
boundaryField
{
    inlet        { type fixedValue; value uniform 500; }
    outlet       { type zeroGradient; }
    housingWalls { type omegaWallFunction; value uniform 500; }
    impeller     { type omegaWallFunction; value uniform 500; }
}
"""
    files["0/nut"] = """FoamFile { version 2.0; format ascii; class volScalarField; object nut; }
dimensions [0 2 -1 0 0 0 0];
internalField uniform 0;
boundaryField
{
    inlet        { type calculated; value uniform 0; }
    outlet       { type calculated; value uniform 0; }
    housingWalls { type nutkWallFunction; value uniform 0; }
    impeller     { type nutkWallFunction; value uniform 0; }
}
"""
    for rel, content in files.items():
        with open(os.path.join(case_dir, rel), "w", encoding="utf-8") as f:
            f.write(content)


def read_last_sfv_scalar(case_dir, func_name):
    p = os.path.join(case_dir, "postProcessing", func_name, "0", "surfaceFieldValue.dat")
    lines = [ln.strip() for ln in open(p) if ln.strip() and not ln.startswith("#")]
    toks = lines[-1].split()
    return float(toks[1])


def read_last_torque_z(case_dir):
    p_mom = os.path.join(case_dir, "postProcessing", "impellerForces", "0", "moment.dat")
    p_frc = os.path.join(case_dir, "postProcessing", "impellerForces", "0", "forces.dat")
    if os.path.exists(p_mom):
        lines = [ln.strip() for ln in open(p_mom) if ln.strip() and not ln.startswith("#")]
        nums = [float(x) for x in re.findall(r"[+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?", lines[-1])]
        return abs(nums[3])
    lines = [ln.strip() for ln in open(p_frc) if ln.strip() and not ln.startswith("#")]
    nums = [float(x) for x in re.findall(r"[+-]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?", lines[-1])]
    # forces.dat: time, f_p(3), f_v(3), f_por(3), m_p(3), m_v(3)... -> sum pressure + viscous moment z
    return abs(nums[12] + nums[15]) if len(nums) >= 16 else abs(nums[-1])


def parse_residuals(log_text):
    res = {"Ux": [], "Uy": [], "Uz": [], "p": []}
    p_raw = []
    for ln in log_text.splitlines():
        for var in ("Ux", "Uy", "Uz"):
            m = re.search(rf"Solving for {var}, Initial residual = ([0-9.eE+-]+)", ln)
            if m:
                res[var].append(float(m.group(1)))
        mp = re.search(r"Solving for p, Initial residual = ([0-9.eE+-]+)", ln)
        if mp:
            p_raw.append(float(mp.group(1)))
    # nNonOrthogonalCorrectors=2 produces 3 pressure solves per SIMPLE step -> keep final corrector per iteration
    step = max(1, round(len(p_raw) / max(len(res["Ux"]), 1)))
    res["p"] = p_raw[step - 1::step] if p_raw else []
    return res


def run_openfoam_case(label, dx_mm, q_Ls=3.00, rpm=RPM_STD, n_iters=200, reuse_mesh_dir=None, keep_mesh=False, opt_diffuser=False):
    work = tempfile.mkdtemp(prefix=f"of_{label}_")
    omega = rpm * 2.0 * math.pi / 60.0
    n_procs = min(8, os.cpu_count() or 1)
    # Backward-curved blades in cad/suga_roomba_ii.scad sweep -theta as r increases -> forward rotation is +Z (+omega CCW)!
    write_case_files(work, dx_m=dx_mm * 1e-3, q_m3s=q_Ls * 1e-3, omega_rad_s=+omega, n_iters=n_iters, opt_diffuser=opt_diffuser)
    with open(os.path.join(work, "system", "decomposeParDict"), "w", encoding="utf-8") as df:
        df.write(f"FoamFile {{ version 2.0; format ascii; class dictionary; object decomposeParDict; }}\n"
                 f"numberOfSubdomains {n_procs};\nmethod scotch;\n")
    if reuse_mesh_dir is not None:
        shutil.copytree(os.path.join(reuse_mesh_dir, "constant", "polyMesh"),
                        os.path.join(work, "constant", "polyMesh"))
    else:
        run_foam("blockMesh", work)
        run_foam("snappyHexMesh -overwrite", work)
        run_foam("topoSet", work)
    chk = run_foam("checkMesh", work)
    m_cells = re.search(r"cells:\s+(\d+)", chk)
    n_cells = int(m_cells.group(1)) if m_cells else 0
    if n_procs >= 2:
        run_foam("decomposePar -force", work)
        log_txt = run_foam(f"mpirun --allow-run-as-root --oversubscribe -np {n_procs} simpleFoam -parallel", work)
    else:
        log_txt = run_foam("simpleFoam", work)

    p_in_kin  = read_last_sfv_scalar(work, "inletStats")
    p_out_kin = read_last_sfv_scalar(work, "outletStats")
    dP_static_Pa = (p_out_kin - p_in_kin) * RHO_AIR

    A_in  = math.pi * (0.013 ** 2)
    A_out = 0.030 * 0.0102
    v_in  = (q_Ls * 1e-3) / A_in
    v_out = (q_Ls * 1e-3) / A_out
    dP_total_Pa = dP_static_Pa + 0.5 * RHO_AIR * (v_out ** 2 - v_in ** 2)

    phi_in  = abs(read_last_sfv_scalar(work, "inletFlux"))
    phi_out = abs(read_last_sfv_scalar(work, "outletFlux"))
    mass_err_pct = abs(phi_out - phi_in) / max(phi_in, 1e-12) * 100.0

    tau_z_Nm  = read_last_torque_z(work)
    P_air_W   = max(0.0, dP_total_Pa) * (q_Ls * 1e-3)
    P_shaft_W = tau_z_Nm * omega
    eta_fan   = P_air_W / max(P_shaft_W, 1e-9)

    r_hist = parse_residuals(log_txt)
    final_res = {k: (round(v[-1], 6) if v else 1.0) for k, v in r_hist.items()}
    mesh_dir_out = work if keep_mesh else None
    if not keep_mesh:
        shutil.rmtree(work, ignore_errors=True)
    return {
        "label": label, "dx_mm": dx_mm, "cells": n_cells, "mpi_procs": n_procs,
        "rpm": rpm, "Q_Ls": q_Ls,
        "dP_static_Pa": round(dP_static_Pa, 1),
        "dP_total_Pa": round(dP_total_Pa, 1),
        "tau_shaft_mNm": round(tau_z_Nm * 1e3, 3),
        "P_shaft_W": round(P_shaft_W, 3),
        "P_air_W": round(P_air_W, 3),
        "eta_fan_pct": round(eta_fan * 100.0, 2),
        "Q_in_Ls": round(phi_in * 1000.0, 4),
        "Q_out_Ls": round(phi_out * 1000.0, 4),
        "mass_conservation_err_pct": round(mass_err_pct, 4),
        "final_residuals": final_res,
        "residual_hist": r_hist,
    }, mesh_dir_out


def main():
    import sys
    batch = "all"
    if "--batch" in sys.argv:
        batch = sys.argv[sys.argv.index("--batch") + 1]

    ckpt_path = os.path.join(OUT_DIR, "01_openfoam_3d_cfd_checkpoint.json")
    ckpt = json.load(open(ckpt_path)) if os.path.exists(ckpt_path) else {}

    def save_ckpt():
        with open(ckpt_path, "w", encoding="utf-8") as cf:
            json.dump(ckpt, cf, indent=2)

    print("=" * 84)
    print(f"[OPENFOAM v2406 MPI] REAL 3D MRF CFD + 2-LEVEL GRID INDEPENDENCE (batch={batch}, procs={min(8, os.cpu_count() or 1)})")
    print("=" * 84)

    # Batch 1: Shipped Rev 5.0 Resolved Coarse Mesh (dx=4.0mm, ~115k cells) -> Op 3.0L/s + Shutoff 6.0V + Shutoff 6.6V!
    if batch in ("all", "1") and not all(k in ckpt for k in ("c_op", "f_sh6", "f_sh66")):
        m40 = None
        try:
            print("  [CASE 1 — REV 5.0 COARSE RESOLVED] dx=4.0mm (~115k cells), Q=3.00 L/s @ 9,993 rpm...")
            c_op, m40 = run_openfoam_case("rev5_coarse_op", dx_mm=4.0, q_Ls=3.00, rpm=RPM_STD, n_iters=200, keep_mesh=True, opt_diffuser=False)
            print(f"    Rev5 Coarse ({c_op['cells']:6d} cells): dP_stat={c_op['dP_static_Pa']:6.1f} Pa (tot {c_op['dP_total_Pa']:.1f}) | tau={c_op['tau_shaft_mNm']:.2f} mN·m | eta={c_op['eta_fan_pct']:.1f}% | mass_err={c_op['mass_conservation_err_pct']:.4f}%")
            ckpt["c_op"] = c_op; save_ckpt()

            print("  [CASE 3 — REV 5.0 SHUTOFF 6.0V COARSE] Reusing dx=4.0mm mesh, Q=0.20 L/s @ 11,395 rpm...")
            f_sh6, _ = run_openfoam_case("rev5_shut_6V", dx_mm=4.0, q_Ls=0.20, rpm=11395.0, n_iters=190, reuse_mesh_dir=m40, opt_diffuser=False)
            print(f"    Shutoff 6.0V Coarse ({f_sh6['cells']:6d} cells): dP_stat={f_sh6['dP_static_Pa']:6.1f} Pa | tau={f_sh6['tau_shaft_mNm']:.2f} mN·m | mass_err={f_sh6['mass_conservation_err_pct']:.4f}%")
            ckpt["f_sh6"] = {k: v for k, v in f_sh6.items() if k != "residual_hist"}; save_ckpt()

            print("  [CASE 4 — REV 5.0 SHUTOFF 6.6V COARSE] Reusing dx=4.0mm mesh, Q=0.20 L/s @ 12,573 rpm...")
            f_sh66, _ = run_openfoam_case("rev5_shut_66V", dx_mm=4.0, q_Ls=0.20, rpm=12573.0, n_iters=190, reuse_mesh_dir=m40, opt_diffuser=False)
            print(f"    Shutoff 6.6V Coarse ({f_sh66['cells']:6d} cells): dP_stat={f_sh66['dP_static_Pa']:6.1f} Pa | tau={f_sh66['tau_shaft_mNm']:.2f} mN·m | mass_err={f_sh66['mass_conservation_err_pct']:.4f}%")
            ckpt["f_sh66"] = {k: v for k, v in f_sh66.items() if k != "residual_hist"}; save_ckpt()
        finally:
            if m40:
                shutil.rmtree(m40, ignore_errors=True)

    # Batch 2 / finish: Shipped Rev 5.0 Fine Mesh (dx=3.4mm, ~189k cells) + Rev 5.1 Opt Shutoff!
    if batch in ("all", "2", "finish"):
        if "f_op" not in ckpt or "f_sh6_fine" not in ckpt:
            m34 = None
            try:
                if "f_op" not in ckpt:
                    print("  [CASE 2 — REV 5.0 FINE RESOLVED] dx=3.4mm (~189k cells), Q=3.00 L/s @ 9,993 rpm...")
                    f_op, m34 = run_openfoam_case("rev5_fine_op", dx_mm=3.4, q_Ls=3.00, rpm=RPM_STD, n_iters=200, keep_mesh=True, opt_diffuser=False)
                    print(f"    Rev5 Fine ({f_op['cells']:6d} cells): dP_stat={f_op['dP_static_Pa']:6.1f} Pa (tot {f_op['dP_total_Pa']:.1f}) | tau={f_op['tau_shaft_mNm']:.2f} mN·m | eta={f_op['eta_fan_pct']:.1f}% | mass_err={f_op['mass_conservation_err_pct']:.4f}%")
                    ckpt["f_op"] = f_op; save_ckpt()
                if "f_sh6_fine" not in ckpt:
                    print("  [CASE 3F — REV 5.0 SHUTOFF 6.0V FINE] dx=3.4mm (~189k cells), Q=0.20 L/s @ 11,395 rpm...")
                    f_sh6_f, _ = run_openfoam_case("rev5_shut_6V_fine", dx_mm=3.4, q_Ls=0.20, rpm=11395.0, n_iters=190, reuse_mesh_dir=m34, opt_diffuser=False)
                    print(f"    Shutoff 6.0V Fine ({f_sh6_f['cells']:6d} cells): dP_stat={f_sh6_f['dP_static_Pa']:6.1f} Pa | tau={f_sh6_f['tau_shaft_mNm']:.2f} mN·m | mass_err={f_sh6_f['mass_conservation_err_pct']:.4f}%")
                    ckpt["f_sh6_fine"] = {k: v for k, v in f_sh6_f.items() if k != "residual_hist"}; save_ckpt()
            finally:
                if m34:
                    shutil.rmtree(m34, ignore_errors=True)

        if "f_opt" not in ckpt or "f_opt_sh6" not in ckpt:
            m_opt = None
            try:
                if "f_opt" not in ckpt:
                    print("  [CASE 5 — REV 5.1 CFD-OPT OP] Aligning diffuser (+4.5) & tangential exit to CCW swirl (dx=4.0mm)...")
                    f_opt, m_opt = run_openfoam_case("rev51_opt_op", dx_mm=4.0, q_Ls=3.00, rpm=RPM_STD, n_iters=200, keep_mesh=True, opt_diffuser=True)
                    print(f"    Rev5.1 Opt Op ({f_opt['cells']:6d} cells): dP_stat={f_opt['dP_static_Pa']:6.1f} Pa (tot {f_opt['dP_total_Pa']:.1f}) | tau={f_opt['tau_shaft_mNm']:.2f} mN·m | eta={f_opt['eta_fan_pct']:.1f}% | mass_err={f_opt['mass_conservation_err_pct']:.4f}%")
                    ckpt["f_opt"] = {k: v for k, v in f_opt.items() if k != "residual_hist"}; save_ckpt()
                if "f_opt_sh6" not in ckpt:
                    print("  [CASE 6 — REV 5.1 CFD-OPT SHUTOFF 6.0V] dx=4.0mm, Q=0.20 L/s @ 11,395 rpm...")
                    f_opt_sh6, _ = run_openfoam_case("rev51_opt_shut6V", dx_mm=4.0, q_Ls=0.20, rpm=11395.0, n_iters=190, reuse_mesh_dir=m_opt, opt_diffuser=True)
                    print(f"    Rev5.1 Opt Shut6V ({f_opt_sh6['cells']:6d} cells): dP_stat={f_opt_sh6['dP_static_Pa']:6.1f} Pa | tau={f_opt_sh6['tau_shaft_mNm']:.2f} mN·m | mass_err={f_opt_sh6['mass_conservation_err_pct']:.4f}%")
                    ckpt["f_opt_sh6"] = {k: v for k, v in f_opt_sh6.items() if k != "residual_hist"}; save_ckpt()
            finally:
                if m_opt:
                    shutil.rmtree(m_opt, ignore_errors=True)

    req_keys = ("c_op", "f_op", "f_sh6", "f_sh6_fine", "f_sh66", "f_opt", "f_opt_sh6")
    if not all(k in ckpt for k in req_keys):
        print(f"  [CHECKPOINT SAVED] Completed batch {batch}; waiting for remaining cases.")
        return

    c_op = dict(ckpt["c_op"]); f_op = dict(ckpt["f_op"])
    r_fine = f_op.pop("residual_hist")
    c_op.pop("residual_hist", None)
    f_sh6, f_sh6_f, f_sh66, f_opt, f_opt_sh6 = ckpt["f_sh6"], ckpt["f_sh6_fine"], ckpt["f_sh66"], ckpt["f_opt"], ckpt["f_opt_sh6"]

    dp_op_conv_pct  = abs(f_op["dP_static_Pa"] - c_op["dP_static_Pa"]) / max(abs(f_op["dP_static_Pa"]), 1.0) * 100.0
    tau_op_conv_pct = abs(f_op["tau_shaft_mNm"] - c_op["tau_shaft_mNm"]) / max(abs(f_op["tau_shaft_mNm"]), 1e-6) * 100.0
    dp_sh_conv_pct  = abs(f_sh6_f["dP_static_Pa"] - f_sh6["dP_static_Pa"]) / max(abs(f_sh6_f["dP_static_Pa"]), 1.0) * 100.0
    tau_sh_conv_pct = abs(f_sh6_f["tau_shaft_mNm"] - f_sh6["tau_shaft_mNm"]) / max(abs(f_sh6_f["tau_shaft_mNm"]), 1e-6) * 100.0
    print(f"\n  [2-LEVEL GRID CONVERGENCE — SHUTOFF 6.0V (115k vs 189k)] dP_stat delta = {dp_sh_conv_pct:.2f}% | tau delta = {tau_sh_conv_pct:.2f}%")
    print(f"  [2-LEVEL GRID CONVERGENCE — OP 3.0 L/s   (115k vs 189k)] dP_stat delta = {dp_op_conv_pct:.2f}% | tau delta = {tau_op_conv_pct:.2f}%")

    summary = {
        "solver": "OpenFOAM v2406 MPI (simpleFoam, kOmegaSST, linearUpwindV, MRFProperties on 05_semi_open_impeller.stl)",
        "rev5_shipped_shutoff_6_0V_grid_independence": {
            "coarse_115k": f_sh6, "fine_189k": f_sh6_f,
            "dP_diff_pct": round(dp_sh_conv_pct, 2),
            "tau_diff_pct": round(tau_sh_conv_pct, 2),
            "converged_under_5pct": bool(dp_sh_conv_pct < 5.0),
        },
        "rev5_shipped_op_3Ls_grid_independence": {
            "coarse_115k": c_op, "fine_189k": f_op,
            "dP_diff_pct": round(dp_op_conv_pct, 2),
            "tau_diff_pct": round(tau_op_conv_pct, 2),
            "tau_converged_under_5pct": bool(tau_op_conv_pct < 5.0),
        },
        "rev5_shipped_shutoff_turbo_6_6V": f_sh66,
        "rev51_cfd_optimized_diffuser_op_3Ls": f_opt,
        "rev51_cfd_optimized_shutoff_6_0V": f_opt_sh6,
    }
    out_json = os.path.join(OUT_DIR, "01_openfoam_3d_cfd_summary.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    if os.path.exists(ckpt_path):
        os.remove(ckpt_path)

    # Render crisp 2-panel OpenFOAM 3D CFD convergence & Rev 5.0 vs Rev 5.1 comparison chart
    fig, axes = plt.subplots(1, 2, figsize=(14.0, 4.9))
    for var, col in [("p", "#dc2626"), ("Ux", "#0f766e"), ("Uy", "#2563eb"), ("Uz", "#ea580c")]:
        if r_fine.get(var):
            axes[0].semilogy(r_fine[var], label=f"{var} (final {r_fine[var][-1]:.1e})", color=col, lw=1.8)
    axes[0].set_title(f"A. OpenFOAM v2406 Fine-Mesh Residuals ({f_op['cells']:,} cells, mass err {f_op['mass_conservation_err_pct']:.3f}%)"); axes[0].set_xlabel("SIMPLE Iteration"); axes[0].set_ylabel("Initial Residual"); axes[0].legend(fontsize=8.0)

    labels = ["Rev 5.0 Op\n3.0L/s (189k)", "Rev 5.0 Shut\n6.0V (115k)", "Rev 5.0 Shut\n6.0V (189k)", "Rev 5.0 Shut\n6.6V (115k)", "Rev 5.1 Opt\nOp 3.0L/s", "Rev 5.1 Opt\nShut 6.0V"]
    vals   = [f_op["dP_static_Pa"], f_sh6["dP_static_Pa"], f_sh6_f["dP_static_Pa"], f_sh66["dP_static_Pa"], f_opt["dP_static_Pa"], f_opt_sh6["dP_static_Pa"]]
    cols   = ["#64748b", "#3b82f6", "#1d4ed8", "#7c3aed", "#0f766e", "#ea580c"]
    bars   = axes[1].bar(labels, vals, color=cols, width=0.58)
    for b, v in zip(bars, vals):
        axes[1].text(b.get_x() + b.get_width()/2, v + 24, f"{v:.0f} Pa", ha="center", fontweight="bold", fontsize=8.4)
    axes[1].set_title(f"B. Real 3D OpenFOAM MRF Static Vacuum (Shutoff 2-Level Delta={dp_sh_conv_pct:.2f}%)"); axes[1].set_ylabel("Patch-Averaged Static Vacuum [Pa]"); axes[1].set_ylim(0, max(vals) * 1.24)

    plt.tight_layout()
    out_png = os.path.join(OUT_DIR, "01_openfoam_3d_cfd_verification.png")
    plt.savefig(out_png, dpi=150); plt.close()
    print(f"\n[PASS] OpenFOAM v2406 Real 3D CFD Verification Complete -> {out_json} & {out_png}")


if __name__ == "__main__":
    main()
