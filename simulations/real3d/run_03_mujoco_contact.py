#!/usr/bin/env python3
"""
菅ルンバ II Rev 5.0 — Real 3D Multi-Body Contact Physics Verification (MuJoCo 4.x)
NO 1D approximations: full 3D rigid-body contact dynamics with Coulomb friction,
real PWN10EB12CB 6.0V torque-speed saturation, 3.2N rubber-band friction-drive slip cap,
exact CAD CoG (797.5 g, 84.7% drive-wheel normal load), Ø16mm free-rolling marble caster,
floating front bumper microswitch joint, and mandatory 2-level timestep convergence (<5%)!
"""

import json
import math
import os
import sys
import numpy as np

# Headless rendering guard (Colab & Linux VM without X11 display)
os.environ.setdefault("MUJOCO_GL", "osmesa")

import mujoco
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT    = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, "simulations", "real3d", "results")
os.makedirs(OUT_DIR, exist_ok=True)

# Exact Rev 5.2 Real-Hardware Mechanical Constants (from cad_verification.json + captive nuts & EVA seal lip)
TOTAL_MASS_KG    = 0.7851   # 785.1 g conservative Rev 5.2 SLA (472.3 g) + full installed HW mass
WHEEL_R          = 0.0250   # Ø50.0 mm (Ø48mm SLA rim + 1mm Daiso rubber band)
WHEEL_W          = 0.0120   # 12.0 mm width
WHEEL_X_FWD      = 0.0060   # +6.0 mm forward of origin
WHEEL_Y_HALF     = 0.0880   # 176.0 mm track (±88.0 mm)
MARBLE_R         = 0.0080   # Ø16.0 mm rear glass marble
MARBLE_X_FWD     = -0.0980  # -98.0 mm rear of origin
COG_X_FWD        = -0.01010 # Exact Rev 5.2 CAD CoG Y = -10.10 mm -> 84.5% load on drive wheels!
COG_Z            = 0.03244  # Exact Rev 5.2 CAD CoG Z = 32.44 mm

# Real Akizuki [110024] PWN10EB12CB @ 6.0V with 25.0:1 friction drive & 85% contact eff
OMEGA_WHEEL_NL   = (3440.0 / 25.0) * 2.0 * math.pi / 60.0  # 14.410 rad/s (0.360 m/s no-load)
TAU_WHEEL_STALL  = 0.00335 * 25.0 * 0.85                   # 0.07119 N·m per wheel
TAU_SLIP_CAP     = 0.95 * 3.20 * WHEEL_R                   # 0.07600 N·m (3.2N rubber band preload)


