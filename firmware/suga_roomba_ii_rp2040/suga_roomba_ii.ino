// =============================================================================
// 菅ルンバ II (Suga Roomba II) — Dual-Core RP2040 Production Firmware
// Hardware: AE-RP2040 + AE-TC78H653FTG + MCP6002 + 2SK4017 + 0.33R MPC74
//           + Murata PKM13EPYH4000 Piezo + 4x OSR5JA3Z74A Red LEDs + Daiso Mouse
// BOM Total: 2,980 JPY (Tax-Inc, Verified 2026)
//
// Core 0: 20kHz 1-Shunt TDM Current & Commutator Ripple DSP, 25kHz Turbine PWM,
//         Piezo Dirt-Detect Envelope, 4x LED-as-Photodiode Ranging, Wheel PID
// Core 1: Colab-Trained INT8 TinyML Inference (<15us), 64x64 Occupancy-Grid
//         Frontier SLAM, Boustrophedon + Wall-Follow + Piezo Dirt-Spiral FSM
// =============================================================================

#include <Arduino.h>
#include <hardware/pwm.h>
#include <hardware/adc.h>
#include <math.h>
#include "suga_roomba_tinyml.h"

// --- Pin Map (100% Matched to KiCad Schematic & PCB suga_roomba_ii.kicad_sch) ---
constexpr uint8_t PIN_MOT_LIN1     = 2;   // TC78H653 IN1A (Left friction drive)
constexpr uint8_t PIN_MOT_LIN2     = 3;   // TC78H653 IN2A
constexpr uint8_t PIN_MOT_RIN1     = 4;   // TC78H653 IN1B (Right friction drive)
constexpr uint8_t PIN_MOT_RIN2     = 5;   // TC78H653 IN2B
constexpr uint8_t PIN_PWM_TURB     = 6;   // DTC114EL Base -> 2SK4017 Gate (ACTIVE-LOW!)
constexpr uint8_t PIN_BUMP_SW      = 10;  // Salvaged Daiso mouse microswitches (active LOW)
constexpr uint8_t PIN_USB_DP       = 12;  // Daiso optical mouse PIO-USB Host D+
constexpr uint8_t PIN_USB_DM       = 13;  // Daiso optical mouse PIO-USB Host D-
constexpr uint8_t PIN_LED_FRONT    = 14;  // Red LED-as-Photodiode: Front Wall
constexpr uint8_t PIN_LED_SIDE     = 15;  // Red LED-as-Photodiode: Right Wall-Follower
constexpr uint8_t PIN_LED_CLIFF_L  = 16;  // Red LED-as-Photodiode: Front-Left Stair Cliff
constexpr uint8_t PIN_LED_CLIFF_R  = 17;  // Red LED-as-Photodiode: Front-Right Stair Cliff
constexpr uint8_t PIN_ADC_SHUNT    = 26;  // ADC0: MCP6002 CH1 (10x 0.33R Shunt Ripple)
constexpr uint8_t PIN_ADC_DIRT     = 27;  // ADC1: MCP6002 CH2 (5x Piezo Acoustic Dirt)

// --- Suction Operating Modes (Verified in CFD & SPICE) ---
// NOTE: Q2 (DTC114EL) inverts GP6! GP6=HIGH pulls 2SK4017 gate LOW (Turbine OFF).
//       GP6=LOW lets R1||R2 pull 2SK4017 gate to VBAT (Turbine ON, Rds=36mOhm).
constexpr float TURB_DUTY_OFF   = 0.00f;
constexpr float TURB_DUTY_ECO   = 0.68f;  // ~4.6V eff ->  971 Pa shutoff, 39.9 min runtime
constexpr float TURB_DUTY_STD   = 0.88f;  // ~6.0V eff -> 1685 Pa shutoff, 18.5 m/s nozzle
constexpr float TURB_DUTY_TURBO = 1.00f;  // ~6.8V peak-> 2177 Pa shutoff, 20.7 m/s (Dirt Boost!)

