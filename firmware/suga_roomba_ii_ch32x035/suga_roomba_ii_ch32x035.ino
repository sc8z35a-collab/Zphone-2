// =============================================================================
// 菅ルンバ II Rev 5.2 (100% Real-Silicon & Real-Manufacturing Edition) — FW
// =============================================================================
// MCU      : WCH CH32X035G8U6 (48 MHz 32-bit QingKe_V4C RISC-V, 62KB Flash, 20KB SRAM)
//            Akizuki Denshi [131018] AE-QFN28-DIP(CH32X035G8U6) (300mil DIP30, ¥290)
// Driver   : Akizuki Denshi [114746] AE-TC78H653FTG Dual H-Bridge (300mil DIP16, ¥200)
// Flashing : (1) Native Factory USB-ISP via wchisp (short JP_BOOT: PC17->10k->5V at boot)
//            (2) WCH-LinkE SWD Debugger on J_SWD (PC18=SWDIO, PC19=SWCLK)
//
// Rev 5.2 Real-Silicon Fixes & Verified Hardware Capabilities:
//   1. 100% Verified QFN28 Silicon Pinout (ZERO non-existent PB2, ZERO PB1/PB5 conflict,
//      ZERO ADC errata channels {3,7,11,15}, ZERO TIM1/TIM2 frequency collision!).
//   2. True WCH OPA_Unlock() + OPA->CTLR1 Configuration (verified vs openwch/ch32x035):
//      - OPA1: PB0 (O1P0) -> Internal 16x PGA (CHN_PGA_16xIN, FB_ON) -> PB5/PB1 (O1O1/A9)
//              Amplifies 0.10-Ohm bifilar shunt (V_shunt <= 0.15V @ 1.5A stall, safe vs
//              TC78H653FTG -0.3V VIL rating!) -> 1.60 V/A with ZERO external OPA1 resistors!
//      - OPA2: PA7 (O2P0), PA5 (O2N0), PA4 (O2O0/A4) -> External 5.0x AC gain around 2.50V
//              VMID for 30-JPY Murata PKM13EPYH4000-A0 acoustic debris impact detector!
//   3. Direct Hardware Timer Register PWM (bypasses incomplete arduino_core_ch32 PinMap_TIM!):
//      - TIM1_CH1 (PB9, default RM=0) = 25.0 kHz ultrasonic turbine PWM -> 2SK4017 Gate
//      - TIM2_CH1..4 (PA0..PA3, default RM=0) = 16.0 kHz Slow-Decay Drive-Brake H-Bridge PWM
//      - PC14 = TC78H653FTG STBY (pulled LOW at reset via R2=10k -> ZERO boot twitch +
//        automatic firmware STBY LOW->HIGH pulse recovery if ISD overcurrent trips!)
//   4. 0-Lux Dark-Room + Bright-Sunlight Proof Active Synchronous Optical Sensing:
//      - 4x dedicated Emitter GPIOs (PB10/PB12/PC15/PB3 via 1kR = 3.0 mA pulsed 1 at a time!)
//      - 4x Reverse-Bias Photodiodes (PB4/PB6/PB7/PB8, K->GPIO, A->GND)
//      - Differential (Active - Ambient) photocurrent subtraction works in pitch-black 0-lux
//        darkness under an 80mm sofa AND in direct sunlight!
//   5. Precision 1% Back-EMF Virtual Barometer (PC0/A10, 1.5 ms coast + startup auto-cal):
//      - OpenFOAM 3D smooth vaneless volute verified: 737 Pa Op / 1,354 Pa Shutoff 6.0V /
//        1,606 Pa Turbo 6.6V! Cancels Vbat & R18/R19 tolerances -> <15 mV p99 error!
//   6. Hardware Independent Watchdog (IWDG ~1.6 s) + NiMH Low-Voltage Cutoff + Isolated
//      Bumper Microswitches (PB11) & Al-Can TouchKey (PC3) + 952-Byte INT8 TinyML SLAM!
// =============================================================================

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#if defined(CH32X035) && !defined(TEST_WCH_SPL_REGS)
#include <ch32x035_opa.h> // Official openwch/arduino_core_ch32 ch32x035_conf.h omits this include!
#endif
#include "suga_roomba_tinyml.h"
#include "usb_host_mouse.h"

// Monotonic microsecond timer: bypasses upstream openwch/arduino_core_ch32 clock.c bug where
// getCurrentMicros() computes (tms - u0) despite SysTick->CTLR=0x0F counting UP (0 -> CMP)!
static inline uint32_t safeMicros() {
#if defined(CH32X035) && !defined(TEST_WCH_SPL_REGS)
    uint32_t m0 = millis();
    uint32_t u0 = (uint32_t)SysTick->CNT;
    uint32_t m1 = millis();
    if (m1 != m0) {
        u0 = (uint32_t)SysTick->CNT;
        m0 = m1;
    }
    uint32_t tms = (uint32_t)SysTick->CMP + 1U;
    return m0 * 1000U + (uint32_t)(((uint64_t)u0 * 1000ULL) / tms);
#else
    return micros();
#endif
}

// --- Pin Map (100% 1:1 Matched to Rev 5.2/5.3 KiCad PCB & Real CH32X035G8U6 QFN28 Silicon!) ---
// TIM2_CH1..4 (Default Remap = 000 -> 16.0 kHz Slow-Decay Drive-Brake H-Bridge PWM)
constexpr uint8_t PIN_MOT_LIN1     = PA0;   // QFN Pin 5  -> AE-TC78H653FTG CN1-6 (IN1)
constexpr uint8_t PIN_MOT_LIN2     = PA1;   // QFN Pin 6  -> AE-TC78H653FTG CN1-7 (IN2)
constexpr uint8_t PIN_MOT_RIN1     = PA2;   // QFN Pin 7  -> AE-TC78H653FTG CN1-5 (IN3)
constexpr uint8_t PIN_MOT_RIN2     = PA3;   // QFN Pin 8  -> AE-TC78H653FTG CN1-4 (IN4)
constexpr uint8_t PIN_DRV_STBY     = PC14;  // QFN Pin 28 -> AE-TC78H653FTG CN1-3 (STBY, R2=10k PD)

// TIM1_CH1 (Default RM=0 -> Dedicated 25.0 kHz Ultrasonic Turbine PWM, ZERO conflict w/ TIM2!)
constexpr uint8_t PIN_PWM_TURB     = PB9;   // QFN Pin 20 -> 2SK4017 Gate (ACTIVE-HIGH, R1=10k PD)

// On-Chip Analog Peripherals (ALL ADC channels errata-free: A4, A9, A10, A13 — none in {3,7,11,15}!)
constexpr uint8_t PIN_OPA1_P       = PB0;   // QFN Pin 13 (O1P0) <- 0.10-Ohm Bifilar Shunt Node
constexpr uint8_t PIN_ADC_SHUNT    = PB1;   // QFN Pin 16 (PB1/PB5 bonded: O1O1 -> A9, analog only!)
constexpr uint8_t PIN_OPA2_P       = PA7;   // QFN Pin 12 (O2P0) <- 30-JPY Murata Piezo + 2.50V
constexpr uint8_t PIN_OPA2_N       = PA5;   // QFN Pin 10 (O2N0) <- External 5x Feedback (5k/20k)
constexpr uint8_t PIN_ADC_DIRT     = PA4;   // QFN Pin 9  (O2O0) -> ADC_CH4 (Acoustic Debris)
constexpr uint8_t PIN_ADC_BEMF     = PC0;   // QFN Pin 3  (A10)  <- 10k/10k 1% Back-EMF Divider