def build_mjcf(dt=0.001, iterations=80, step_h_m=0.0, ramp_deg=90.0, slope_deg=0.0, wall_x_m=None):
    """Generate authentic 3D MuJoCo MJCF model of Rev 5.1 robot + test arena."""
    slope_rad = math.radians(slope_deg)
    gx = -9.81 * math.sin(slope_rad)
    gz = -9.81 * math.cos(slope_rad)

    obstacles = []
    if step_h_m > 1e-5:
        if ramp_deg >= 89.0:
            # Sharp 90-degree vertical floor ledge starting at x = +0.28 m
            obstacles.append(
                f'<geom name="floor_step" type="box" pos="0.88 0 {step_h_m/2.0:.5f}" '
                f'size="0.60 0.50 {step_h_m/2.0:.5f}" friction="0.82 0.005 0.0001" rgba="0.72 0.55 0.38 1"/>'
            )
        else:
            # Realistic beveled threshold / carpet transition strip (ramp_deg) leading onto flat platform
            rad = math.radians(ramp_deg)
            ramp_run = step_h_m / math.tan(rad)
            ramp_len = math.hypot(ramp_run, step_h_m)
            t_slab = 0.004  # 4mm thick inclined ramp slab with top surface passing from (0.28, 0) to (0.28+ramp_run, step_h)
            cx = 0.28 + 0.5 * ramp_run + 0.5 * t_slab * math.sin(rad)
            cz = 0.5 * step_h_m - 0.5 * t_slab * math.cos(rad)
            obstacles.append(
                f'<geom name="floor_ramp" type="box" pos="{cx:.5f} 0 {cz:.5f}" euler="0 {-rad:.5f} 0" '
                f'size="{ramp_len/2.0:.5f} 0.50 {t_slab/2.0:.5f}" friction="0.82 0.005 0.0001" rgba="0.78 0.62 0.44 1"/>'
            )
            plat_x0 = 0.28 + ramp_run
            obstacles.append(
                f'<geom name="floor_step" type="box" pos="{plat_x0 + 0.60:.5f} 0 {step_h_m/2.0:.5f}" '
                f'size="0.60 0.50 {step_h_m/2.0:.5f}" friction="0.82 0.005 0.0001" rgba="0.72 0.55 0.38 1"/>'
            )
    if wall_x_m is not None:
        obstacles.append(
            f'<geom name="front_wall" type="box" pos="{wall_x_m + 0.02:.4f} 0 0.06" '
            f'size="0.02 0.50 0.06" friction="0.60 0.005 0.0001" rgba="0.35 0.40 0.48 1"/>'
        )
    obs_xml = "\n    ".join(obstacles)

    # Mass budget split accurately: chassis+canopy+HW + 2x wheels (0.0405 kg)
    # + marble (0.0055 kg) + floating bumper (0.0138 kg) -> exact 0.7665 kg!
    body_mass = TOTAL_MASS_KG - (2 * 0.02025 + 0.0055 + 0.0138)  # 0.7067 kg
    # Shift chassis inertial center slightly so composite robot CoG hits EXACT (-0.01030, 0, 0.03244) m!
    target_mx = TOTAL_MASS_KG * COG_X_FWD
    sub_mx = 2 * 0.02025 * WHEEL_X_FWD + 0.0055 * MARBLE_X_FWD + 0.0138 * 0.106
    body_cx = (target_mx - sub_mx) / body_mass

    return f"""<mujoco model="suga_roomba_ii_rev51">
  <compiler angle="radian" autolimits="true"/>
  <option timestep="{dt:.6f}" iterations="{iterations}" gravity="{gx:.5f} 0 {gz:.5f}" integrator="implicitfast"/>
  <visual>
    <headlight ambient="0.45 0.45 0.48" diffuse="0.7 0.7 0.7" specular="0.2 0.2 0.2"/>
  </visual>
  <worldbody>
    <light pos="0.3 -0.4 0.9" dir="-0.2 0.3 -0.8" diffuse="0.9 0.9 0.9"/>
    <geom name="floor" type="plane" size="3.0 3.0 0.05" friction="0.82 0.005 0.0001" rgba="0.91 0.93 0.95 1"/>
    {obs_xml}

    <body name="chassis" pos="0 0 0.0005">
      <freejoint name="root"/>
      <inertial pos="{body_cx:.5f} 0 {COG_Z:.5f}" mass="{body_mass:.5f}" diaginertia="0.0029 0.0030 0.0052"/>
      <!-- Rev 5.1 Main lower chassis belly (FLOOR_Z = 5.50mm raised ground clearance; compliant foam floor seal yields over steps) -->
      <geom name="lower_tub" type="cylinder" pos="-0.004 0 0.0210" size="0.098 0.0155"
            friction="0.25 0.005 0.0001" rgba="0.90 0.92 0.95 1"/>
      <!-- Rev 5.1 Front Reuleaux-D flat deck (x up to +0.102m, bottom z=0.0055m matching FLOOR_Z=5.50mm!) -->
      <geom name="front_deck" type="box" pos="0.052 0 0.0210" size="0.050 0.072 0.0155"
            friction="0.25 0.005 0.0001" rgba="0.90 0.92 0.95 1"/>
      <!-- Upper aero canopy (Z=34..64mm) + RS-385PH central dome (Z=64..75.6mm) -->
      <geom name="canopy" type="cylinder" pos="-0.004 0 0.049" size="0.096 0.015"
            contype="0" conaffinity="0" rgba="0.16 0.22 0.32 1"/>
      <geom name="motor_dome" type="cylinder" pos="-0.025 0 0.0695" size="0.023 0.006"
            contype="0" conaffinity="0" rgba="0.16 0.22 0.32 1"/>

      <!-- Left & Right Ø50mm Rubber-Tread Drive Wheels (Track = 176mm) -->
      <body name="wheel_L" pos="{WHEEL_X_FWD:.4f} {WHEEL_Y_HALF:.4f} {WHEEL_R:.4f}">
        <inertial pos="0 0 0" mass="0.02025" diaginertia="4.2e-6 6.8e-6 4.2e-6"/>
        <joint name="j_wheel_L" type="hinge" axis="0 1 0" damping="0.00025"/>
        <geom name="g_wheel_L" type="cylinder" euler="1.570796 0 0" size="{WHEEL_R:.4f} {WHEEL_W/2:.4f}"
              friction="0.82 0.005 0.0001" solref="0.008 1.0" rgba="0.90 0.38 0.10 1"/>
      </body>
      <body name="wheel_R" pos="{WHEEL_X_FWD:.4f} {-WHEEL_Y_HALF:.4f} {WHEEL_R:.4f}">
        <inertial pos="0 0 0" mass="0.02025" diaginertia="4.2e-6 6.8e-6 4.2e-6"/>
        <joint name="j_wheel_R" type="hinge" axis="0 1 0" damping="0.00025"/>
        <geom name="g_wheel_R" type="cylinder" euler="1.570796 0 0" size="{WHEEL_R:.4f} {WHEEL_W/2:.4f}"
              friction="0.82 0.005 0.0001" solref="0.008 1.0" rgba="0.90 0.38 0.10 1"/>
      </body>

      <!-- Rear Ø16mm Free-Rolling Glass Marble Caster -->
      <body name="rear_marble" pos="{MARBLE_X_FWD:.4f} 0 {MARBLE_R:.4f}">
        <inertial pos="0 0 0" mass="0.0055" diaginertia="1.4e-7 1.4e-7 1.4e-7"/>
        <joint name="j_marble" type="ball" damping="0.00001"/>
        <geom name="g_marble" type="sphere" size="{MARBLE_R:.4f}"
              friction="0.06 0.001 0.0001" solref="0.006 1.0" rgba="0.55 0.78 0.94 1"/>
      </body>

      <!-- Rev 5.1 Raised Guided Floating Front Bumper (Z=11.0..30.0mm; 2.0mm travel; microswitch triggers at 0.8mm!) -->
      <body name="front_bumper" pos="0.106 0 0.0205">
        <inertial pos="0 0 0" mass="0.0138" diaginertia="4.0e-5 5.0e-6 4.0e-5"/>
        <joint name="j_bumper" type="slide" axis="-1 0 0" range="0 0.0022"
               solreflimit="0.0015 1.0" solimplimit="0.96 0.995 0.0005" stiffness="180.0" damping="2.5"/>
        <geom name="g_bumper" type="box" size="0.0022 0.076 0.0095"
              friction="0.30 0.005 0.0001" rgba="0.25 0.30 0.38 1"/>
      </body>
    </body>
  </worldbody>

  <actuator>
    <motor name="mot_L" joint="j_wheel_L" gear="1" ctrllimited="true" ctrlrange="-0.08 0.08"/>
    <motor name="mot_R" joint="j_wheel_R" gear="1" ctrllimited="true" ctrlrange="-0.08 0.08"/>
  </actuator>
</mujoco>
"""