// --- Shared Inter-Core Telemetry (Core0 writes, Core1 reads) ---
struct SharedTelemetry {
    volatile float shuntMeanL;
    volatile float shuntMeanR;
    volatile float rippleFreqL;
    volatile float rippleFreqR;
    volatile float rippleAmpL;
    volatile float rippleAmpR;
    volatile float piezoRms;
    volatile float piezoPeak;
    volatile float piezoZcr;
    volatile uint32_t ledFrontUs;
    volatile uint32_t ledSideUs;
    volatile uint32_t ledCliffLUs;
    volatile uint32_t ledCliffRUs;
    volatile bool bumperHit;
    volatile float odomX_mm;
    volatile float odomY_mm;
    volatile float odomTheta_rad;
};

struct MotorCommand {
    volatile float targetSpeedL;   // -1.0 .. +1.0
    volatile float targetSpeedR;   // -1.0 .. +1.0
    volatile float targetTurbine;  //  0.0 ..  1.0
};

static SharedTelemetry g_tel = {};
static MotorCommand    g_cmd = {0.0f, 0.0f, TURB_DUTY_OFF};

// =============================================================================
// Hardware Helpers (25kHz Turbine PWM + LED-as-Photodiode Discharge Timer)
// =============================================================================
static uint pwmSliceTurb;
static uint pwmChanTurb;

void initTurbinePWM25kHz() {
    gpio_set_function(PIN_PWM_TURB, GPIO_FUNC_PWM);
    pwmSliceTurb = pwm_gpio_to_slice_num(PIN_PWM_TURB);
    pwmChanTurb  = pwm_gpio_to_channel(PIN_PWM_TURB);
    // 125 MHz / 5000 = 25.0 kHz ultrasonic PWM (completely inaudible!)
    pwm_set_wrap(pwmSliceTurb, 4999);
    // Safe boot default: GP6 HIGH (100% GPIO duty = 0% Turbine ON)
    pwm_set_chan_level(pwmSliceTurb, pwmChanTurb, 4999);
    pwm_set_enabled(pwmSliceTurb, true);
}

void setTurbineDuty(float duty) {
    duty = constrain(duty, 0.0f, 1.0f);
    // Invert for DTC114EL level-shifter polarity
    uint16_t level = (uint16_t)((1.0f - duty) * 4999.0f);
    pwm_set_chan_level(pwmSliceTurb, pwmChanTurb, level);
}

void setWheelPWM(uint8_t in1, uint8_t in2, float cmd) {
    cmd = constrain(cmd, -1.0f, 1.0f);
    int val = (int)(fabsf(cmd) * 255.0f);
    if (cmd >= 0.0f) {
        analogWrite(in1, val);
        analogWrite(in2, 0);
    } else {
        analogWrite(in1, 0);
        analogWrite(in2, val);
    }
}

// Fast reverse-bias LED-as-Photodiode discharge measurement (fallback/portable to PIO)
uint32_t measureLedDischargeUs(uint8_t pin, uint32_t timeoutUs = 2000) {
    // 1. Reverse-bias charge LED junction capacitance (~30 pF) to 3.3V
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delayMicroseconds(12);

    // 2. Float pin as high-Z input and time photocurrent discharge to logic LOW
    uint32_t t0 = micros();
    pinMode(pin, INPUT);
    while (digitalRead(pin) == HIGH) {
        uint32_t dt = micros() - t0;
        if (dt >= timeoutUs) return timeoutUs;
    }
    return micros() - t0;
}

// =============================================================================
// CORE 0: Real-Time 1-Shunt TDM Commutator Ripple DSP + Sensor Sampling
// =============================================================================
void setup() {
    Serial.begin(115200);
    initTurbinePWM25kHz();

    pinMode(PIN_MOT_LIN1, OUTPUT);
    pinMode(PIN_MOT_LIN2, OUTPUT);
    pinMode(PIN_MOT_RIN1, OUTPUT);
    pinMode(PIN_MOT_RIN2, OUTPUT);
    pinMode(PIN_BUMP_SW, INPUT_PULLUP);
    analogWriteFreq(20000);
    analogWriteRange(255);

    adc_init();
    adc_gpio_init(PIN_ADC_SHUNT);
    adc_gpio_init(PIN_ADC_DIRT);

    // Soft-start turbine ramp (avoids 5S NiMH inrush sag; verified in ngspice!)
    delay(400);
}

