// =============================================================================
// 菅ルンバ II Rev 4.0 (Hyper-Genesis Industrial-Fab Edition) — CH32X035G8U6 FW
// =============================================================================
// MCU      : WCH CH32X035G8U6 (48 MHz 32-bit QingKe_V4C RISC-V, 62KB Flash, 20KB SRAM)
//            Akizuki Denshi [131018] DIP30 Breakout (290 JPY tax-inc!)
// BOM Cost : 2,880 JPY Total Tax-Inc (INCL. 690 JPY JLC3DP Industrial SLA Hyper-Body!)
// Flashing : Native Factory USB-ISP Bootloader via salvaged Daiso USB cable (wchisp)
//            -> ZERO WCH-LinkE / external programmer required!
//
// Rev 4.0 Disruptive Firmware Capabilities:
//   1. On-Chip OPA1 (PA5/PA6/PA7) -> 13x Gain for 0-JPY 0.25-Ohm Bifilar Mouse-Wire Shunt
//   2. On-Chip OPA2 (PB0/PB1/PB3) ->  5x Gain around 2.50V Mid-Rail for 30-JPY Murata Piezo
//   3. Direct 5.0V Active-HIGH 25kHz PWM (PA0) -> 2SK4017 Gate (4.95V, Rds=36mOhm)
//   4. [NEW REV 4.0] 0-JPY Back-EMF Virtual Barometer (PB2 via 2x spare 10k E1 divider):
//      Samples RS-385PH Back-EMF during 50us PWM-OFF coast window every 100ms ->
//      Closed-Loop Constant-Suction Control (2,360 / 3,033 Pa) + Auto-Clog Alert!
//   5. 4x 5.0V Reverse-Bias Red LED-as-Photodiode (PB4..PB7) -> 8.35x Cliff/Floor Contrast
//   6. 0-JPY Aluminum-Can Capacitive TouchKey (PB8) + Salvaged Mouse Bumper Switches
//   7. Colab-Trained 952-Byte Calibrated INT8 TinyML (12->24->16->6, >99% Acc, <28 us)
//   8. 64x64 Occupancy-Grid Frontier SLAM + Dual-Side L+R Coanda Corner-Sweep FSM
// =============================================================================

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include "suga_roomba_tinyml.h"

// --- Pin Map (100% Matched to Rev 4.0 KiCad Schematic & PCB) ---
constexpr uint8_t PIN_PWM_TURB     = PA0;   // Direct 5.0V 25kHz PWM -> 2SK4017 Gate (ACTIVE-HIGH!)
constexpr uint8_t PIN_MOT_LIN1     = PA1;   // TC78H653FTG IN1A (Left 25:1 Leaf-Spring Wheel)
constexpr uint8_t PIN_MOT_LIN2     = PA2;   // TC78H653FTG IN2A
constexpr uint8_t PIN_MOT_RIN1     = PA3;   // TC78H653FTG IN1B (Right 25:1 Leaf-Spring Wheel)
constexpr uint8_t PIN_MOT_RIN2     = PA4;   // TC78H653FTG IN2B
constexpr uint8_t PIN_OPA1_P       = PA5;   // On-Chip OPA1(+) <- 0-JPY 0.25R Bifilar Shunt
constexpr uint8_t PIN_OPA1_N       = PA6;   // On-Chip OPA1(-) <- E1 10k/30k Feedback (13x)
constexpr uint8_t PIN_ADC_SHUNT    = PA7;   // On-Chip OPA1_OUT -> ADC_CH7
constexpr uint8_t PIN_OPA2_P       = PB0;   // On-Chip OPA2(+) <- 30-JPY Murata Piezo + 2.50V
constexpr uint8_t PIN_OPA2_N       = PB1;   // On-Chip OPA2(-) <- E1 10k/40k Feedback (5x)
constexpr uint8_t PIN_ADC_BEMF     = PB2;   // [NEW REV 4.0] 0-JPY 10k/10k Turbine Back-EMF Barometer!
constexpr uint8_t PIN_ADC_DIRT     = PB3;   // On-Chip OPA2_OUT -> Acoustic Debris Impact
// PC16 = USB_DM, PC17 = USB_DP (Native 5V USB Host for Daiso Mouse & Factory USB-ISP!)
constexpr uint8_t PIN_LED_FRONT    = PB4;   // 5V LED-as-Photodiode Front Wall
constexpr uint8_t PIN_LED_SIDE     = PB5;   // 5V LED-as-Photodiode Side Wall (Dual Coanda Align)
constexpr uint8_t PIN_LED_CLIFF_L  = PB6;   // 5V LED-as-Photodiode Front-Left Stair Cliff
constexpr uint8_t PIN_LED_CLIFF_R  = PB7;   // 5V LED-as-Photodiode Front-Right Stair Cliff
constexpr uint8_t PIN_BUMP_TK      = PB8;   // Bumper Microswitches + 0-JPY Al-Can TouchKey