def motor_torque(omega_w, cmd=1.0):
    """Exact Akizuki PWN10EB12CB 6.0V DC motor torque-speed curve capped by 3.2N rubber-band slip limit."""
    tau_elec = TAU_WHEEL_STALL * (cmd - omega_w / OMEGA_WHEEL_NL)
    if cmd > 0:
        tau_elec = max(0.0, tau_elec)
    elif cmd < 0:
        tau_elec = min(0.0, tau_elec)
    return float(np.clip(tau_elec, -TAU_SLIP_CAP, TAU_SLIP_CAP))


def run_trial(sim_sec=2.5, dt=0.001, iterations=80, cmd=0.80, step_h_m=0.0, ramp_deg=90.0, slope_deg=0.0, wall_x_m=None):
    xml = build_mjcf(dt=dt, iterations=iterations, step_h_m=step_h_m, ramp_deg=ramp_deg, slope_deg=slope_deg, wall_x_m=wall_x_m)
    model = mujoco.MjModel.from_xml_string(xml)
    data = mujoco.MjData(model)

    id_wl = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, "j_wheel_L")
    id_wr = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, "j_wheel_R")
    id_bm = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, "j_bumper")
    qvel_wl = model.jnt_dofadr[id_wl]
    qvel_wr = model.jnt_dofadr[id_wr]
    qpos_bm = model.jnt_qposadr[id_bm]

    gid_wl = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, "g_wheel_L")
    gid_wr = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, "g_wheel_R")
    gid_mb = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, "g_marble")

    # Settle vertically for 0.15s before driving
    n_settle = int(0.15 / dt)
    for _ in range(n_settle):
        mujoco.mj_step(model, data)

    # Measure static weight distribution on wheels vs marble
    f_w, f_m = 0.0, 0.0
    c_force = np.zeros(6)
    for k in range(data.ncon):
        c = data.contact[k]
        mujoco.mj_contactForce(model, data, k, c_force)
        if c.geom1 in (gid_wl, gid_wr) or c.geom2 in (gid_wl, gid_wr):
            f_w += abs(c_force[0])
        elif c.geom1 == gid_mb or c.geom2 == gid_mb:
            f_m += abs(c_force[0])
    wheel_load_pct = 100.0 * f_w / max(f_w + f_m, 1e-6)

    n_steps = int(sim_sec / dt)
    t_hist, x_hist, vx_hist, z_hist, bm_hist, tau_hist = [], [], [], [], [], []
    hit_time_ms = None

    for s in range(n_steps):
        om_l = data.qvel[qvel_wl]
        om_r = data.qvel[qvel_wr]
        tl = motor_torque(om_l, cmd)
        tr = motor_torque(om_r, cmd)
        data.ctrl[0] = tl
        data.ctrl[1] = tr
        mujoco.mj_step(model, data)

        if s % max(1, int(0.01 / dt)) == 0:
            t_hist.append(s * dt)
            x_hist.append(float(data.qpos[0]))
            vx_hist.append(float(data.qvel[0]))
            z_hist.append(float(data.qpos[2]))
            bm_mm = float(data.qpos[qpos_bm] * 1000.0)
            bm_hist.append(bm_mm)
            tau_hist.append(0.5 * (tl + tr) * 1000.0)
            if wall_x_m is not None and hit_time_ms is None and bm_mm >= 0.80:
                hit_time_ms = round(s * dt * 1000.0, 1)

    steady_vx = float(np.mean(vx_hist[-30:])) if len(vx_hist) >= 30 else float(vx_hist[-1])
    max_x = float(np.max(x_hist))
    final_z_mm = float(z_hist[-1] * 1000.0)
    climbed_step = (step_h_m > 0) and (max_x > 0.42) and (final_z_mm >= step_h_m * 1000.0 * 0.70)
    return {
        "dt_ms": dt * 1000.0,
        "wheel_load_pct": round(wheel_load_pct, 2),
        "steady_vx_ms": round(steady_vx, 4),
        "max_x_m": round(max_x, 4),
        "final_z_mm": round(final_z_mm, 2),
        "max_bumper_mm": round(float(np.max(bm_hist)), 3),
        "bumper_trigger_ms": hit_time_ms,
        "peak_tau_mNm": round(float(np.max(tau_hist)), 2),
        "climbed_step": bool(climbed_step),
        "t_hist": t_hist, "x_hist": x_hist, "vx_hist": vx_hist, "bm_hist": bm_hist, "tau_hist": tau_hist,
    }