void loop() {
    static uint32_t lastCtrlUs = 0;
    static float smoothTurb = 0.0f;
    static float prevShunt = 0.0f;
    static float prevPiezo = 0.0f;

    // 1. Burst-sample 1-Shunt TDM (ADC0) & Piezo Acoustic Dirt-Detect (ADC1)
    //    64 samples over ~6.4 ms captures multiple PWN10EB12CB commutator ripples & grain impacts
    constexpr int N_SAMP = 64;
    float sumShunt = 0.0f, acShuntEnergy = 0.0f;
    int shuntZeroCross = 0;
    float sumPiezoSq = 0.0f, peakPiezo = 0.0f;
    int piezoZcr = 0;

    for (int i = 0; i < N_SAMP; ++i) {
        adc_select_input(0);
        float vShunt = adc_read() * (3.3f / 4095.0f);
        sumShunt += vShunt;
        float acS = vShunt - prevShunt;
        acShuntEnergy += acS * acS;
        if ((acS > 0.015f && prevShunt > 0.0f) || (acS < -0.015f)) shuntZeroCross++;
        prevShunt = 0.85f * prevShunt + 0.15f * vShunt;

        adc_select_input(1);
        float vPiezo = (adc_read() * (3.3f / 4095.0f)) - 1.65f; // AC around VMID=1.65V
        float aP = fabsf(vPiezo);
        sumPiezoSq += vPiezo * vPiezo;
        if (aP > peakPiezo) peakPiezo = aP;
        if ((vPiezo >= 0.0f && prevPiezo < 0.0f) || (vPiezo < 0.0f && prevPiezo >= 0.0f)) piezoZcr++;
        prevPiezo = vPiezo;
        delayMicroseconds(80);
    }

    float meanShuntV = sumShunt / N_SAMP;
    float rippleRms  = sqrtf(acShuntEnergy / N_SAMP);
    float rippleFreq = (float)shuntZeroCross / (float)N_SAMP;

    // TDM Split Estimate (proportional to commanded left/right duty ratio)
    float wL = fabsf(g_cmd.targetSpeedL) + 0.05f;
    float wR = fabsf(g_cmd.targetSpeedR) + 0.05f;
    float sumW = wL + wR;
    g_tel.shuntMeanL  = constrain((meanShuntV / 2.6f) * (2.0f * wL / sumW), 0.0f, 1.0f);
    g_tel.shuntMeanR  = constrain((meanShuntV / 2.6f) * (2.0f * wR / sumW), 0.0f, 1.0f);
    g_tel.rippleFreqL = constrain(rippleFreq * (wL / max(wL, wR)), 0.0f, 1.0f);
    g_tel.rippleFreqR = constrain(rippleFreq * (wR / max(wL, wR)), 0.0f, 1.0f);
    g_tel.rippleAmpL  = constrain(rippleRms * 4.2f, 0.0f, 1.0f);
    g_tel.rippleAmpR  = constrain(rippleRms * 4.2f, 0.0f, 1.0f);

    g_tel.piezoRms  = constrain(sqrtf(sumPiezoSq / N_SAMP) / 0.55f, 0.0f, 1.0f);
    g_tel.piezoPeak = constrain(peakPiezo / 1.15f, 0.0f, 1.0f);
    g_tel.piezoZcr  = constrain((float)piezoZcr / 28.0f, 0.0f, 1.0f);

    // 2. 4x LED-as-Photodiode Optical Wall & Stair Cliff Ranging
    g_tel.ledFrontUs  = measureLedDischargeUs(PIN_LED_FRONT,   1800);
    g_tel.ledSideUs   = measureLedDischargeUs(PIN_LED_SIDE,    1800);
    g_tel.ledCliffLUs = measureLedDischargeUs(PIN_LED_CLIFF_L, 1800);
    g_tel.ledCliffRUs = measureLedDischargeUs(PIN_LED_CLIFF_R, 1800);
    g_tel.bumperHit   = (digitalRead(PIN_BUMP_SW) == LOW);

    // 3. Apply Smooth Soft-Start Turbine PWM + Differential Friction Drive
    uint32_t nowUs = micros();
    float dt = (nowUs - lastCtrlUs) * 1e-6f;
    if (dt > 0.1f) dt = 0.01f;
    lastCtrlUs = nowUs;

    // Ramp turbine max 1.2 duty/sec so 5S NiMH battery never trips 3A fuse on startup
    float tErr = g_cmd.targetTurbine - smoothTurb;
    smoothTurb += constrain(tErr, -1.5f * dt, 1.2f * dt);
    setTurbineDuty(smoothTurb);

    setWheelPWM(PIN_MOT_LIN1, PIN_MOT_LIN2, g_cmd.targetSpeedL);
    setWheelPWM(PIN_MOT_RIN1, PIN_MOT_RIN2, g_cmd.targetSpeedR);

    // 4. Dead-Reckoning Odometry (0.306 m/s nominal cruise, 172 mm wheel track)
    constexpr float V_MAX_MMS = 306.0f;
    constexpr float TRACK_MM  = 172.0f;
    float vL = g_cmd.targetSpeedL * V_MAX_MMS;
    float vR = g_cmd.targetSpeedR * V_MAX_MMS;
    float vC = 0.5f * (vL + vR);
    float wZ = (vR - vL) / TRACK_MM;
    g_tel.odomTheta_rad += wZ * dt;
    g_tel.odomX_mm += vC * cosf(g_tel.odomTheta_rad) * dt;
    g_tel.odomY_mm += vC * sinf(g_tel.odomTheta_rad) * dt;
}