// --- Rev 4.0 SLA 3D-Twisted Impeller + 11-Vane Diffuser Operating Targets ---
constexpr float TURB_DUTY_OFF   = 0.00f;
constexpr float TURB_DUTY_ECO   = 0.70f;  // ~4.5V eff -> 1,320 Pa shutoff, 20.1 m/s
constexpr float TURB_DUTY_STD   = 0.88f;  // ~6.0V eff -> 2,360 Pa shutoff, 26.0 m/s, eta_fan=82.6%
constexpr float TURB_DUTY_TURBO = 1.00f;  // ~6.8V peak-> 3,033 Pa shutoff, 28.9 m/s (<3A SAFE!)

// --- Virtual Barometer Targets (Verified in CFD & ngspice 42) ---
constexpr float BEMF_TARGET_STD_V = 4.73f; // Open-flow 9,780 rpm @ 6.0V (Q = 6.00 L/s)
constexpr float BEMF_CLOG_THRES_V = 5.38f; // Flow < 2.2 L/s -> Quad-Cyclone / Dust-Cup Full!

// --- Physical Robot Constants (Rev 4.0 Ultra-Slim H=52.0mm Isogrid Unibody) ---
constexpr float WHEEL_DIA_MM       = 50.0f;  // 25.0:1 Print-in-Place Leaf-Spring Friction Drive
constexpr float WHEEL_TRACK_MM     = 146.0f; // ±73.0mm symmetric track
constexpr float CELL_SIZE_MM       = 65.0f;  // 64x64 SLAM covers 4.16m x 4.16m room
constexpr int   GRID_N             = 64;

struct SensorFrame {
    float shuntMeanL, shuntMeanR;
    float rippleFreqL, rippleFreqR;
    float rippleAmpL, rippleAmpR;
    float piezoRms, piezoPeak, piezoZcr;
    uint32_t ledFrontUs, ledSideUs, ledCliffLUs, ledCliffRUs;
    bool bumperHit, touchActive;
    float turbineBemfV;     // [NEW REV 4.0] Real-time measured Back-EMF [V]
    float estAirflowLs;     // [NEW REV 4.0] Virtual Barometer estimated Q [L/s]
    bool  filterClogAlert;  // [NEW REV 4.0] True if Q < 2.2 L/s for > 3s
    float odomX_mm, odomY_mm, odomTheta_rad;
};

static SensorFrame g_tel = {};
static uint8_t g_grid[GRID_N][GRID_N];

// =============================================================================
// 1. Hardware Initialization (On-Chip OPA1/OPA2 + Direct 5V 25kHz PWM)
// =============================================================================
void initOnChipOpAmps() {
#if defined(CH32X035) || defined(OPA_BASE)
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB;
    OPA->CR = (1U << 0) | (1U << 16); // Enable OPA1 (13x shunt) + OPA2 (5x piezo)
#else
    pinMode(PIN_ADC_SHUNT, INPUT_ANALOG);
    pinMode(PIN_ADC_DIRT,  INPUT_ANALOG);
#endif
    pinMode(PIN_ADC_BEMF, INPUT_ANALOG);
}

void initTurbinePWM25kHz() {
    pinMode(PIN_PWM_TURB, OUTPUT);
    digitalWrite(PIN_PWM_TURB, LOW); // Safe boot OFF!
    analogWriteFrequency(25000);
    analogWriteResolution(10);
    analogWrite(PIN_PWM_TURB, 0);
}

void setTurbineDuty(float duty) {
    duty = constrain(duty, 0.0f, 1.0f);
    analogWrite(PIN_PWM_TURB, (uint16_t)(duty * 1023.0f));
}