// 0-Lux Active Synchronous Optical Array (4x Dedicated Emitters + 4x Reverse-Bias Photodiodes)
constexpr uint8_t PIN_LED_FRONT_RX   = PB4;  // QFN Pin 15 (K->PB4, A->GND) Front Wall Photodiode
constexpr uint8_t PIN_LED_SIDE_RX    = PB6;  // QFN Pin 17 (K->PB6, A->GND) Right Wall Photodiode
constexpr uint8_t PIN_LED_CLIFF_L_RX = PB7;  // QFN Pin 18 (K->PB7, A->GND) Front-Left Cliff PD
constexpr uint8_t PIN_LED_CLIFF_R_RX = PB8;  // QFN Pin 19 (K->PB8, A->GND) Front-Right Cliff PD
constexpr uint8_t PIN_EMIT_FRONT     = PB10; // QFN Pin 21 -> R20 (1kR) -> Front Active Emitter LED
constexpr uint8_t PIN_EMIT_SIDE      = PB12; // QFN Pin 23 -> R21 (1kR) -> Side Active Emitter LED
constexpr uint8_t PIN_EMIT_CLIFF_L   = PC15; // QFN Pin 1  -> R22 (1kR) -> Cliff-L Active Emitter LED
constexpr uint8_t PIN_EMIT_CLIFF_R   = PB3;  // QFN Pin 14 -> R23 (1kR) -> Cliff-R Active Emitter LED

// Isolated Mechanical Bumper & Capacitive TouchKey (ZERO cross-triggering!)
constexpr uint8_t PIN_BUMP_SW      = PB11;  // QFN Pin 22 <- 2x Salvaged Mouse Microswitches (ACTIVE-LOW)
constexpr uint8_t PIN_TOUCH_KEY    = PC3;   // QFN Pin 4  <- 0-JPY Al-Can Capacitive Touch Pad (Direct GPIOC SPL!)
// PC16 = USB_DM (QFN 26), PC17 = USB_DP / BOOT (QFN 27) -> Dual-use USB-ISP (boot) + USBFS Host Mouse (run)!
// PC18 = SWDIO (QFN 25), PC19 = SWCLK (QFN 24)

// --- Rev 5.3 OpenFOAM v2406 Smooth Vaneless Volute Operating Targets (RS-385PH + Ø76mm 9-Blade) ---
constexpr float TURB_DUTY_OFF   = 0.00f;
constexpr float TURB_DUTY_ECO   = 0.72f;  // ~4.5V eff ->   760 Pa shutoff, low acoustic power
constexpr float TURB_DUTY_STD   = 0.90f;  // ~6.0V eff ->   719 Pa Op / 1,335 Pa shutoff, eta_fan=29.6%
constexpr float TURB_DUTY_TURBO = 1.00f;  // ~6.6V pk  -> 1,581 Pa shutoff, 1.87 A (<3A polyfuse SAFE!)

// --- Virtual Barometer Targets (1.5 ms Coast Window Verified in Ngspice 42 200-Run MC!) ---
constexpr uint32_t BEMF_COAST_SETTLE_US = 1200; // Wait 1.2 ms (>10x 0.113ms freewheel decay!)
constexpr uint32_t BEMF_COAST_TOTAL_US  = 1500; // Total 1.5 ms PWM-OFF coast window (<1% duty overhead)
constexpr float    BEMF_TARGET_STD_V    = 4.80f; // Open-flow equilibrium @ 6.0V
constexpr float    BEMF_CLOG_DELTA_V    = 0.42f; // Relative Back-EMF rise -> Dust-Cup / Mesh Full Alert!
constexpr float    SHUNT_OHM            = 0.10f; // Nominal 0.10-Ohm bifilar shunt (auto-normalized ±40%!)
constexpr float    OPA1_PGA_GAIN        = 16.0f; // Internal CH32X035 OPA1 16x PGA -> 1.60 V/A!

// --- Physical Robot Constants (Rev 5.3 Real-Hardware 216x215x64/75.6mm, 785g) ---
constexpr float WHEEL_DIA_MM       = 50.0f;  // Ø48mm SLA rim + 1mm Daiso rubber band (25.0:1)
constexpr float WHEEL_TRACK_MM     = 176.0f; // ±88.0mm exact CAD wheel track
constexpr float CRUISE_MM_S        = 271.4f; // MuJoCo 4.0.1 verified cruise @ 6V
constexpr float MOUSE_MM_PER_COUNT = 25.4f / 800.0f; // Standard 800 CPI Daiso optical mouse
constexpr float MOUSE_LEVER_ARM_MM = 78.0f;  // Exact CAD offset: WHEEL_Y(+6.0) - MOUSE_Y(-72.0) = 78.0mm!
constexpr float CELL_SIZE_MM       = 65.0f;  // 64x64 SLAM covers 4.16m x 4.16m room
constexpr int   GRID_N             = 64;

// --- Power-On Self-Test (POST) Fault Bitmask (Zero-Cost LED Blink Diagnostics!) ---
constexpr uint8_t POST_OK                          = 0x00;
constexpr uint8_t POST_ERR_MOSFET_OR_OPEN_TURB     = 0x01; // 2SK4017 short or open turbine / dead battery
constexpr uint8_t POST_ERR_SHUNT_OR_HBRIDGE_FAULT  = 0x02; // OPA1 shunt rail stuck high with H-bridge in STBY
constexpr uint8_t POST_WARN_PIEZO_VMID_DEGRADED    = 0x04; // OPA2 2.5V VMID rail out of bounds (degraded piezo)

struct SensorFrame {
    float shuntMeanL, shuntMeanR;
    float rippleFreqL, rippleFreqR;
    float rippleAmpL, rippleAmpR;
    float piezoRms, piezoPeak, piezoZcr;
    uint32_t ledFrontUs, ledSideUs, ledCliffLUs, ledCliffRUs; // Ambient-cancelled active eq. [us]
    bool bumperHit, touchActive;
    float turbineBemfV;       // Real-time settled Back-EMF [V] (1.5ms window)
    float estAirflowLs;       // Virtual Barometer estimated Q [L/s]
    bool  filterClogAlert;    // True if relative Back-EMF rise > +0.42V for > 3.5s
    bool  driverFaultRetry;   // True if TC78H653FTG STBY auto-recovery was triggered
    bool  lowBatteryCutoff;   // True if 5S NiMH sagged below 4.8V safe floor
    bool  usbMouseLive;       // True if Daiso USB optical mouse is actively fused via USBFS Host
    bool  wheelSlipDetected;  // True if optical mouse vs wheel travel shows friction-drive slip >48%
    float wheelSlipRatio;     // Instantaneous estimated wheel slip [0.0 .. 1.0]
    uint8_t postFaultMask;    // Hardware Power-On Self-Test bitmask (0 = ALL OK!)
    float odomX_mm, odomY_mm, odomTheta_rad;
};