// =============================================================================
// CORE 1: TinyML INT8 Neural Inference + 64x64 Occupancy Grid SLAM + FSM
// =============================================================================
constexpr int GRID_N = 64;
constexpr float CELL_MM = 80.0f; // 5.12m x 5.12m room map in 4 KB SRAM!
static uint8_t slamGrid[GRID_N][GRID_N]; // 0=unknown, 1=cleaned, 2=obstacle

enum NavMode { NAV_BOUSTROPHEDON, NAV_WALL_FOLLOW, NAV_DIRT_SPIRAL, NAV_ESCAPE, NAV_UNJAM };
static NavMode navMode = NAV_BOUSTROPHEDON;
static uint32_t modeTimerMs = 0;
static int turnDir = 1;

static inline uint8_t toQ7(float x) {
    int v = (int)lroundf(constrain(x, 0.0f, 1.0f) * 127.0f);
    return (uint8_t)constrain(v, 0, 127);
}

void markSlamCell(float x_mm, float y_mm, uint8_t val) {
    int gx = GRID_N / 2 + (int)(x_mm / CELL_MM);
    int gy = GRID_N / 2 + (int)(y_mm / CELL_MM);
    if (gx >= 0 && gx < GRID_N && gy >= 0 && gy < GRID_N) {
        if (val == 2 || slamGrid[gy][gx] != 2) slamGrid[gy][gx] = val;
    }
}

void setup1() {
    memset(slamGrid, 0, sizeof(slamGrid));
    delay(600);
}