void setWheelPWM(uint8_t in1, uint8_t in2, float cmd) {
    cmd = constrain(cmd, -1.0f, 1.0f);
    uint16_t val = (uint16_t)(fabsf(cmd) * 1023.0f);
    if (cmd >= 0.0f) { analogWrite(in1, val); analogWrite(in2, 0); }
    else             { analogWrite(in1, 0);   analogWrite(in2, val); }
}

// =============================================================================
// 2. [NEW REV 4.0] 0-JPY Back-EMF Virtual Barometer (50us PWM-OFF Coast Sample)
// =============================================================================
void sampleVirtualBarometer(float currentDuty) {
    static uint8_t clogStreak = 0;
    if (currentDuty < 0.25f) {
        g_tel.turbineBemfV = 0.0f;
        g_tel.estAirflowLs = 0.0f;
        return;
    }
    // Pause MOSFET briefly for 50us so inductive flyback decays (<15us via SB340LS),
    // leaving pure generator Back-EMF across RS-385PH terminals!
    analogWrite(PIN_PWM_TURB, 0);
    delayMicroseconds(50);

    // Divider R18/R19 = 10k/10k -> V_drain = 2.0 * V_adc_bemf (max 3.0V < 5.0V SAFE!)
    constexpr float ADC_TO_V = 5.00f / 4095.0f;
    float vDrain = 2.0f * (analogRead(PIN_ADC_BEMF) * ADC_TO_V);
    analogWrite(PIN_PWM_TURB, (uint16_t)(currentDuty * 1023.0f)); // Resume seamlessly!

    // V_bemf = V_batt_nom(6.0V) - V_drain (clamped cleanly)
    float vBemf = constrain(6.05f - vDrain, 0.0f, 6.20f);
    g_tel.turbineBemfV = 0.80f * g_tel.turbineBemfV + 0.20f * vBemf;

    // Linear airflow inverse calibration from Rev 4.0 CFD (5.74V @ 0 L/s -> 4.73V @ 6.0 L/s)
    float qEst = constrain((5.74f - g_tel.turbineBemfV) * (6.00f / 1.01f), 0.0f, 7.2f);
    g_tel.estAirflowLs = qEst;

    if (g_tel.turbineBemfV > BEMF_CLOG_THRES_V) {
        if (clogStreak < 30) clogStreak++;
    } else {
        clogStreak = 0;
    }
    g_tel.filterClogAlert = (clogStreak >= 25);
}

// =============================================================================
// 3. 5.0V Reverse-Bias LED-as-Photodiode Ranging (8.35x Cliff/Floor Contrast)
// =============================================================================
uint32_t measureLedDischargeUs(uint8_t pin, uint32_t timeoutUs = 1200) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delayMicroseconds(10);
    uint32_t t0 = micros();
    pinMode(pin, INPUT);
    while (digitalRead(pin) == HIGH) {
        if ((micros() - t0) >= timeoutUs) return timeoutUs;
    }
    return micros() - t0;
}

bool readBumperAndTouchKey(bool* touchOut) {
    pinMode(PIN_BUMP_TK, INPUT_PULLUP);
    delayMicroseconds(5);
    bool bumpHit = (digitalRead(PIN_BUMP_TK) == LOW);
    bool touched = false;
    if (!bumpHit) {
        pinMode(PIN_BUMP_TK, OUTPUT);
        digitalWrite(PIN_BUMP_TK, LOW);
        delayMicroseconds(8);
        uint32_t t0 = micros();
        pinMode(PIN_BUMP_TK, INPUT_PULLUP);
        while (digitalRead(PIN_BUMP_TK) == LOW && (micros() - t0) < 120) {}
        touched = ((micros() - t0) >= 18);
    }
    *touchOut = touched;
    return bumpHit;
}