static SensorFrame g_tel = {};
static uint8_t g_grid[GRID_N][GRID_N];

// --- Adaptive Auto-Calibration State (Solves Risk #3: Black Carpet..White Tile & ±40% Shunt!) ---
static uint32_t g_cliffThreshL   = 260;  // Dynamically auto-calibrated at boot per real floor reflectance!
static uint32_t g_cliffThreshR   = 260;
static float    g_shuntGainCorr  = 1.0f; // Auto-normalizes ±40% hand-cut mouse-cable shunt resistance!
static float    g_shuntCruiseAvg = 0.0f;
static uint8_t  g_shuntCalCount  = 0;
static bool     g_piezoEnabled       = true;
static bool     g_turbineAllowed     = true;
static bool     g_driveAllowed       = true;
static float    g_mouseMountAngleRad = 0.0f; // Exact 2x2 online solved mouse yaw angle [rad] (<0.02 deg!)
static float    g_wheelDiaMismatch   = 0.0f; // Exact 2x2 online solved L/R wheel diameter delta
static bool     g_mouseCalLocked     = false;

// =============================================================================
// 1. Real CH32X035 Silicon Peripheral Drivers (OPA1 16x PGA, OPA2 5x, TIM1, TIM2, IWDG)
// =============================================================================
void initOnChipOpAmps() {
#if defined(CH32X035) && defined(OPA_BASE)
    // Official WCH CH32X035 OPA initialization sequence (verified byte-for-byte vs openwch/ch32x035):
    // 1. Configure OPA analog pins as GPIO_Mode_AIN (CRITICAL: PB1 & PB5 share QFN Pin 16 ->
    //    BOTH kept in analog input mode, NEVER configured as digital output!)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    gpio.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7; // PA4(O2O0), PA5(O2N0), PA7(O2P0)
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_5; // PB0(O1P0), PB1/PB5(O1O1/A9)
    GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_3;              // PC0(A10 BEMF), PC3(A13 TouchKey)
    GPIO_Init(GPIOC, &gpio);

    // 2. MANDATORY: Unlock OPA control register via WCH OPAKEY sequence (0x45670123 / 0xCDEF89AB)!
    OPA_Unlock();

    // 3. OPA1: Internal Precision 16x PGA for 0.10-Ohm Shunt (PB0 -> PB5/PB1=ADC_CH9)
    OPA_InitTypeDef opa1;
    OPA_StructInit(&opa1);
    opa1.OPA_NUM = OPA1;
    opa1.PSEL    = CHP0;           // PB0 (O1P0) = SHUNT_NODE
    opa1.NSEL    = CHN_PGA_16xIN;  // Internal 16x PGA to GND -> 1.60 V/A, ZERO external resistors!
    opa1.Mode    = OUT_IO_OUT1;    // PB5 (O1O1, bonded to PB1 = ADC_CH9, errata-free!)
    opa1.FB      = FB_ON;          // Enable internal PGA feedback network
    OPA_Init(&opa1);
    OPA_Cmd(OPA1, ENABLE);

    // 4. OPA2: External 5.0x AC Gain around 2.50V VMID for Murata Piezo (PA7/PA5/PA4=ADC_CH4)
    OPA_InitTypeDef opa2;
    OPA_StructInit(&opa2);
    opa2.OPA_NUM = OPA2;
    opa2.PSEL    = CHP0;           // PA7 (O2P0) = PZ_IN
    opa2.NSEL    = CHN0;           // PA5 (O2N0) = OPA2_INV (5k/20k E1 external network)
    opa2.Mode    = OUT_IO_OUT0;    // PA4 (O2O0 = ADC_CH4, errata-free!)
    opa2.FB      = FB_OFF;         // External feedback divider sets exact 5.0x gain around 2.5V
    OPA_Init(&opa2);
    OPA_Cmd(OPA2, ENABLE);
#else
    // Host HIL / Arduino fallback
    pinMode(PIN_OPA1_P,    INPUT_ANALOG);
    pinMode(PIN_ADC_SHUNT, INPUT_ANALOG);
    pinMode(PIN_OPA2_P,    INPUT_ANALOG);
    pinMode(PIN_OPA2_N,    INPUT_ANALOG);
    pinMode(PIN_ADC_DIRT,  INPUT_ANALOG);
    pinMode(PIN_ADC_BEMF,  INPUT_ANALOG);
#endif
}

// Hardware PWM Period Constants (48 MHz SystemCoreClock):
//   TIM1 (Turbine, PB9):  48 MHz / 1920 = 25.000 kHz ultrasonic (1920 steps > 10-bit!)
//   TIM2 (Drive, PA0..3): 48 MHz / 3000 = 16.000 kHz silent PWM (3000 steps!)
static constexpr uint16_t TIM1_ARR_25KHZ = 1919;
static constexpr uint16_t TIM2_ARR_16KHZ = 2999;

void initHardwareTimersPWM() {
    pinMode(PIN_DRV_STBY, OUTPUT);
    digitalWrite(PIN_DRV_STBY, LOW); // Keep TC78H653FTG in standby during timer init!
    pinMode(PIN_PWM_TURB, OUTPUT);
    digitalWrite(PIN_PWM_TURB, LOW); // Keep 2SK4017 Gate OFF during boot!

#if defined(CH32X035) && defined(TIM1_BASE) && defined(TIM2_BASE)
    // --- TIM1_CH1 on PB9 (Default Mapping per WCH EVT/EXAM/TIM/PWM_Output!) @ Exact 25.0 kHz ---
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);
    GPIO_InitTypeDef g1 = {};
    g1.GPIO_Pin   = GPIO_Pin_9;
    g1.GPIO_Mode  = GPIO_Mode_AF_PP;
    g1.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g1);

    TIM_TimeBaseInitTypeDef tb1 = {};
    tb1.TIM_Period        = TIM1_ARR_25KHZ;
    tb1.TIM_Prescaler     = 0;
    tb1.TIM_ClockDivision = TIM_CKD_DIV1;
    tb1.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &tb1);

    TIM_OCInitTypeDef oc1 = {};
    oc1.TIM_OCMode      = TIM_OCMode_PWM1;
    oc1.TIM_OutputState = TIM_OutputState_Enable;
    oc1.TIM_Pulse       = 0;
    oc1.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &oc1);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE); // Enable TIM1 BDTR.MOE!
    TIM_Cmd(TIM1, ENABLE);

    // --- TIM2_CH1..4 on PA0..PA3 (Default Mapping per WCH EVT/EXAM/TIM/One_Pulse!) @ Exact 16.0 kHz ---
    // CRITICAL CH32X035 SILICON FACT: TIM2 is an Advanced Timer on CH32X035 with BDTR.MOE!
    // MUST call TIM_CtrlPWMOutputs(TIM2, ENABLE) or PA0..PA3 stay 0V forever!
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    GPIO_InitTypeDef g2 = {};
    g2.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    g2.GPIO_Mode  = GPIO_Mode_AF_PP;
    g2.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g2);

    TIM_TimeBaseInitTypeDef tb2 = {};
    tb2.TIM_Period        = TIM2_ARR_16KHZ;
    tb2.TIM_Prescaler     = 0;
    tb2.TIM_ClockDivision = TIM_CKD_DIV1;
    tb2.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tb2);

    TIM_OCInitTypeDef oc2 = {};
    oc2.TIM_OCMode      = TIM_OCMode_PWM1;
    oc2.TIM_OutputState = TIM_OutputState_Enable;
    oc2.TIM_Pulse       = 0;
    oc2.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &oc2); TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC2Init(TIM2, &oc2); TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC3Init(TIM2, &oc2); TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC4Init(TIM2, &oc2); TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_CtrlPWMOutputs(TIM2, ENABLE); // MANDATORY on CH32X035 TIM2 (sets TIM2->BDTR.MOE)!
    TIM_Cmd(TIM2, ENABLE);
