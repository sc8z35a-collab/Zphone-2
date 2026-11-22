#!/usr/bin/env python3
"""
菅ルンバ II Rev 3.0 (CH32X035 5.0V Single-Chip Mixed-Signal) — Real ngspice 42 Verification!
Simulates all 4 core Rev 3.0 electronic circuits with real ngspice transient analysis:
  1. 5S NiMH Sag Isolation (1S4 + 470uF + LP2950L-50 -> 5.00V) & Direct 5V 2SK4017 PWM (No DTC114EL!)
  2. 0-JPY 0.25-ohm Bifilar Copper Shunt + CH32X035 On-Chip OPA1 (13x E1 10k-only) Ripple Tach
  3. 30-JPY Murata Piezo Acoustic Dirt Detect + CH32X035 On-Chip OPA2 (2.50V mid-rail, +51% headroom!)
  4. 40-JPY Red LED-as-Photodiode 5.0V Reverse-Bias Optical Ranging (PIOC Wall/Cliff Contrast)
"""

import json
import os
import subprocess
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPICE_DIR = os.path.join(ROOT, "simulations", "spice")
RES_DIR   = os.path.join(ROOT, "simulations", "results")
os.makedirs(SPICE_DIR, exist_ok=True)
os.makedirs(RES_DIR, exist_ok=True)