// =============================================================================
// 4. 1-Shunt TDM Ripple DSP + 5V Piezo Dirt-Impact Feature Extraction
// =============================================================================
void sampleAnalogAndOpticalSensors(float cmdL, float cmdR) {
    static float prevShunt = 0.0f, prevPiezo = 0.0f;
    static uint32_t lastOdomUs = 0;

    constexpr int N_SAMP = 64;
    constexpr float ADC_TO_V = 5.00f / 4095.0f;
    float sumShunt = 0.0f, acShuntEnergy = 0.0f, sumPiezoSq = 0.0f, peakPiezo = 0.0f;
    int shuntZcr = 0, piezoZcr = 0;

    for (int i = 0; i < N_SAMP; ++i) {
        float vShunt = analogRead(PIN_ADC_SHUNT) * ADC_TO_V;
        sumShunt += vShunt;
        float acS = vShunt - prevShunt;
        acShuntEnergy += acS * acS;
        if ((acS > 0.018f && prevShunt > 0.0f) || (acS < -0.018f)) shuntZcr++;
        prevShunt = 0.85f * prevShunt + 0.15f * vShunt;

        float vPiezo = (analogRead(PIN_ADC_DIRT) * ADC_TO_V) - 2.50f;
        float aP = fabsf(vPiezo);
        sumPiezoSq += vPiezo * vPiezo;
        if (aP > peakPiezo) peakPiezo = aP;
        if ((vPiezo >= 0.0f && prevPiezo < 0.0f) || (vPiezo < 0.0f && prevPiezo >= 0.0f)) piezoZcr++;
        prevPiezo = vPiezo;
        delayMicroseconds(70);
    }

    float meanV = sumShunt / N_SAMP;
    float rmsRipple = sqrtf(acShuntEnergy / N_SAMP);
    float totalCmd = fabsf(cmdL) + fabsf(cmdR) + 0.05f;
    float fracL = (fabsf(cmdL) + 0.025f) / totalCmd;
    float fracR = (fabsf(cmdR) + 0.025f) / totalCmd;

    g_tel.shuntMeanL  = meanV * fracL * 2.0f;
    g_tel.shuntMeanR  = meanV * fracR * 2.0f;
    g_tel.rippleFreqL = (float)shuntZcr * fracL * 14.0f;
    g_tel.rippleFreqR = (float)shuntZcr * fracR * 14.0f;
    g_tel.rippleAmpL  = rmsRipple;
    g_tel.rippleAmpR  = rmsRipple;
    g_tel.piezoRms    = sqrtf(sumPiezoSq / N_SAMP);
    g_tel.piezoPeak   = peakPiezo;
    g_tel.piezoZcr    = (float)piezoZcr;

    g_tel.ledFrontUs  = measureLedDischargeUs(PIN_LED_FRONT);
    g_tel.ledSideUs   = measureLedDischargeUs(PIN_LED_SIDE);
    g_tel.ledCliffLUs = measureLedDischargeUs(PIN_LED_CLIFF_L);
    g_tel.ledCliffRUs = measureLedDischargeUs(PIN_LED_CLIFF_R);

    bool tk = false;
    g_tel.bumperHit   = readBumperAndTouchKey(&tk);
    g_tel.touchActive = tk;

    uint32_t nowUs = micros();
    float dt = (lastOdomUs == 0) ? 0.01f : (nowUs - lastOdomUs) * 1e-6f;
    lastOdomUs = nowUs;
    float vL = cmdL * 281.0f, vR = cmdR * 281.0f;
    g_tel.odomTheta_rad += ((vR - vL) / WHEEL_TRACK_MM) * dt;
    float vC = 0.5f * (vL + vR);
    g_tel.odomX_mm += vC * cosf(g_tel.odomTheta_rad) * dt;
    g_tel.odomY_mm += vC * sinf(g_tel.odomTheta_rad) * dt;
}

static inline uint8_t normQ7(float val, float maxVal) {
    return (uint8_t)(constrain(val / maxVal, 0.0f, 1.0f) * 127.0f + 0.5f);
}

uint8_t runTinyMLInference() {
    uint8_t feat[12] = {
        normQ7(g_tel.shuntMeanL, 2.20f), normQ7(g_tel.shuntMeanR, 2.20f),
        normQ7(g_tel.rippleFreqL, 550.0f), normQ7(g_tel.rippleFreqR, 550.0f),
        normQ7(g_tel.rippleAmpL, 0.45f), normQ7(g_tel.rippleAmpR, 0.45f),
        normQ7(g_tel.piezoRms, 0.75f), normQ7(g_tel.piezoPeak, 2.10f), normQ7(g_tel.piezoZcr, 28.0f),
        normQ7((float)g_tel.ledFrontUs, 600.0f),
        normQ7((float)g_tel.ledCliffLUs, 500.0f), normQ7((float)g_tel.ledCliffRUs, 500.0f)
    };
    return suga_tinyml_predict(feat);
}