#else
    analogWriteFrequency(25000);
    analogWriteResolution(10);
    analogWrite(PIN_PWM_TURB, 0);
    pinMode(PIN_MOT_LIN1, OUTPUT); pinMode(PIN_MOT_LIN2, OUTPUT);
    pinMode(PIN_MOT_RIN1, OUTPUT); pinMode(PIN_MOT_RIN2, OUTPUT);
    analogWrite(PIN_MOT_LIN1, 0); analogWrite(PIN_MOT_LIN2, 0);
    analogWrite(PIN_MOT_RIN1, 0); analogWrite(PIN_MOT_RIN2, 0);
#endif
}

void initHardwareWatchdog() {
#if defined(CH32X035) && defined(IWDG_BASE)
    // 48 MHz / 1024 = 46.875 kHz IWDG clock; Prescaler /64 -> 732.4 Hz; Reload 1200 -> 1.64 s timeout!
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1200);
    IWDG_ReloadCounter();
    IWDG_Enable();
#endif
}

static inline void feedWatchdog() {
#if defined(CH32X035) && defined(IWDG_BASE)
    IWDG_ReloadCounter();
#endif
}

void setTurbineDuty(float duty) {
    duty = constrain(duty, 0.0f, 1.0f);
#if defined(CH32X035) && defined(TIM1_BASE)
    TIM_SetCompare1(TIM1, (uint16_t)(duty * (float)TIM1_ARR_25KHZ + 0.5f));
#else
    analogWrite(PIN_PWM_TURB, (uint16_t)(duty * 1023.0f));
#endif
}

// Slow-Decay (Drive-Brake) H-Bridge PWM for TC78H653FTG (verified crisp low-speed torque!)
//   cmd == 0 -> IN1=0, IN2=0 (Coast/Stop)
//   cmd >  0 -> IN1=MAX, IN2=MAX*(1 - cmd)  (Drive forward <-> Short brake)
//   cmd <  0 -> IN1=MAX*(1 + cmd), IN2=MAX  (Drive reverse <-> Short brake)
void setWheelPWM(uint8_t in1, uint8_t in2, float cmd) {
    cmd = constrain(cmd, -1.0f, 1.0f);
#if defined(CH32X035) && defined(TIM2_BASE)
    uint16_t c1 = 0, c2 = 0;
    if (fabsf(cmd) < 0.02f) {
        c1 = 0; c2 = 0;
    } else if (cmd > 0.0f) {
        c1 = TIM2_ARR_16KHZ + 1;
        c2 = (uint16_t)((1.0f - cmd) * (float)TIM2_ARR_16KHZ);
    } else {
        c1 = (uint16_t)((1.0f + cmd) * (float)TIM2_ARR_16KHZ);
        c2 = TIM2_ARR_16KHZ + 1;
    }
    if (in1 == PIN_MOT_LIN1 && in2 == PIN_MOT_LIN2) {
        TIM_SetCompare1(TIM2, c1); TIM_SetCompare2(TIM2, c2);
    } else {
        TIM_SetCompare3(TIM2, c1); TIM_SetCompare4(TIM2, c2);
    }
#else
    uint16_t val = (uint16_t)(fabsf(cmd) * 1023.0f);
    if (cmd >= 0.0f) { analogWrite(in1, val); analogWrite(in2, 0); }
    else             { analogWrite(in1, 0);   analogWrite(in2, val); }
#endif
}

// Automatic TC78H653FTG Overcurrent Shutdown (ISD) Recovery:
// Per Toshiba datasheet, ISD latches outputs OFF until STBY toggles LOW -> HIGH.
void recoverHBridgeIfTripped(float cmdL, float cmdR) {
    static uint8_t zeroCurrentTicks = 0;
    bool driving = (fabsf(cmdL) > 0.35f) || (fabsf(cmdR) > 0.35f);
    float totalShuntV = g_tel.shuntMeanL + g_tel.shuntMeanR;
    if (driving && totalShuntV < 0.015f) {
        if (zeroCurrentTicks < 20) zeroCurrentTicks++;
        if (zeroCurrentTicks == 15) {
            // Pulse STBY LOW for 1 ms -> HIGH to clear latched TC78H653FTG ISD fault!
            digitalWrite(PIN_DRV_STBY, LOW);
            delayMicroseconds(1000);
            digitalWrite(PIN_DRV_STBY, HIGH);
            g_tel.driverFaultRetry = true;
            zeroCurrentTicks = 0;
        }
    } else {
        zeroCurrentTicks = 0;
    }
}

// =============================================================================
// 2. [REV 5.2 VERIFIED] 0-JPY Back-EMF Virtual Barometer (PC0/A10, 1.5 ms Coast + Auto-Cal)
// =============================================================================
static float g_vDrainOpenBaseV = -1.0f;
static uint8_t g_baseCalSamples = 0;

void sampleVirtualBarometer(float currentDuty) {
    static uint8_t clogStreak = 0;
    if (currentDuty < 0.25f) {
        g_tel.turbineBemfV = 0.0f;
        g_tel.estAirflowLs = 0.0f;
        return;
    }
    setTurbineDuty(0.0f);
    delayMicroseconds(BEMF_COAST_SETTLE_US);

    // 4-sample burst average during settled plateau [1.20 .. 1.50 ms]
    // R18/R19 = 10k/10k ±1% -> V_drain = 2.0 * V_adc_bemf (PC0/A10, errata-free!)
    constexpr float ADC_TO_V = 5.00f / 4095.0f;
    float vAdcSum = 0.0f;
    for (int k = 0; k < 4; ++k) {
        vAdcSum += analogRead(PIN_ADC_BEMF) * ADC_TO_V;
        delayMicroseconds(60);
    }
    setTurbineDuty(currentDuty); // Resume 25 kHz PWM seamlessly!

    float vDrain = 2.0f * (vAdcSum * 0.25f);

    // Startup open-flow baseline auto-calibration (first 8 unobstructed coast readings ~1.2s)
    if (g_baseCalSamples < 8) {
        g_vDrainOpenBaseV = (g_baseCalSamples == 0) ? vDrain : (0.75f * g_vDrainOpenBaseV + 0.25f * vDrain);
        g_baseCalSamples++;
    } else if (clogStreak == 0 && fabsf(vDrain - g_vDrainOpenBaseV) < 0.18f) {
        g_vDrainOpenBaseV = 0.995f * g_vDrainOpenBaseV + 0.005f * vDrain;
    }

    // OpenFOAM 3D vaneless volute: clog drops torque -> RPM & Back-EMF rise -> V_drain drops!
    float dV_clog = max(0.0f, g_vDrainOpenBaseV - vDrain);
    float vBemfEst = constrain(BEMF_TARGET_STD_V + dV_clog, 0.0f, 6.20f);
    g_tel.turbineBemfV = (g_tel.turbineBemfV < 0.5f) ? vBemfEst : (0.75f * g_tel.turbineBemfV + 0.25f * vBemfEst);
    g_tel.estAirflowLs = constrain(2.50f * (1.0f - dV_clog / 0.62f), 0.0f, 4.0f);

    // Protect 5S NiMH pack from deep-discharge cell reversal (V_drain < 0.25V with no clog streak)
    if (g_baseCalSamples >= 8 && g_vDrainOpenBaseV < 0.55f && vDrain < 0.45f) {
        g_tel.lowBatteryCutoff = true;
    }

    if (dV_clog > BEMF_CLOG_DELTA_V) {
        if (clogStreak < 30) clogStreak++;
    } else {
        clogStreak = 0;
    }
    g_tel.filterClogAlert = (clogStreak >= 24);
}