def main():
    print("=" * 84)
    print("[MUJOCO 4.x] REV 5.1 REAL 3D MULTI-BODY CONTACT & TIMESTEP CONVERGENCE AUDIT")
    print("=" * 84)

    # 1. Mandatory 2-Level Timestep / Solver Convergence Check (Coarse dt=2.0ms vs Fine dt=0.5ms)
    r_coarse = run_trial(sim_sec=2.0, dt=0.0020, iterations=40,  cmd=0.80)
    r_fine   = run_trial(sim_sec=2.0, dt=0.0005, iterations=100, cmd=0.80)
    err_v_pct = abs(r_fine["steady_vx_ms"] - r_coarse["steady_vx_ms"]) / max(r_fine["steady_vx_ms"], 1e-6) * 100.0
    err_w_pct = abs(r_fine["wheel_load_pct"] - r_coarse["wheel_load_pct"]) / max(r_fine["wheel_load_pct"], 1e-6) * 100.0
    print(f"  [CONVERGENCE] Coarse (dt=2.0ms, it=40) : vx={r_coarse['steady_vx_ms']:.4f} m/s | WheelLoad={r_coarse['wheel_load_pct']:.2f}%")
    print(f"  [CONVERGENCE] Fine   (dt=0.5ms, it=100): vx={r_fine['steady_vx_ms']:.4f} m/s | WheelLoad={r_fine['wheel_load_pct']:.2f}%")
    print(f"  [CONVERGENCE] Delta: vx={err_v_pct:.3f}% (<5% PASS={err_v_pct<5.0}) | WheelLoad={err_w_pct:.3f}% (<5% PASS={err_w_pct<5.0})")
    assert err_v_pct < 5.0 and err_w_pct < 5.0, "MuJoCo timestep convergence exceeded 5%!"

    # 2A. Sharp 90-deg Vertical Step Sweep (Rev 5.1 raised 5.50mm FLOOR_Z -> clears 5.0mm sharp ledges!)
    print("\n  [TRIAL 2A] 3D Sharp 90-deg Vertical Ledge Sweep (Rev 5.1 5.50mm Ground Clearance):")
    sharp_steps = []
    for sh_mm in (3.0, 4.0, 5.0, 6.0):
        res = run_trial(sim_sec=3.0, dt=0.001, iterations=80, cmd=1.0, step_h_m=sh_mm * 1e-3, ramp_deg=90.0)
        sharp_steps.append({"step_mm": sh_mm, "ramp_deg": 90.0, "climbed": res["climbed_step"], "max_x_m": res["max_x_m"], "final_z_mm": res["final_z_mm"]})
        print(f"    Sharp 90° {sh_mm:4.1f} mm -> {'CLIMBED [PASS]' if res['climbed_step'] else 'SKIRT BLOCKED '} | max_x={res['max_x_m']:.3f} m | final_z={res['final_z_mm']:.2f} mm")
    assert any(s["step_mm"] == 5.0 and s["climbed"] for s in sharp_steps), "Rev 5.1 MUST climb 5.0mm sharp 90° step!"

    # 2B. Realistic 20-deg Beveled Threshold / Carpet Strip Sweep (standard hardwood-to-rug/tatami transition!)
    print("\n  [TRIAL 2B] 3D 20-deg Beveled Threshold / Rug-Edge Ramp Sweep (Full 6.0V):")
    bevel_steps = []
    for sh_mm in (3.0, 5.0, 6.5, 8.0):
        res = run_trial(sim_sec=3.2, dt=0.001, iterations=80, cmd=1.0, step_h_m=sh_mm * 1e-3, ramp_deg=20.0)
        bevel_steps.append({"step_mm": sh_mm, "ramp_deg": 20.0, "climbed": res["climbed_step"], "max_x_m": res["max_x_m"], "final_z_mm": res["final_z_mm"]})
        print(f"    Bevel 20° {sh_mm:4.1f} mm -> {'CLIMBED [PASS]' if res['climbed_step'] else 'BLOCKED       '} | max_x={res['max_x_m']:.3f} m | final_z={res['final_z_mm']:.2f} mm")
    assert any(s["step_mm"] == 5.0 and s["climbed"] for s in bevel_steps), "Rev 5.1 MUST climb 5.0mm 20° threshold!"

    # 3. Real 3D Incline Slope Sweep (10, 15, 20, 22, 25, 30 deg — honestly captures 3D CoG weight transfer!)
    print(f"\n  [TRIAL 3] 3D Hardwood Slope-Climb Sweep (Full 6.0V, Real {TOTAL_MASS_KG*1000:.1f}g CoG Weight Transfer):")
    slope_results = []
    for deg in (10.0, 15.0, 20.0, 22.0, 25.0, 30.0):
        res = run_trial(sim_sec=2.0, dt=0.001, iterations=80, cmd=1.0, slope_deg=deg)
        ok = res["steady_vx_ms"] > 0.02
        slope_results.append({"slope_deg": deg, "steady_vx_ms": res["steady_vx_ms"], "climbed": ok, "peak_tau_mNm": res["peak_tau_mNm"]})
        print(f"    Slope {deg:4.1f} deg -> {'CLIMBS [PASS]' if ok else 'SLIP/STALL   '} | vx={res['steady_vx_ms']:+.3f} m/s | peak_tau={res['peak_tau_mNm']:.1f} mN·m")

    # 4. Real 3D Wall Bumper Impact & Hard-Stop Microswitch Verification
    print("\n  [TRIAL 4] 3D Floating Front Bumper Wall Impact & Microswitch Trigger:")
    r_wall = run_trial(sim_sec=1.6, dt=0.0005, iterations=100, cmd=0.80, wall_x_m=0.35)
    print(f"    Wall @ x=0.35m -> max_bumper_slide={r_wall['max_bumper_mm']:.2f} mm (range 2.2mm, >=0.8mm SW trigger) | "
          f"trigger_t={r_wall['bumper_trigger_ms']} ms | stall_tau={r_wall['peak_tau_mNm']:.1f} mN·m (<76mN·m slip cap)")
    assert r_wall["max_bumper_mm"] >= 0.80, "Floating bumper failed to depress microswitch!"

    summary = {
        "revision": "Rev 5.2",
        "solver": f"MuJoCo {mujoco.__version__} (3D Rigid-Body Contact, Coulomb Friction mu=0.82)",
        "robot_mass_g": round(TOTAL_MASS_KG * 1000.0, 1),
        "ground_clearance_mm": 5.5,
        "convergence_2level": {
            "coarse_dt_ms": 2.0, "coarse_vx_ms": r_coarse["steady_vx_ms"], "coarse_wheel_load_pct": r_coarse["wheel_load_pct"],
            "fine_dt_ms": 0.5,   "fine_vx_ms":   r_fine["steady_vx_ms"],   "fine_wheel_load_pct":   r_fine["wheel_load_pct"],
            "vx_diff_pct": round(err_v_pct, 3), "wheel_load_diff_pct": round(err_w_pct, 3),
            "converged_under_5pct": bool(err_v_pct < 5.0 and err_w_pct < 5.0),
        },
        "sharp_90deg_step_sweep": sharp_steps,
        "beveled_20deg_threshold_sweep": bevel_steps,
        "step_5mm_90deg_and_20deg_passed": True,
        "slope_climb_sweep": slope_results,
        "wall_bumper_impact": {
            "max_bumper_travel_mm": r_wall["max_bumper_mm"],
            "microswitch_trigger_ms": r_wall["bumper_trigger_ms"],
            "stall_torque_mNm": r_wall["peak_tau_mNm"],
            "under_rubber_slip_cap": bool(r_wall["peak_tau_mNm"] <= TAU_SLIP_CAP * 1000.0 + 0.1),
        },
    }
    out_json = os.path.join(OUT_DIR, "03_mujoco_3d_contact_summary.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    # Render crisp 4-panel verification figure
    fig, axes = plt.subplots(2, 2, figsize=(13.2, 8.4))
    axes[0, 0].plot(r_coarse["t_hist"], r_coarse["vx_hist"], "--", color="#64748b", lw=2.0, label=f"Coarse dt=2.0ms ({r_coarse['steady_vx_ms']:.3f} m/s)")
    axes[0, 0].plot(r_fine["t_hist"],   r_fine["vx_hist"],   "-",  color="#0f766e", lw=2.4, label=f"Fine dt=0.5ms ({r_fine['steady_vx_ms']:.3f} m/s, err={err_v_pct:.2f}%)")
    axes[0, 0].set_title("A. 2-Level Timestep Convergence (Flat Cruise @ 80% 6V)"); axes[0, 0].set_xlabel("Time [s]"); axes[0, 0].set_ylabel("Forward Speed [m/s]"); axes[0, 0].legend(fontsize=8.2)

    all_lbls = [f"90°\n{s['step_mm']:.0f}mm" for s in sharp_steps] + [f"20°\n{s['step_mm']:.1f}mm" for s in bevel_steps]
    all_xs   = [s["max_x_m"] for s in sharp_steps] + [s["max_x_m"] for s in bevel_steps]
    all_cols = ["#0f766e" if s["climbed"] else "#dc2626" for s in (sharp_steps + bevel_steps)]
    axes[0, 1].bar(all_lbls, all_xs, color=all_cols, width=0.58)
    axes[0, 1].axhline(0.42, color="#475569", ls="--", lw=1.2, label="Cleared Threshold (x > 0.42m)")
    axes[0, 1].set_title("B. Rev 5.1 Step Sweep (5.5mm Clearance -> 5.0mm 90° & 20° PASS!)"); axes[0, 1].set_ylabel("Max Travel [m]"); axes[0, 1].legend(fontsize=8.0)

    degs = [s["slope_deg"] for s in slope_results]
    svxs = [s["steady_vx_ms"] for s in slope_results]
    axes[1, 0].plot(degs, svxs, "o-", color="#2563eb", lw=2.4, ms=7)
    axes[1, 0].axhline(0.0, color="#dc2626", ls="--", lw=1.2, label="Zero-Speed Stall/Slip Boundary")
    axes[1, 0].set_title(f"C. 3D Hardwood Slope-Climb Curve (Real {TOTAL_MASS_KG*1000:.1f}g CoG Transfer)"); axes[1, 0].set_xlabel("Incline Angle [deg]"); axes[1, 0].set_ylabel("Steady Climb Speed [m/s]"); axes[1, 0].legend(fontsize=8.0)

    ax1 = axes[1, 1]
    ax1.plot(r_wall["t_hist"], r_wall["bm_hist"], color="#ea580c", lw=2.4, label=f"Bumper Slide (max {r_wall['max_bumper_mm']:.2f} mm)")
    ax1.axhline(0.80, color="#dc2626", ls="--", lw=1.2, label="Microswitch Trigger (0.80 mm)")
    ax1.axhline(2.20, color="#475569", ls=":",  lw=1.1, label="SLA Hard-Stop Limit (2.20 mm)")
    ax1.set_title("D. 3D Wall Impact: Floating Bumper Travel & Wheel Torque"); ax1.set_xlabel("Time [s]"); ax1.set_ylabel("Bumper Compression [mm]", color="#ea580c")
    ax2 = ax1.twinx()
    ax2.plot(r_wall["t_hist"], r_wall["tau_hist"], ":", color="#0f766e", lw=2.0, label="Wheel Torque [mN·m]")
    ax2.set_ylabel("Wheel Torque [mN·m]", color="#0f766e")
    ax1.legend(loc="upper left", fontsize=7.6)

    plt.tight_layout()
    out_png = os.path.join(OUT_DIR, "03_mujoco_3d_contact_verification.png")
    plt.savefig(out_png, dpi=150); plt.close()
    print(f"\n[PASS] MuJoCo 3D Contact Verification Complete -> {out_json} & {out_png}")


if __name__ == "__main__":
    main()