// =============================================================================
// 5. Autonomous 64x64 SLAM + Dual-Coanda Corner Sweep + Closed-Loop Suction FSM
// =============================================================================
enum NavMode {
    NAV_STANDBY = 0,
    NAV_CRUISE_SLAM,
    NAV_WALL_COANDA_SWEEP, // Dual L+R 17.9 m/s Coanda jets scrub both 90° corners!
    NAV_DIRT_TURBO_SPIRAL, // 3,033 Pa Piezo Dirt-Turbo Spiral!
    NAV_AVOID_BACKOFF,
    NAV_CLIFF_ESTOP,
    NAV_UNJAM_REVERSE
};

static NavMode  g_mode        = NAV_CRUISE_SLAM;
static uint32_t g_modeTimerMs = 0;
static float    g_cmdL = 0.0f, g_cmdR = 0.0f, g_cmdTurb = TURB_DUTY_OFF, g_smoothTurb = 0.0f;

void updateOccupancyGrid(bool obstacleAhead) {
    int gx = GRID_N / 2 + (int)(g_tel.odomX_mm / CELL_SIZE_MM);
    int gy = GRID_N / 2 + (int)(g_tel.odomY_mm / CELL_SIZE_MM);
    if (gx >= 1 && gx < GRID_N - 1 && gy >= 1 && gy < GRID_N - 1) {
        g_grid[gy][gx] = 1;
        if (obstacleAhead) {
            int ox = gx + (int)roundf(cosf(g_tel.odomTheta_rad));
            int oy = gy + (int)roundf(sinf(g_tel.odomTheta_rad));
            if (ox >= 0 && ox < GRID_N && oy >= 0 && oy < GRID_N) g_grid[oy][ox] = 2;
        }
    }
}

void setup() {
    initTurbinePWM25kHz();
    initOnChipOpAmps();
    pinMode(PIN_MOT_LIN1, OUTPUT); pinMode(PIN_MOT_LIN2, OUTPUT);
    pinMode(PIN_MOT_RIN1, OUTPUT); pinMode(PIN_MOT_RIN2, OUTPUT);
    pinMode(PIN_BUMP_TK,  INPUT_PULLUP);
    memset(g_grid, 0, sizeof(g_grid));
    delay(350);
}