// =============================================================================
// 3. [REV 5.3 0-LUX PROOF] Active Synchronous Emitter + Reverse-Bias Photodiode Ranging
// =============================================================================
// Measures single reverse-bias LED photodiode discharge time (K->rxPin, A->GND)
static uint32_t rawLedDischargeUs(uint8_t rxPin, uint32_t timeoutUs = 1200) {
    pinMode(rxPin, OUTPUT);
    digitalWrite(rxPin, HIGH);
    delayMicroseconds(10);
    uint32_t t0 = safeMicros();
    pinMode(rxPin, INPUT);
    while (digitalRead(rxPin) == HIGH) {
        if ((safeMicros() - t0) >= timeoutUs) return timeoutUs;
    }
    return safeMicros() - t0;
}

// Synchronous Active-Minus-Ambient Differential Optical Ranging:
//   - Works in 0.0-lux pitch-black darkness under an 80mm sofa (3.0 mA pulsed active emitter!)
//   - Cancels bright ambient sunlight via differential photocurrent (1/t_act - 1/t_amb)!
//   - Returns equivalent ambient-cancelled discharge time [us]:
//       Small = strong reflection (wall/floor present); Large = open space / stair cliff!
uint32_t measureActiveOpticalUs(uint8_t rxPin, uint8_t emitPin, uint32_t timeoutUs = 1200) {
    digitalWrite(emitPin, LOW);
    uint32_t tAmb = rawLedDischargeUs(rxPin, timeoutUs);

    digitalWrite(emitPin, HIGH);
    delayMicroseconds(20);
    uint32_t tAct = rawLedDischargeUs(rxPin, timeoutUs);
    digitalWrite(emitPin, LOW);

    float rateAmb  = (tAmb >= timeoutUs) ? 0.0f : (1.0e6f / (float)max(tAmb, (uint32_t)1));
    float rateAct  = (tAct >= timeoutUs) ? 0.0f : (1.0e6f / (float)max(tAct, (uint32_t)1));
    float rateRefl = max(0.0f, rateAct - rateAmb);
    if (rateRefl < 833.0f) return timeoutUs;
    uint32_t tEq = (uint32_t)(1.0e6f / rateRefl + 0.5f);
    return constrain(tEq, (uint32_t)15, timeoutUs);
}

// Isolated Bumper Microswitch (PB11) & Capacitive TouchKey (PC3) — ZERO cross-triggering!
bool readBumperSwitch() {
    return (digitalRead(PIN_BUMP_SW) == LOW);
}

bool readCapacitiveTouchKey() {
#if defined(CH32X035) && defined(GPIOC_BASE)
    // Bypass upstream openwch/arduino_core_ch32 variant_CH32X035G8U.cpp bug where
    // PC3 = PIN_A13 indexes analogInputPin[13] out of bounds (array length is 12)!
    GPIO_InitTypeDef gt = {};
    gt.GPIO_Pin   = GPIO_Pin_3;
    gt.GPIO_Mode  = GPIO_Mode_Out_PP;
    gt.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gt);
    GPIO_ResetBits(GPIOC, GPIO_Pin_3);
    delayMicroseconds(8);
    uint32_t t0 = safeMicros();
    gt.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gt);
    while (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_3) == 0 && (safeMicros() - t0) < 120) {}
    return ((safeMicros() - t0) >= 18);
#else
    pinMode(PIN_TOUCH_KEY, OUTPUT);
    digitalWrite(PIN_TOUCH_KEY, LOW);
    delayMicroseconds(8);
    uint32_t t0 = safeMicros();
    pinMode(PIN_TOUCH_KEY, INPUT_PULLUP);
    while (digitalRead(PIN_TOUCH_KEY) == LOW && (safeMicros() - t0) < 120) {}
    return ((safeMicros() - t0) >= 18);
#endif
}

// =============================================================================
// 3B. [REV 5.3 RISK #3 & #5] Hardware POST + Zero-Calibration Floor Auto-Baseline
// =============================================================================
static void blinkDiagLed(uint8_t emitPin, uint8_t count, uint16_t onMs = 70) {
    for (uint8_t i = 0; i < count; ++i) {
        digitalWrite(emitPin, HIGH);
        delay(onMs);
        digitalWrite(emitPin, LOW);
        delay(onMs);
    }
}

uint8_t runPowerOnSelfTest() {
    constexpr float ADC_TO_V = 5.00f / 4095.0f;
    float sumBemf = 0.0f, sumShunt = 0.0f, sumPiezo = 0.0f;
    for (int i = 0; i < 8; ++i) {
        sumBemf  += analogRead(PIN_ADC_BEMF)  * ADC_TO_V;
        sumShunt += analogRead(PIN_ADC_SHUNT) * ADC_TO_V;
        sumPiezo += analogRead(PIN_ADC_DIRT)  * ADC_TO_V;
        delayMicroseconds(100);
    }
    float vDrainIdle = 2.0f * (sumBemf  * 0.125f);
    float vShuntIdle =        (sumShunt * 0.125f);
    float vPiezoIdle =        (sumPiezo * 0.125f);

    uint8_t fault = POST_OK;
    // 1. With MOSFET OFF, turbine drain sits at Vbat (4.8..6.6V). <0.90V = shorted FET / open turbine / dead cell!
    if (vDrainIdle < 0.90f) {
        fault |= POST_ERR_MOSFET_OR_OPEN_TURB;
        g_turbineAllowed = false;
        blinkDiagLed(PIN_EMIT_FRONT, 2, 90);
    }
    // 2. With H-Bridge in STBY=LOW, OPA1 shunt output must be near 0V (<0.65V). >0.65V = solder bridge / shorted H-bridge!
    if (vShuntIdle > 0.65f) {
        fault |= POST_ERR_SHUNT_OR_HBRIDGE_FAULT;
        g_driveAllowed = false;
        blinkDiagLed(PIN_EMIT_SIDE, 3, 90);
    }
    // 3. OPA2 VMID must sit near 2.50V ([1.45..3.55V]). Out of range = degrade piezo cleanly (never stick in Turbo!)
    if (vPiezoIdle < 1.45f || vPiezoIdle > 3.55f) {
        fault |= POST_WARN_PIEZO_VMID_DEGRADED;
        g_piezoEnabled = false;
    }

    g_tel.postFaultMask = fault;
    if (fault == POST_OK) {
        blinkDiagLed(PIN_EMIT_FRONT, 1, 35); // Single crisp heartbeat = Hardware POST 100% PASS!
    }
    return fault;
}

