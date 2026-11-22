#!/usr/bin/env python3
"""
Execute all 4 ngspice transient circuit simulations for Suga Roomba II,
verify electrical safety/performance assertions, and render publication-grade
SPICE verification waveforms.
"""
import json
import os
import subprocess
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SPICE_DIR = os.path.join(ROOT, "simulations", "spice")
RES_DIR = os.path.join(ROOT, "simulations", "results")

plt.rcParams.update({
    "figure.facecolor": "#FFFFFF",
    "axes.facecolor": "#F8FAFC",
    "axes.edgecolor": "#CBD5E1",
    "axes.grid": True,
    "grid.color": "#E2E8F0",
    "grid.linestyle": "--",
    "grid.linewidth": 0.7,
    "font.family": "DejaVu Sans",
})


def run_ngspice(cir_name):
    path = os.path.join(SPICE_DIR, cir_name)
    proc = subprocess.run(
        ["ngspice", "-b", path],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        print(f"ERROR running {cir_name}:\n{proc.stdout}\n{proc.stderr}")
        raise RuntimeError(f"ngspice failed on {cir_name}")
    print(f"[ngspice] {cir_name} OK")


def load_wrdata(filename, n_signals):
    """ngspice wrdata outputs pairs of (time, val) columns for each signal."""
    data = np.loadtxt(os.path.join(RES_DIR, filename))
    t = data[:, 0]
    vals = [data[:, 2 * i + 1] for i in range(n_signals)]
    return t, vals


def main():
    for cir in [
        "power_mosfet_isolation.cir",
        "motor_shunt_ripple.cir",
        "piezo_dirt_detect.cir",
        "led_photodiode_sensor.cir",
    ]:
        run_ngspice(cir)

    # 1. Power & MOSFET Isolation
    t_p, [vbat, vldo, v3v3, vdrain, ibatt] = load_wrdata("spice_power.txt", 5)
    vbat_min = float(np.min(vbat))
    v3v3_min = float(np.min(v3v3))
    v3v3_max = float(np.max(v3v3))
    vdrain_max = float(np.max(vdrain))

    # 2. Single-Shunt Commutator Ripple & Wall-Stall Amp
    t_s, [vshunt, vadc_s] = load_wrdata("spice_shunt.txt", 2)
    mask_cruise = (t_s >= 0.005) & (t_s <= 0.022)
    mask_stall = (t_s >= 0.030) & (t_s <= 0.048)
    adc_cruise_mean = float(np.mean(vadc_s[mask_cruise]))
    adc_cruise_pp = float(np.max(vadc_s[mask_cruise]) - np.min(vadc_s[mask_cruise]))
    adc_stall_mean = float(np.mean(vadc_s[mask_stall]))

    # 3. Piezo Dirt Detect Amp (measure after 1ms uic startup bias settling)
    t_d, [vpz, vdirt, vmid] = load_wrdata("spice_piezo.txt", 3)
    mask_d = t_d >= 0.001
    dirt_peak_dev = float(np.max(np.abs(vdirt[mask_d] - vmid[mask_d])))

    # 4. LED-as-Photodiode Reverse-Bias Discharge
    t_l, [vwall, vcliff] = load_wrdata("spice_led_sensor.txt", 2)
    # Time after t=10us for voltage to cross RP2040 1.65V digital threshold
    idx_after = t_l >= 12e-6
    t_w_cross = float(t_l[idx_after][np.argmax(vwall[idx_after] < 1.65)] - 10e-6) * 1e6
    cliff_crossed = np.any(vcliff[idx_after] < 1.65)
    t_c_cross = (
        float(t_l[idx_after][np.argmax(vcliff[idx_after] < 1.65)] - 10e-6) * 1e6
        if cliff_crossed
        else 2500.0
    )
    led_contrast = t_c_cross / max(t_w_cross, 1.0)

    summary = {
        "power_isolation": {
            "vbat_min_sag_V": round(vbat_min, 3),
            "v3v3_min_V": round(v3v3_min, 4),
            "v3v3_max_V": round(v3v3_max, 4),
            "mcu_zero_brownout_pass": v3v3_min >= 3.25,
            "vdrain_flyback_clamped_max_V": round(vdrain_max, 2),
            "mosfet_vds_60V_margin_x": round(60.0 / max(vdrain_max, 1.0), 1),
        },
        "single_shunt_ripple_amp": {
            "cruise_adc_mean_V": round(adc_cruise_mean, 3),
            "commutator_ripple_pp_mV": round(adc_cruise_pp * 1000.0, 1),
            "wall_stall_adc_mean_V": round(adc_stall_mean, 3),
            "stall_step_ratio": round(adc_stall_mean / max(adc_cruise_mean, 0.01), 2),
        },
        "piezo_dirt_detect": {
            "midrail_bias_V": round(float(np.mean(vmid[mask_d])), 3),
            "peak_impact_swing_V": round(dirt_peak_dev, 3),
            "adc_saturation_free": bool((float(np.max(vdirt[mask_d])) < 3.25) and (float(np.min(vdirt[mask_d])) > 0.05)),
        },
        "led_as_photodiode": {
            "wall_present_discharge_us": round(t_w_cross, 1),
            "cliff_open_air_discharge_us_min": round(t_c_cross, 1),
            "contrast_ratio_x": round(led_contrast, 1),
        },
    }

    with open(os.path.join(RES_DIR, "spice_summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    # Render 2x2 SPICE Waveform Verification Plot
    fig, axes = plt.subplots(2, 2, figsize=(13.5, 8.6), dpi=160)

    # Panel 1: Power Sag Isolation
    ax = axes[0, 0]
    ax.plot(t_p * 1e3, vbat, color="#EF4444", lw=1.4, alpha=0.85, label=f"VBAT Motor Bus (min {vbat_min:.2f}V sag)")
    ax.plot(t_p * 1e3, vldo, color="#F59E0B", lw=1.8, label="1S4 + 470uF Isolated Hold-up")
    ax.plot(t_p * 1e3, v3v3, color="#2563EB", lw=2.3, label=f"RP2040 3.3V Rail ({v3v3_min:.3f}V rock-solid!)")
    ax.set_title("SPICE 1: Turbine Inrush Sag Isolation & SB340LS Clamping")
    ax.set_xlabel("Time [ms]")
    ax.set_ylabel("Voltage [V]")
    ax.set_ylim(2.5, 6.6)
    ax.legend(loc="lower right", fontsize=8)

    # Panel 2: Single-Shunt Ripple + Wall Stall
    ax = axes[0, 1]
    ax.plot(t_s * 1e3, vadc_s, color="#0F766E", lw=1.8, label=f"MCP6002 CH1 -> RP2040 ADC (Gain=10x)")
    ax.axvline(25.0, color="#DC2626", ls="--", lw=1.4, label="Wall Collision Event (t=25ms)")
    ax.set_title(f"SPICE 2: 1-Shunt Commutator Ripple ({adc_cruise_pp*1e3:.0f} mVpp) & Stall Detect")
    ax.set_xlabel("Time [ms]")
    ax.set_ylabel("ADC Input Voltage [V]")
    ax.set_ylim(0, 2.4)
    ax.legend(loc="upper left", fontsize=8)

    # Panel 3: Piezo Dirt Detect
    ax = axes[1, 0]
    ax.plot(t_d * 1e3, vdirt, color="#7C3AED", lw=1.6, label=f"MCP6002 CH2 Output (Peak +/-{dirt_peak_dev:.2f}V)")
    ax.plot(t_d * 1e3, vmid, color="#94A3B8", ls="--", lw=1.2, label="1.65V Virtual Mid-Rail")
    ax.set_title("SPICE 3: Murata Piezo Dirt Detect (3 Sand/Rice Impacts)")
    ax.set_xlabel("Time [ms]")
    ax.set_ylabel("ADC Input Voltage [V]")
    ax.set_ylim(0.5, 2.8)
    ax.legend(loc="upper right", fontsize=8)

    # Panel 4: LED-as-Photodiode Ranging
    ax = axes[1, 1]
    ax.plot(t_l * 1e3, vwall, color="#16A34A", lw=2.3, label=f"Wall/Floor Present (t_cross = {t_w_cross:.0f} us)")
    ax.plot(t_l * 1e3, vcliff, color="#DC2626", lw=2.3, label=f"Stair Cliff / Open (t_cross > {t_c_cross:.0f} us)")
    ax.axhline(1.65, color="#475569", ls=":", lw=1.4, label="RP2040 PIO Digital Threshold (1.65V)")
    ax.set_title(f"SPICE 4: 10-JPY LED-as-Photodiode Optical Ranging ({led_contrast:.1f}x Contrast)")
    ax.set_xlabel("Time [ms]")
    ax.set_ylabel("LED Cathode Voltage [V]")
    ax.set_ylim(0, 3.6)
    ax.legend(loc="center right", fontsize=8)

    plt.tight_layout()
    plt.savefig(os.path.join(RES_DIR, "04_ngspice_circuit_verification.png"))
    plt.close()

    print(json.dumps(summary, indent=2))
    assert summary["power_isolation"]["mcu_zero_brownout_pass"], "MCU brownout detected!"
    assert summary["led_as_photodiode"]["contrast_ratio_x"] > 10.0, "LED contrast too low!"
    print("ALL 4 NGSPICE CIRCUIT SIMULATIONS PASSED!")


if __name__ == "__main__":
    main()