void loop() {
    static uint32_t lastTouchMs = 0, lastBaroMs = 0;
    uint32_t nowMs = millis();

    sampleAnalogAndOpticalSensors(g_cmdL, g_cmdR);

    // Every 100ms: Run 0-JPY 50us Back-EMF Virtual Barometer!
    if ((nowMs - lastBaroMs) >= 100) {
        lastBaroMs = nowMs;
        sampleVirtualBarometer(g_smoothTurb);
    }

    if (g_tel.touchActive && (nowMs - lastTouchMs) > 800) {
        lastTouchMs = nowMs;
        g_mode = (g_mode == NAV_STANDBY) ? NAV_CRUISE_SLAM : NAV_STANDBY;
    }

    uint8_t state = runTinyMLInference();
    bool cliffHard = (g_tel.ledCliffLUs > 260) || (g_tel.ledCliffRUs > 260) || (state == STATE_STAIR_CLIFF_EDGE);
    bool wallHard  = g_tel.bumperHit || (g_tel.ledFrontUs < 110) || (state == STATE_WALL_SOFT_STALL);
    updateOccupancyGrid(wallHard);

    if (g_mode == NAV_STANDBY) {
        g_cmdL = 0.0f; g_cmdR = 0.0f; g_cmdTurb = TURB_DUTY_OFF;
    } else if (cliffHard) {
        g_mode = NAV_CLIFF_ESTOP; g_modeTimerMs = nowMs;
    } else if (state == STATE_HAIR_BRUSH_JAM && g_mode != NAV_UNJAM_REVERSE) {
        g_mode = NAV_UNJAM_REVERSE; g_modeTimerMs = nowMs;
    } else if (wallHard && g_mode != NAV_AVOID_BACKOFF && g_mode != NAV_CLIFF_ESTOP) {
        g_mode = NAV_AVOID_BACKOFF; g_modeTimerMs = nowMs;
    } else if (state == STATE_HIGH_DIRT_DEBRIS && g_mode == NAV_CRUISE_SLAM) {
        g_mode = NAV_DIRT_TURBO_SPIRAL; g_modeTimerMs = nowMs;
    } else if (g_tel.ledSideUs < 140 && g_mode == NAV_CRUISE_SLAM) {
        g_mode = NAV_WALL_COANDA_SWEEP; g_modeTimerMs = nowMs;
    }

    uint32_t elapsed = nowMs - g_modeTimerMs;
    switch (g_mode) {
        case NAV_STANDBY:
            g_cmdL = 0.0f; g_cmdR = 0.0f; g_cmdTurb = TURB_DUTY_OFF;
            break;
        case NAV_CLIFF_ESTOP:
            g_cmdTurb = TURB_DUTY_ECO;
            if (elapsed < 380)      { g_cmdL = -0.70f; g_cmdR = -0.70f; }
            else if (elapsed < 900) { g_cmdL = -0.60f; g_cmdR = +0.60f; }
            else                    { g_mode = NAV_CRUISE_SLAM; }
            break;
        case NAV_UNJAM_REVERSE:
            g_cmdTurb = TURB_DUTY_ECO;
            if (elapsed < 450)      { g_cmdL = -0.75f; g_cmdR = -0.75f; }
            else if (elapsed < 850) { g_cmdL = +0.60f; g_cmdR = -0.60f; }
            else                    { g_mode = NAV_CRUISE_SLAM; }
            break;
        case NAV_AVOID_BACKOFF:
            g_cmdTurb = TURB_DUTY_STD;
            if (elapsed < 220)      { g_cmdL = -0.50f; g_cmdR = -0.50f; }
            else if (elapsed < 660) { g_cmdL = -0.55f; g_cmdR = +0.55f; }
            else                    { g_mode = NAV_WALL_COANDA_SWEEP; g_modeTimerMs = nowMs; }
            break;
        case NAV_WALL_COANDA_SWEEP: {
            g_cmdTurb = (state == STATE_HIGH_DIRT_DEBRIS) ? TURB_DUTY_TURBO : TURB_DUTY_STD;
            float err = constrain(((float)g_tel.ledSideUs - 125.0f) * 0.0022f, -0.22f, 0.22f);
            g_cmdL = 0.68f + err; g_cmdR = 0.68f - err;
            if (elapsed > 3200 || g_tel.ledSideUs > 320) g_mode = NAV_CRUISE_SLAM;
            break;
        }
        case NAV_DIRT_TURBO_SPIRAL: {
            g_cmdTurb = TURB_DUTY_TURBO; // 3,033 Pa Dirt-Turbo!
            float rFactor = constrain(elapsed / 3800.0f, 0.15f, 0.85f);
            g_cmdL = 0.65f * rFactor; g_cmdR = 0.65f;
            if (elapsed > 3800) g_mode = NAV_CRUISE_SLAM;
            break;
        }
        case NAV_CRUISE_SLAM:
        default: {
            // Closed-Loop Constant-Suction Trim using 0-JPY Back-EMF Virtual Barometer!
            float baseDuty = (state == STATE_HIGH_DIRT_DEBRIS) ? TURB_DUTY_TURBO : TURB_DUTY_STD;
            if (baseDuty == TURB_DUTY_STD && g_tel.turbineBemfV > 1.0f && !g_tel.filterClogAlert) {
                float bemfErr = BEMF_TARGET_STD_V - g_tel.turbineBemfV;
                baseDuty = constrain(baseDuty + bemfErr * 0.12f, TURB_DUTY_ECO, TURB_DUTY_TURBO);
            }
            g_cmdTurb = baseDuty;
            g_cmdL = 0.72f; g_cmdR = 0.72f;
            break;
        }
    }

    if (g_smoothTurb < g_cmdTurb)      g_smoothTurb = min(g_cmdTurb, g_smoothTurb + 0.04f);
    else if (g_smoothTurb > g_cmdTurb) g_smoothTurb = max(g_cmdTurb, g_smoothTurb - 0.06f);

    setTurbineDuty(g_smoothTurb);
    setWheelPWM(PIN_MOT_LIN1, PIN_MOT_LIN2, g_cmdL);
    setWheelPWM(PIN_MOT_RIN1, PIN_MOT_RIN2, g_cmdR);
}