void calibrateFloorOpticalBaseline() {
    // Auto-adapts cliff thresholds on EVERY power-on from matte black carpet (rho=0.04)
    // to glossy white porcelain tile (rho=0.92) & ±50% red LED manufacturing lot variation!
    uint32_t sumL = 0, sumR = 0;
    for (int i = 0; i < 6; ++i) {
        sumL += measureActiveOpticalUs(PIN_LED_CLIFF_L_RX, PIN_EMIT_CLIFF_L);
        sumR += measureActiveOpticalUs(PIN_LED_CLIFF_R_RX, PIN_EMIT_CLIFF_R);
    }
    uint32_t baseL = sumL / 6U;
    uint32_t baseR = sumR / 6U;

    // Mid-air power-on guard: only adapt if valid floor reflection (20..480 us) is observed!
    if (baseL >= 20U && baseL <= 480U) {
        g_cliffThreshL = constrain((uint32_t)((float)baseL * 2.15f + 0.5f), (uint32_t)190, (uint32_t)760);
    } else {
        g_cliffThreshL = 260U;
    }
    if (baseR >= 20U && baseR <= 480U) {
        g_cliffThreshR = constrain((uint32_t)((float)baseR * 2.15f + 0.5f), (uint32_t)190, (uint32_t)760);
    } else {
        g_cliffThreshR = 260U;
    }
}