CIRCUITS = {
    "power_mosfet_isolation.cir": """* Rev 3.0: 5S NiMH Sag Isolation (1S4 Schottky + 470uF + LP2950L-50 5.0V) + Direct 5V CH32X035 -> 2SK4017 PWM
.model D_1S4 D(IS=1.5e-5 RS=0.09 N=1.06 BV=40)
.model D_SB340 D(IS=2e-5 RS=0.045 N=1.05 BV=40)
.model M_2SK4017 NMOS(VTO=1.9 KP=14.0 RD=0.022 RS=0.014 CBD=320p CBS=350p)

* 5S NiMH Battery (6.0V nom, 0.21 ohm pack ESR)
V_BATT v_raw 0 DC 6.0
R_ESR  v_raw vbat 0.21
C_BULK vbat 0 470u IC=6.0

* Sag-Isolated 5.0V Rail (1S4 Schottky Vf=0.25V@35mA + 470uF Holdup + LP2950L-50-T92 0.38V dropout)
D_ISO  vbat vldo_in D_1S4
C_HOLD vldo_in 0 470u IC=5.75
B_LDO  v5v 0 V = (V(vldo_in) > 5.38) ? 5.0 : ((V(vldo_in) > 0.38) ? (V(vldo_in) - 0.38) : 0)
C_5V   v5v 0 1u IC=5.0
R_MCU  v5v 0 140 ; 35.7mA (CH32X035 18mA + 5V USB Mouse 17.7mA)

* Direct 5.0V 25kHz PWM from CH32X035 PA0 (NO DTC114EL needed! Safe pulldown 10k)
V_PWM  pwm_pa0 0 PULSE(0 5.0 0.5m 60n 60n 32u 40u)
R_G    pwm_pa0 gate 100
R_PD   gate 0 10k

* RS-385PH Turbine (15A startup inrush pulse at t=0.5ms..4.5ms, L=120uH, Ra=0.382R + BackEMF)
L_MOT  vbat mot_mid 120u
R_MOT  mot_mid mot_emf 0.382
V_EMF  mot_emf drain PWL(0 0 0.5m 0 4.5m 4.55 12m 4.55)
D_FLY  drain vbat D_SB340
M_FET  drain gate 0 0 M_2SK4017

.tran 2u 10m 0 2u uic
.control
run
set wr_vecnames
wrdata {out} v(vbat) v(vldo_in) v(v5v) v(drain) v(gate)
quit
.endc
.end
""",
    "motor_shunt_ripple.cir": """* Rev 3.0: 0-JPY 0.25R Salvaged Bifilar Copper Shunt + CH32X035 On-Chip OPA1 (Gain=13x E1 10k-only)
V_DD v5v 0 DC 5.0

* Drive Motor Current: Normal cruise (0.19A + 48mApp commutator ripple @ 175Hz) -> Wall Stall at t=30ms (0.46A)
I_DRIVE 0 shunt_n PWL(0 0.19 29.8m 0.19 30.2m 0.46 60m 0.46)
I_RIP   0 shunt_n SIN(0 0.024 175 0 0)
R_SHUNT shunt_n 0 0.25 ; 0-JPY 22cm bifilar copper strand from salvaged Daiso mouse cable!

* CH32X035 On-Chip OPA1 (5V Rail-to-Rail, E1 10k-only: Rf=30k = 3x10k series, Rg=2.5k = 4x10k parallel -> G=13x)
R_G   inv_n 0 2.5k
R_F   adc_shunt inv_n 30k
B_OPA adc_shunt 0 V = 2.5 + 2.45 * tanh(2e3 * (V(shunt_n) - V(inv_n)))
C_FILT adc_shunt 0 10n

.tran 50u 60m 0 50u
.control
run
set wr_vecnames
wrdata {out} v(shunt_n) v(adc_shunt)
quit
.endc
.end
""",
    "piezo_dirt_detect.cir": """* Rev 3.0: 30-JPY Murata PKM13EPYH4000 Piezo + CH32X035 On-Chip OPA2 (VMID = 2.50V -> +51% headroom!)
V_DD v5v 0 DC 5.0
R_B1 v5v vmid 10k
R_B2 vmid 0 10k
C_M  vmid 0 1u IC=2.5

* Piezo sand & rice grain impact bursts inside horizontal PET cyclone inlet (4kHz damped bursts)
V_IMP1 n1 vmid SIN(0 0.24 4000 3m 450)
V_IMP2 n2 n1   SIN(0 0.36 4200 10m 380)
C_PZ   n2 pz_in 18n
R_PZ   pz_in vmid 20k ; 2x 10k E1 series

* CH32X035 On-Chip OPA2 Non-Inverting AC Gain = 1 + 20k/5k = 5.0x around VMID = 2.50V
R_G2   inv2 vmid 5k   ; 2x 10k parallel
R_F2   adc_dirt inv2 20k ; 2x 10k series
B_OPA2 adc_dirt 0 V = 2.5 + 2.45 * tanh(2e3 * (V(pz_in) - V(inv2)))

.tran 10u 18m 0 10u uic
.control
run
set wr_vecnames
wrdata {out} v(pz_in) v(adc_dirt)
quit
.endc
.end
""",
    "led_photodiode_sensor.cir": """* Rev 3.0: 40-JPY Red LED-as-Photodiode 5.0V Reverse-Bias Optical Ranging (CH32X035 PIOC)
* Physical reverse-biased PN junction: Cj=30pF, photocurrent rolls off smoothly to 0V, forward LED clamp
.model D_RED D(IS=1e-18 N=1.8 RS=5 CJO=30p VJ=1.6)
C_J1 led_floor 0 30p IC=5.0
D_1  0 led_floor D_RED
B_PH1 led_floor 0 I = 320n * (0.5 + 0.5 * tanh(V(led_floor) * 20))
R_LK1 led_floor 0 250Meg

C_J2 led_cliff 0 30p IC=5.0
D_2  0 led_cliff D_RED
B_PH2 led_cliff 0 I = 28n * (0.5 + 0.5 * tanh(V(led_cliff) * 20))
R_LK2 led_cliff 0 250Meg

.tran 2u 3.0m 0 2u uic
.control
run
set wr_vecnames
wrdata {out} v(led_floor) v(led_cliff)
quit
.endc
.end
""",
}