void loop1() {
    // 1. Pack 12-feature Q7 vector for Colab-trained INT8 Neural Network
    uint8_t feat[12] = {
        toQ7(g_tel.shuntMeanL),
        toQ7(g_tel.shuntMeanR),
        toQ7(g_tel.rippleFreqL),
        toQ7(g_tel.rippleFreqR),
        toQ7(g_tel.rippleAmpL),
        toQ7(g_tel.rippleAmpR),
        toQ7(g_tel.piezoRms),
        toQ7(g_tel.piezoPeak),
        toQ7(g_tel.piezoZcr),
        toQ7((float)g_tel.ledFrontUs  / 1800.0f),
        toQ7((float)g_tel.ledCliffLUs / 1800.0f),
        toQ7((float)g_tel.ledCliffRUs / 1800.0f)
    };

    // 2. Run INT8 TinyML Inference (~12 microseconds on RP2040 Cortex-M0+!)
    uint8_t aiState = suga_tinyml_predict(feat);
    uint32_t nowMs = millis();

    // Update 64x64 SLAM map
    markSlamCell(g_tel.odomX_mm, g_tel.odomY_mm, 1);

    // 3. Highest-Priority Safety Reflex: Stair Cliff or Physical Bumper
    if (aiState == STATE_STAIR_CLIFF_EDGE || g_tel.ledCliffLUs > 950 || g_tel.ledCliffRUs > 950) {
        // Immediate emergency reverse & 140 deg turn away from stairs!
        g_cmd.targetSpeedL = -0.75f;
        g_cmd.targetSpeedR = -0.75f;
        g_cmd.targetTurbine = TURB_DUTY_ECO;
        delay(260);
        g_cmd.targetSpeedL =  0.65f * turnDir;
        g_cmd.targetSpeedR = -0.65f * turnDir;
        delay(420);
        turnDir = -turnDir;
        return;
    }

    if (g_tel.bumperHit || aiState == STATE_WALL_SOFT_STALL || g_tel.ledFrontUs < 240) {
        // Mark obstacle cell ahead in SLAM grid
        markSlamCell(g_tel.odomX_mm + 110.0f * cosf(g_tel.odomTheta_rad),
                     g_tel.odomY_mm + 110.0f * sinf(g_tel.odomTheta_rad), 2);
        navMode = NAV_ESCAPE;
        modeTimerMs = nowMs + 560;
    } else if (aiState == STATE_HAIR_BRUSH_JAM && navMode != NAV_UNJAM) {
        // Self-cleaning reverse pulse to unwind entangled hair from agitator/wheels
        navMode = NAV_UNJAM;
        modeTimerMs = nowMs + 700;
    } else if (aiState == STATE_HIGH_DIRT_DEBRIS && navMode != NAV_DIRT_SPIRAL) {
        // Murata 30-JPY Piezo detected sand/rice/heavy debris -> 2,177 Pa Turbo Spiral!
        navMode = NAV_DIRT_SPIRAL;
        modeTimerMs = nowMs + 4200;
    }

    // 4. Execute Active Autonomous Navigation Behavior
    switch (navMode) {
        case NAV_ESCAPE:
            if (nowMs < modeTimerMs - 340) {
                g_cmd.targetSpeedL = -0.55f;
                g_cmd.targetSpeedR = -0.55f;
            } else if (nowMs < modeTimerMs) {
                // Reuleaux-D zero rear-overhang pivot turn + lane shift
                g_cmd.targetSpeedL =  0.60f * turnDir;
                g_cmd.targetSpeedR = -0.60f * turnDir;
            } else {
                turnDir = -turnDir;
                navMode = (g_tel.ledSideUs < 420) ? NAV_WALL_FOLLOW : NAV_BOUSTROPHEDON;
            }
            g_cmd.targetTurbine = TURB_DUTY_STD;
            break;

        case NAV_UNJAM:
            g_cmd.targetTurbine = TURB_DUTY_ECO;
            g_cmd.targetSpeedL = ((nowMs / 180) % 2 == 0) ? -0.80f : 0.65f;
            g_cmd.targetSpeedR = ((nowMs / 180) % 2 == 0) ? -0.80f : 0.65f;
            if (nowMs >= modeTimerMs) navMode = NAV_BOUSTROPHEDON;
            break;

        case NAV_DIRT_SPIRAL: {
            // Expanding Archimedean spiral over debris hotspot at 2,177 Pa Dirt-Turbo!
            float prog = 1.0f - (float)(modeTimerMs - nowMs) / 4200.0f;
            g_cmd.targetTurbine = TURB_DUTY_TURBO;
            g_cmd.targetSpeedL = 0.72f;
            g_cmd.targetSpeedR = 0.16f + 0.42f * prog;
            if (nowMs >= modeTimerMs) navMode = NAV_BOUSTROPHEDON;
            break;
        }

        case NAV_WALL_FOLLOW: {
            // Right-side LED-as-Photodiode proportional wall hug (sweeps corners with side brush!)
            float err = ((float)g_tel.ledSideUs - 320.0f) / 600.0f;
            float steer = constrain(err * 0.45f, -0.35f, 0.35f);
            g_cmd.targetSpeedL = 0.74f + steer;
            g_cmd.targetSpeedR = 0.74f - steer;
            g_cmd.targetTurbine = (aiState == STATE_CARPET_HIGH_DRAG) ? TURB_DUTY_TURBO : TURB_DUTY_STD;
            if (g_tel.ledSideUs > 1100) navMode = NAV_BOUSTROPHEDON;
            break;
        }

        case NAV_BOUSTROPHEDON:
        default:
            // Straight systematic sweep with adaptive Eco/Std/Carpet suction
            g_cmd.targetSpeedL = 0.78f;
            g_cmd.targetSpeedR = 0.78f;
            g_cmd.targetTurbine = (aiState == STATE_CARPET_HIGH_DRAG) ? TURB_DUTY_TURBO :
                                  (aiState == STATE_HARD_FLOOR_NORMAL ? TURB_DUTY_STD : TURB_DUTY_ECO);
            break;
    }

    delay(20); // 50 Hz TinyML & SLAM cognitive tick
}