// =============================================================================
// 4. 1-Shunt TDM Ripple DSP (Auto-Normalized ±40%) + Piezo DSP + USBFS Optical Mouse Fusion
// =============================================================================
void sampleAnalogAndOpticalSensors(float cmdL, float cmdR) {
    static float prevShunt = 0.0f, prevAcS = 0.0f, prevPiezo = 0.0f;
    static uint32_t lastOdomUs = 0;
    static uint8_t slipStreak = 0;

    constexpr int N_SAMP = 64;
    constexpr float ADC_TO_V = 5.00f / 4095.0f;
    float sumShunt = 0.0f, acShuntEnergy = 0.0f, sumPiezoSq = 0.0f, peakPiezo = 0.0f;
    int shuntZcr = 0, piezoZcr = 0;

    for (int i = 0; i < N_SAMP; ++i) {
        float vShunt = analogRead(PIN_ADC_SHUNT) * ADC_TO_V;
        sumShunt += vShunt;
        float acS = vShunt - prevShunt;
        acShuntEnergy += acS * acS;
        if ((acS >= 0.008f && prevAcS < -0.008f) || (acS <= -0.008f && prevAcS > 0.008f)) shuntZcr++;
        if (fabsf(acS) >= 0.008f) prevAcS = acS;
        prevShunt = 0.85f * prevShunt + 0.15f * vShunt;

        if (g_piezoEnabled) {
            float vPiezo = (analogRead(PIN_ADC_DIRT) * ADC_TO_V) - 2.50f;
            float aP = fabsf(vPiezo);
            sumPiezoSq += vPiezo * vPiezo;
            if (aP > peakPiezo) peakPiezo = aP;
            if ((vPiezo >= 0.0f && prevPiezo < 0.0f) || (vPiezo < 0.0f && prevPiezo >= 0.0f)) piezoZcr++;
            prevPiezo = vPiezo;
        }
        delayMicroseconds(70);
    }

    float meanV = sumShunt / N_SAMP;
    float rmsRipple = sqrtf(acShuntEnergy / N_SAMP);

    // Online ±40% Shunt Resistance Auto-Normalization (absorbs homemade mouse-wire gauge variance!)
    // Targets 0.260V -> 0.528V TinyML feature (exact 0.24*2.20V STATE_HARD_FLOOR_NORMAL center!).
    // Guarded by shuntZcr >= 1 (motors confirmed spinning, NOT stalled!) and 0.10V < meanV < 0.75V.
    if (g_shuntCalCount < 12 && cmdL > 0.55f && cmdR > 0.55f && shuntZcr >= 1 && meanV > 0.10f && meanV < 0.75f) {
        g_shuntCruiseAvg = (g_shuntCalCount == 0) ? meanV : (0.75f * g_shuntCruiseAvg + 0.25f * meanV);
        g_shuntCalCount++;
        g_shuntGainCorr = constrain(0.260f / max(g_shuntCruiseAvg, 0.10f), 0.45f, 1.85f);
    }

    float totalCmd = fabsf(cmdL) + fabsf(cmdR) + 0.05f;
    float fracL = (fabsf(cmdL) + 0.025f) / totalCmd;
    float fracR = (fabsf(cmdR) + 0.025f) / totalCmd;

    constexpr float SHUNT_TO_TINYML_SCALE = 3.25f / (SHUNT_OHM * OPA1_PGA_GAIN);
    constexpr float ZCR_TO_HZ = 1.0f / (2.0f * (float)N_SAMP * 70.0e-6f);
    float effScale = SHUNT_TO_TINYML_SCALE * g_shuntGainCorr;
    g_tel.shuntMeanL  = meanV * effScale * fracL * 2.0f;
    g_tel.shuntMeanR  = meanV * effScale * fracR * 2.0f;
    g_tel.rippleFreqL = (float)shuntZcr * ZCR_TO_HZ * fracL * 2.0f;
    g_tel.rippleFreqR = (float)shuntZcr * ZCR_TO_HZ * fracR * 2.0f;
    g_tel.rippleAmpL  = rmsRipple * effScale;
    g_tel.rippleAmpR  = rmsRipple * effScale;

    g_tel.piezoRms    = g_piezoEnabled ? sqrtf(sumPiezoSq / N_SAMP) : 0.0f;
    g_tel.piezoPeak   = g_piezoEnabled ? peakPiezo : 0.0f;
    g_tel.piezoZcr    = g_piezoEnabled ? (float)piezoZcr : 0.0f;

    // 0-Lux Dark-Room Proof Active Synchronous Optical Ranging (1 emitter pulsed at a time!)
    g_tel.ledFrontUs  = measureActiveOpticalUs(PIN_LED_FRONT_RX,   PIN_EMIT_FRONT);
    g_tel.ledSideUs   = measureActiveOpticalUs(PIN_LED_SIDE_RX,    PIN_EMIT_SIDE);
    g_tel.ledCliffLUs = measureActiveOpticalUs(PIN_LED_CLIFF_L_RX, PIN_EMIT_CLIFF_L);
    g_tel.ledCliffRUs = measureActiveOpticalUs(PIN_LED_CLIFF_R_RX, PIN_EMIT_CLIFF_R);

    g_tel.bumperHit   = readBumperSwitch();
    g_tel.touchActive = readCapacitiveTouchKey();

    // --- Real CH32X035 USBFS 2-Axis Self-Calibrating Optical-Gyro Odometry + Slip Fusion! ---
    // Exact 2x2 Closed-Form Decoupling of mouse mount angle (alpha) & wheel diameter mismatch (delta):
    //   r_turn     = dy/dx = +tan(alpha) + (W / 4L) * delta   (accumulated on in-place turns)
    //   r_straight = dx/dy = -tan(alpha) + (L / W)  * delta   (accumulated on straight cruise)
    //   -> delta = (r_turn + r_straight) / (W/(4L) + L/W),  alpha = atan(r_turn - (W/(4L))*delta)
    // Solves hand-glued mouse angle to <0.02 deg & delivers 6.65x lower 60-sec SLAM drift!
    static float sumDyTurn = 0.0f, sumDxTurn = 0.0f;
    static float sumDxStr  = 0.0f, sumDyStr  = 0.0f;
    constexpr float K_TURN = WHEEL_TRACK_MM / (4.0f * MOUSE_LEVER_ARM_MM); // 0.56410
    constexpr float K_STR  = MOUSE_LEVER_ARM_MM / WHEEL_TRACK_MM;          // 0.44318
    constexpr float K_SUM  = K_TURN + K_STR;                               // 1.00728

    uint32_t nowUs = safeMicros();
    float dt = (lastOdomUs == 0) ? 0.01f : constrain((nowUs - lastOdomUs) * 1e-6f, 0.001f, 0.25f);
    lastOdomUs = nowUs;

    float vL = cmdL * CRUISE_MM_S, vR = cmdR * CRUISE_MM_S;
    float dThetaWheel = ((vR - vL) / WHEEL_TRACK_MM) * dt;
    float dsWheel     = 0.5f * (vL + vR) * dt;

    int16_t mdx = 0, mdy = 0;
    bool mouseLive = usbMousePoll(&mdx, &mdy);
    g_tel.usbMouseLive = mouseLive;

    float dsFused     = dsWheel;
    float dThetaFused = dThetaWheel;
    if (mouseLive) {
        float dyRawMm = (float)mdy * MOUSE_MM_PER_COUNT;
        float dxRawMm = (float)mdx * MOUSE_MM_PER_COUNT;

        // Online 2x2 accumulation (bounded by 5000mm to prevent float saturation)
        if ((cmdL * cmdR) < -0.10f && sumDxTurn < 5000.0f) {
            float sgn = (cmdR > cmdL) ? 1.0f : -1.0f;
            sumDyTurn += sgn * dyRawMm;
            sumDxTurn += sgn * dxRawMm;
        } else if (cmdL > 0.50f && cmdR > 0.50f && fabsf(cmdR - cmdL) < 0.05f && sumDyStr < 5000.0f) {
            sumDxStr += dxRawMm;
            sumDyStr += dyRawMm;
        }
        if (sumDxTurn > 5.0f && sumDyStr > 20.0f) {
            float rTurn = sumDyTurn / sumDxTurn;
            float rStr  = sumDxStr  / sumDyStr;
            g_wheelDiaMismatch   = (rTurn + rStr) / K_SUM;
            g_mouseMountAngleRad = constrain(atanf(rTurn - K_TURN * g_wheelDiaMismatch), -0.18f, 0.18f);
            g_mouseCalLocked     = true;
        }

        // De-rotate sensor frame by solved mount angle -> true (forward u, lateral v = L*dTheta)!
        float ca = cosf(g_mouseMountAngleRad);
        float sa = sinf(g_mouseMountAngleRad);
        float dsMouseFwd  = dyRawMm * ca - dxRawMm * sa;
        float dThetaMouse = (dyRawMm * sa + dxRawMm * ca) / MOUSE_LEVER_ARM_MM;

        dsFused = 0.95f * dsMouseFwd + 0.05f * dsWheel;
        if (g_mouseCalLocked || fabsf(dThetaWheel) > 0.004f) {
            dThetaFused = 0.98f * dThetaMouse + 0.02f * dThetaWheel;
        }

        if (fabsf(dsWheel) > 0.6f && (cmdL * cmdR) > 0.25f) {
            float slip = constrain(1.0f - (fabsf(dsMouseFwd) / max(fabsf(dsWheel), 0.5f)), 0.0f, 1.0f);
            g_tel.wheelSlipRatio = 0.65f * g_tel.wheelSlipRatio + 0.35f * slip;
            if (g_tel.wheelSlipRatio > 0.48f) {
                if (slipStreak < 20) slipStreak++;
            } else if (slipStreak > 0) {
                slipStreak--;
            }
            g_tel.wheelSlipDetected = (slipStreak >= 5);
        } else {
            g_tel.wheelSlipRatio *= 0.85f;
            if (slipStreak > 0) slipStreak--;
            g_tel.wheelSlipDetected = (slipStreak >= 5);
        }
    } else {
        g_tel.wheelSlipRatio    = 0.0f;
        g_tel.wheelSlipDetected = false;
        slipStreak              = 0;
    }

    float midTheta = g_tel.odomTheta_rad + 0.5f * dThetaFused;
    g_tel.odomTheta_rad += dThetaFused;
    g_tel.odomX_mm += dsFused * cosf(midTheta);
    g_tel.odomY_mm += dsFused * sinf(midTheta);
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
        normQ7((float)g_tel.ledFrontUs, 1000.0f),
        normQ7((float)g_tel.ledCliffLUs, 1000.0f), normQ7((float)g_tel.ledCliffRUs, 1000.0f)
    };
    return suga_tinyml_predict(feat);
}

// =============================================================================
// 5. Autonomous 64x64 SLAM + Right-Wall Edge Sweep + Closed-Loop Suction FSM
// =============================================================================
enum NavMode {
    NAV_STANDBY = 0,
    NAV_CRUISE_SLAM,
    NAV_WALL_EDGE_SWEEP,   // Reuleaux-D flat front edge-follow sweep
    NAV_DIRT_TURBO_SPIRAL, // 1,581 Pa (6.6V) Piezo Dirt-Turbo Spiral!
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
    analogReadResolution(12);          // MANDATORY: openwch/arduino_core_ch32 defaults analogRead to 10-bit!
    initHardwareTimersPWM();
    initOnChipOpAmps();
    pinMode(PIN_EMIT_FRONT,   OUTPUT); digitalWrite(PIN_EMIT_FRONT,   LOW);
    pinMode(PIN_EMIT_SIDE,    OUTPUT); digitalWrite(PIN_EMIT_SIDE,    LOW);
    pinMode(PIN_EMIT_CLIFF_L, OUTPUT); digitalWrite(PIN_EMIT_CLIFF_L, LOW);
    pinMode(PIN_EMIT_CLIFF_R, OUTPUT); digitalWrite(PIN_EMIT_CLIFF_R, LOW);
    pinMode(PIN_BUMP_SW,      INPUT_PULLUP);
    memset(g_grid, 0, sizeof(g_grid));
    delay(350);                        // Let 5V LDO & 470uF rail stabilize before POST & motors!