def load_wrdata(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            parts = s.split()
            try:
                rows.append([float(x) for x in parts])
            except ValueError:
                continue
    return np.array(rows)

def main():
    out_files = {
        "power_mosfet_isolation.cir": os.path.join(RES_DIR, "spice_power.txt"),
        "motor_shunt_ripple.cir":     os.path.join(RES_DIR, "spice_shunt.txt"),
        "piezo_dirt_detect.cir":      os.path.join(RES_DIR, "spice_piezo.txt"),
        "led_photodiode_sensor.cir":  os.path.join(RES_DIR, "spice_led_sensor.txt"),
    }
    for fname, tmpl in CIRCUITS.items():
        cir_path = os.path.join(SPICE_DIR, fname)
        out_txt = out_files[fname]
        with open(cir_path, "w", encoding="utf-8") as f:
            f.write(tmpl.format(out=out_txt))
        proc = subprocess.run(["ngspice", "-b", cir_path], capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(f"ngspice failed on {fname}:\n{proc.stderr}")
        print(f"[ngspice OK] {fname} -> {os.path.basename(out_txt)}")

    # Parse all 4 real simulation outputs (wrdata alternates time, val, time, val...)
    d_pwr = load_wrdata(out_files["power_mosfet_isolation.cir"])
    t_pwr, vbat, vldo, v5v, vdrn, vgate = d_pwr[:,0]*1e3, d_pwr[:,1], d_pwr[:,3], d_pwr[:,5], d_pwr[:,7], d_pwr[:,9]

    d_sh = load_wrdata(out_files["motor_shunt_ripple.cir"])
    t_sh, vsh, vadc_sh = d_sh[:,0]*1e3, d_sh[:,1], d_sh[:,3]

    d_pz = load_wrdata(out_files["piezo_dirt_detect.cir"])
    t_pz, vpz, vadc_pz = d_pz[:,0]*1e3, d_pz[:,1], d_pz[:,3]

    d_ld = load_wrdata(out_files["led_photodiode_sensor.cir"])
    t_ld, vflr, vclf = d_ld[:,0]*1e3, d_ld[:,1], d_ld[:,3]

    # Metrics
    vbat_min = float(np.min(vbat))
    v5v_min  = float(np.min(v5v[t_pwr >= 0.2]))
    vdrn_max = float(np.max(vdrn))

    m_norm = (t_sh >= 5.0) & (t_sh <= 28.0)
    m_stal = (t_sh >= 35.0) & (t_sh <= 58.0)
    sh_rip_mv = float((np.max(vadc_sh[m_norm]) - np.min(vadc_sh[m_norm])) * 1000.0)
    sh_jump   = float(np.mean(vadc_sh[m_stal]) / np.mean(vadc_sh[m_norm]))

    m_pz = t_pz >= 1.5
    pz_swing = float(np.max(np.abs(vadc_pz[m_pz] - 2.50)))
    pz_sat_free = bool(np.min(vadc_pz[m_pz]) > 0.15 and np.max(vadc_pz[m_pz]) < 4.85)

    # Discharge time from 5.0V to 2.5V CMOS threshold
    t_flr_us = float(t_ld[np.argmax(vflr <= 2.5)] * 1000.0)
    t_clf_us = float(t_ld[np.argmax(vclf <= 2.5)] * 1000.0)
    led_ratio = t_clf_us / max(t_flr_us, 1e-6)

    spice_summary = {
        "revision": "Rev 3.0 CH32X035 5.0V Single-Chip Mixed-Signal",
        "power_isolation": {
            "vbat_min_sag_V": round(vbat_min, 3),
            "v5v_rail_min_V": round(v5v_min, 3),
            "zero_brownout": bool(v5v_min >= 4.95),
            "mosfet_direct_5V_gate_V": round(float(np.max(vgate)), 2),
            "flyback_clamp_max_V": round(vdrn_max, 2),
            "fet_voltage_margin_x": round(60.0 / vdrn_max, 1),
        },
        "bifilar_shunt_opa1": {
            "shunt_resistance_ohm": 0.25,
            "shunt_cost_jpy": 0,
            "ripple_peak_to_peak_mV": round(sh_rip_mv, 1),
            "wall_stall_jump_ratio": round(sh_jump, 2),
        },
        "piezo_dirt_opa2": {
            "mid_rail_V": 2.50,
            "peak_deviation_V": round(pz_swing, 3),
            "adc_saturation_free": pz_sat_free,
        },
        "led_photodiode_5V": {
            "floor_decay_us": round(t_flr_us, 1),
            "cliff_decay_us": round(t_clf_us, 1),
            "contrast_ratio": round(led_ratio, 2),
        },
    }
    with open(os.path.join(RES_DIR, "spice_summary.json"), "w", encoding="utf-8") as f:
        json.dump(spice_summary, f, indent=2)

    # 2x2 Verification Waveform Dashboard
    fig, axes = plt.subplots(2, 2, figsize=(13, 8.2))
    axes[0,0].plot(t_pwr, vbat, color="#ea580c", lw=1.6, label=f"5S NiMH VBAT (min {vbat_min:.2f}V)")
    axes[0,0].plot(t_pwr, v5v,  color="#0f766e", lw=2.4, label=f"CH32X035 + Mouse 5.0V Rail (min {v5v_min:.3f}V)")
    axes[0,0].plot(t_pwr, vdrn, color="#3b82f6", lw=0.7, alpha=0.55, label=f"2SK4017 Drain Clamp (max {vdrn_max:.2f}V)")
    axes[0,0].set_title("A. 5V Sag-Isolated Supply & Direct-5V 2SK4017 PWM (No DTC114EL)")
    axes[0,0].set_xlabel("Time [ms]"); axes[0,0].set_ylabel("Voltage [V]"); axes[0,0].legend(fontsize=8, loc="lower right")

    axes[0,1].plot(t_sh, vadc_sh, color="#0f766e", lw=1.6, label=f"On-Chip OPA1 Out ({sh_rip_mv:.0f} mVpp ripple, {sh_jump:.2f}x stall)")
    axes[0,1].axvline(30.0, color="#ea580c", ls="--", lw=1.3, label="Wall Contact Event (t=30ms)")
    axes[0,1].set_title("B. 0-JPY 0.25R Bifilar Copper Shunt + CH32X035 On-Chip OPA1")
    axes[0,1].set_xlabel("Time [ms]"); axes[0,1].set_ylabel("ADC Voltage [V]"); axes[0,1].legend(fontsize=8)

    axes[1,0].plot(t_pz[m_pz], vadc_pz[m_pz], color="#2563eb", lw=1.3, label=f"On-Chip OPA2 Out (±{pz_swing:.2f}V around 2.50V)")
    axes[1,0].axhline(2.50, color="#64748b", ls=":", lw=1.0)
    axes[1,0].set_title("C. 30-JPY Murata Piezo Dirt Detect (5V Rail: +51% Headroom)")
    axes[1,0].set_xlabel("Time [ms]"); axes[1,0].set_ylabel("ADC Voltage [V]"); axes[1,0].set_ylim(0, 5.1); axes[1,0].legend(fontsize=8)

    axes[1,1].plot(t_ld*1e3, vflr, color="#0f766e", lw=2.3, label=f"Floor / Wall ({t_flr_us:.0f} us)")
    axes[1,1].plot(t_ld*1e3, vclf, color="#dc2626", lw=2.3, label=f"Stair Cliff Drop ({t_clf_us:.0f} us = {led_ratio:.1f}x!)")
    axes[1,1].axhline(2.5, color="#64748b", ls="--", lw=1.1, label="CH32X035 5V CMOS Threshold (2.5V)")
    axes[1,1].set_title("D. 40-JPY Red LED-as-Photodiode 5.0V Reverse-Bias Ranging")
    axes[1,1].set_xlabel("Discharge Time [us]"); axes[1,1].set_ylabel("LED Pin Voltage [V]"); axes[1,1].set_xlim(0, 2800); axes[1,1].set_ylim(-0.15, 5.4); axes[1,1].legend(fontsize=8)

    plt.tight_layout()
    plt.savefig(os.path.join(RES_DIR, "04_ngspice_circuit_verification.png"), dpi=150)
    plt.close()

    print("\n=== REV 3.0 NGSPICE VERIFICATION SUMMARY ===")
    print(json.dumps(spice_summary, indent=2))
    assert spice_summary["power_isolation"]["zero_brownout"]
    assert spice_summary["piezo_dirt_opa2"]["adc_saturation_free"]
    assert spice_summary["led_photodiode_5V"]["contrast_ratio"] > 7.0


if __name__ == "__main__":
    main()