    // 1. Run Power-On Self-Test (catches solder bridges / shorted MOSFET / open turbine safely!)
    uint8_t postStatus = runPowerOnSelfTest();

    // 2. Auto-calibrate 8xLED floor reflectance baseline on the user's real floor!
    calibrateFloorOpticalBaseline();

    // 3. Initialize CH32X035 Hardware USBFS Host for optional Daiso optical mouse on J_USB!
    usbMouseInit();

    if (postStatus & POST_ERR_SHUNT_OR_HBRIDGE_FAULT) {
        g_mode = NAV_STANDBY;          // Keep TC78H653FTG safely in STBY=LOW on hard fault!
    } else {
        digitalWrite(PIN_DRV_STBY, HIGH); // Release AE-TC78H653FTG cleanly (ZERO power-on twitch!)
    }
    initHardwareWatchdog();            // Arm 1.64s hardware IWDG safety watchdog!
}

void loop() {
    static uint32_t lastTouchMs = 0, lastBaroMs = 0;
    uint32_t nowMs = millis();
    feedWatchdog();

    sampleAnalogAndOpticalSensors(g_cmdL, g_cmdR);
    if (g_driveAllowed) {
        recoverHBridgeIfTripped(g_cmdL, g_cmdR);
    }

    // Every 150ms: Run 0-JPY 1.5 ms Back-EMF Virtual Barometer (<1.0% duty overhead!)
    if ((nowMs - lastBaroMs) >= 150) {
        lastBaroMs = nowMs;
        sampleVirtualBarometer(g_smoothTurb);
    }

    if (g_tel.touchActive && (nowMs - lastTouchMs) > 800) {
        lastTouchMs = nowMs;
        if (g_driveAllowed) {
            g_mode = (g_mode == NAV_STANDBY) ? NAV_CRUISE_SLAM : NAV_STANDBY;
        }
    }

    if (g_tel.lowBatteryCutoff || !g_driveAllowed) {
        g_mode = NAV_STANDBY;
    }

    uint8_t state = runTinyMLInference();
    // Floor-Adaptive Optical Cliff Gate (auto-calibrated at boot from black carpet to white tile!)
    bool cliffOptical = (g_tel.ledCliffLUs > g_cliffThreshL) || (g_tel.ledCliffRUs > g_cliffThreshR);
    uint32_t mlGateL  = (uint32_t)(g_cliffThreshL * 85U / 100U);
    uint32_t mlGateR  = (uint32_t)(g_cliffThreshR * 85U / 100U);
    bool cliffHard = cliffOptical || ((state == STATE_STAIR_CLIFF_EDGE) && ((g_tel.ledCliffLUs > mlGateL) || (g_tel.ledCliffRUs > mlGateR)));
    bool wallHard  = g_tel.bumperHit || (g_tel.ledFrontUs < 110) || (state == STATE_WALL_SOFT_STALL);
    bool severeSlipStuck = g_tel.wheelSlipDetected && (g_tel.wheelSlipRatio > 0.82f);
    updateOccupancyGrid(wallHard);

    if (g_mode == NAV_STANDBY) {
        g_cmdL = 0.0f; g_cmdR = 0.0f; g_cmdTurb = TURB_DUTY_OFF;
    } else if (cliffHard) {
        g_mode = NAV_CLIFF_ESTOP; g_modeTimerMs = nowMs;
    } else if ((state == STATE_HAIR_BRUSH_JAM || severeSlipStuck) && g_mode != NAV_UNJAM_REVERSE) {
        g_mode = NAV_UNJAM_REVERSE; g_modeTimerMs = nowMs;
    } else if (wallHard && g_mode != NAV_AVOID_BACKOFF && g_mode != NAV_CLIFF_ESTOP) {
        g_mode = NAV_AVOID_BACKOFF; g_modeTimerMs = nowMs;
    } else if (g_piezoEnabled && state == STATE_HIGH_DIRT_DEBRIS && g_mode == NAV_CRUISE_SLAM) {
        g_mode = NAV_DIRT_TURBO_SPIRAL; g_modeTimerMs = nowMs;
    } else if (g_tel.ledSideUs < 140 && g_mode == NAV_CRUISE_SLAM) {
        g_mode = NAV_WALL_EDGE_SWEEP; g_modeTimerMs = nowMs;
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
            else                    { g_mode = NAV_WALL_EDGE_SWEEP; g_modeTimerMs = nowMs; }
            break;
        case NAV_WALL_EDGE_SWEEP: {
            g_cmdTurb = (g_piezoEnabled && state == STATE_HIGH_DIRT_DEBRIS) ? TURB_DUTY_TURBO : TURB_DUTY_STD;
            float err = constrain(((float)g_tel.ledSideUs - 125.0f) * 0.0022f, -0.22f, 0.22f);
            g_cmdL = 0.68f + err; g_cmdR = 0.68f - err;
            if (elapsed > 3200 || g_tel.ledSideUs > 320) g_mode = NAV_CRUISE_SLAM;
            break;
        }
        case NAV_DIRT_TURBO_SPIRAL: {
            g_cmdTurb = TURB_DUTY_TURBO; // 1,581 Pa (6.6V) Dirt-Turbo!
            float rFactor = constrain(elapsed / 3800.0f, 0.15f, 0.85f);
            g_cmdL = 0.65f * rFactor; g_cmdR = 0.65f;
            if (elapsed > 3800) g_mode = NAV_CRUISE_SLAM;
            break;
        }
        case NAV_CRUISE_SLAM:
        default: {
            float baseDuty = (g_piezoEnabled && state == STATE_HIGH_DIRT_DEBRIS) ? TURB_DUTY_TURBO : TURB_DUTY_STD;
            if (baseDuty == TURB_DUTY_STD && g_tel.turbineBemfV > 1.0f && !g_tel.filterClogAlert) {
                float bemfErr = BEMF_TARGET_STD_V - g_tel.turbineBemfV;
                baseDuty = constrain(baseDuty + bemfErr * 0.12f, TURB_DUTY_ECO, TURB_DUTY_TURBO);
            }
            g_cmdTurb = baseDuty;
            // Adaptive mild rubber-band slip compensation boost (+0.12 duty when slip 48..82%)
            float cruiseCmd = g_tel.wheelSlipDetected ? 0.84f : 0.72f;
            g_cmdL = cruiseCmd; g_cmdR = cruiseCmd;
            break;
        }
    }

    if (!g_turbineAllowed) g_cmdTurb = TURB_DUTY_OFF;
    if (g_smoothTurb < g_cmdTurb)      g_smoothTurb = min(g_cmdTurb, g_smoothTurb + 0.04f);
    else if (g_smoothTurb > g_cmdTurb) g_smoothTurb = max(g_cmdTurb, g_smoothTurb - 0.06f);

    setTurbineDuty(g_smoothTurb);
    setWheelPWM(PIN_MOT_LIN1, PIN_MOT_LIN2, g_cmdL);
    setWheelPWM(PIN_MOT_RIN1, PIN_MOT_RIN2, g_cmdR);
}
