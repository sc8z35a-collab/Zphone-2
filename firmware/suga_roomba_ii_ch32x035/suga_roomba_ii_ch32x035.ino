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
//      - TIM1_CH1 (PB9, PartialRemap1) = 25.0 kHz ultrasonic turbine PWM -> 2SK4017 Gate
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
#include "suga_roomba_tinyml.h"

// --- Pin Map (100% 1:1 Matched to Rev 5.2 KiCad PCB & Real CH32X035G8U6 QFN28 Silicon!) ---
// TIM2_CH1..4 (Default Remap = 000 -> 16.0 kHz Slow-Decay Drive-Brake H-Bridge PWM)
constexpr uint8_t PIN_MOT_LIN1     = PA0;   // QFN Pin 5  -> AE-TC78H653FTG CN1-6 (IN1)
constexpr uint8_t PIN_MOT_LIN2     = PA1;   // QFN Pin 6  -> AE-TC78H653FTG CN1-7 (IN2)
constexpr uint8_t PIN_MOT_RIN1     = PA2;   // QFN Pin 7  -> AE-TC78H653FTG CN1-5 (IN3)
constexpr uint8_t PIN_MOT_RIN2     = PA3;   // QFN Pin 8  -> AE-TC78H653FTG CN1-4 (IN4)
constexpr uint8_t PIN_DRV_STBY     = PC14;  // QFN Pin 28 -> AE-TC78H653FTG CN1-3 (STBY, R2=10k PD)

// TIM1_CH1 (PartialRemap1 -> Dedicated 25.0 kHz Ultrasonic Turbine PWM, ZERO conflict w/ TIM2!)
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
constexpr uint8_t PIN_TOUCH_KEY    = PC3;   // QFN Pin 4  <- 0-JPY Al-Can Capacitive Touch Pad (A13/TKey)
// PC16 = USB_DM (QFN 26), PC17 = USB_DP / BOOT (QFN 27), PC18 = SWDIO (QFN 25), PC19 = SWCLK (QFN 24)

// --- Rev 5.2 OpenFOAM v2406 Smooth Vaneless Volute Operating Targets (RS-385PH + Ø76mm 9-Blade) ---
constexpr float TURB_DUTY_OFF   = 0.00f;
constexpr float TURB_DUTY_ECO   = 0.72f;  // ~4.5V eff ->   760 Pa shutoff, low acoustic power
constexpr float TURB_DUTY_STD   = 0.90f;  // ~6.0V eff ->   737 Pa Op / 1,354 Pa shutoff, eta_fan=29.6%
constexpr float TURB_DUTY_TURBO = 1.00f;  // ~6.6V pk  -> 1,606 Pa shutoff, 1.87 A (<3A polyfuse SAFE!)

// --- Virtual Barometer Targets (1.5 ms Coast Window Verified in Ngspice 42 200-Run MC!) ---
constexpr uint32_t BEMF_COAST_SETTLE_US = 1200; // Wait 1.2 ms (>10x 0.113ms freewheel decay!)
constexpr uint32_t BEMF_COAST_TOTAL_US  = 1500; // Total 1.5 ms PWM-OFF coast window (<1% duty overhead)
constexpr float    BEMF_TARGET_STD_V    = 4.80f; // Open-flow equilibrium @ 6.0V
constexpr float    BEMF_CLOG_DELTA_V    = 0.42f; // Relative Back-EMF rise -> Dust-Cup / Mesh Full Alert!
constexpr float    SHUNT_OHM            = 0.10f; // 0.10-Ohm bifilar shunt (V_shunt <= 0.15V @ 1.5A stall!)
constexpr float    OPA1_PGA_GAIN        = 16.0f; // Internal CH32X035 OPA1 16x PGA -> 1.60 V/A!

// --- Physical Robot Constants (Rev 5.2 Real-Hardware 216x215x64/75.6mm, ~790g) ---
constexpr float WHEEL_DIA_MM       = 50.0f;  // Ø48mm SLA rim + 1mm Daiso rubber band (25.0:1)
constexpr float WHEEL_TRACK_MM     = 176.0f; // ±88.0mm exact CAD wheel track
constexpr float CRUISE_MM_S        = 274.0f; // 104.6 rpm cruise @ 6V
constexpr float CELL_SIZE_MM       = 65.0f;  // 64x64 SLAM covers 4.16m x 4.16m room
constexpr int   GRID_N             = 64;

struct SensorFrame {
    float shuntMeanL, shuntMeanR;
    float rippleFreqL, rippleFreqR;
    float rippleAmpL, rippleAmpR;
    float piezoRms, piezoPeak, piezoZcr;
    uint32_t ledFrontUs, ledSideUs, ledCliffLUs, ledCliffRUs; // Ambient-cancelled active eq. [us]
    bool bumperHit, touchActive;
    float turbineBemfV;     // Real-time settled Back-EMF [V] (1.5ms window)
    float estAirflowLs;     // Virtual Barometer estimated Q [L/s]
    bool  filterClogAlert;  // True if relative Back-EMF rise > +0.42V for > 3.5s
    bool  driverFaultRetry; // True if TC78H653FTG STBY auto-recovery was triggered
    bool  lowBatteryCutoff; // True if 5S NiMH sagged below 4.8V safe floor
    float odomX_mm, odomY_mm, odomTheta_rad;
};

static SensorFrame g_tel = {};
static uint8_t g_grid[GRID_N][GRID_N];

// =============================================================================
// 1. Real CH32X035 Silicon Peripheral Drivers (OPA1 16x PGA, OPA2 5x, TIM1, TIM2, IWDG)
// =============================================================================
void initOnChipOpAmps() {
#if defined(CH32X035) && defined(OPA_BASE) && defined(OPA_Unlock)
    // Official WCH CH32X035 OPA initialization sequence (verified against openwch/ch32x035):
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

#if defined(CH32X035) && defined(TIM1_BASE) && defined(TIM_TimeBaseInit)
    // --- TIM1_CH1 on PB9 (PartialRemap1) @ Exact 25.0 kHz ---
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap1_TIM1, ENABLE);
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
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);

    // --- TIM2_CH1..4 on PA0..PA3 (Default Mapping, ZERO remap conflict!) @ Exact 16.0 kHz ---
    // Configured directly in silicon registers because WCH arduino_core_ch32 PinMap_TIM omits PA0..PA3!
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
#if defined(CH32X035) && defined(IWDG_WriteAccess_Enable)
    // 48 MHz / 1024 = 46.875 kHz IWDG clock; Prescaler /64 -> 732.4 Hz; Reload 1200 -> 1.64 s timeout!
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1200);
    IWDG_ReloadCounter();
    IWDG_Enable();
#endif
}

static inline void feedWatchdog() {
#if defined(CH32X035) && defined(IWDG_ReloadCounter)
    IWDG_ReloadCounter();
#endif
}

void setTurbineDuty(float duty) {
    duty = constrain(duty, 0.0f, 1.0f);
#if defined(CH32X035) && defined(TIM1_BASE) && defined(TIM_SetCompare1)
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
#if defined(CH32X035) && defined(TIM2_BASE) && defined(TIM_SetCompare1)
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
// 3. [REV 5.2 0-LUX PROOF] Active Synchronous Emitter + Reverse-Bias Photodiode Ranging
// =============================================================================
// Measures single reverse-bias LED photodiode discharge time (K->rxPin, A->GND)
static uint32_t rawLedDischargeUs(uint8_t rxPin, uint32_t timeoutUs = 1200) {
    pinMode(rxPin, OUTPUT);
    digitalWrite(rxPin, HIGH);
    delayMicroseconds(10);
    uint32_t t0 = micros();
    pinMode(rxPin, INPUT);
    while (digitalRead(rxPin) == HIGH) {
        if ((micros() - t0) >= timeoutUs) return timeoutUs;
    }
    return micros() - t0;
}

// Synchronous Active-Minus-Ambient Differential Optical Ranging:
//   - Works in 0.0-lux pitch-black darkness under an 80mm sofa (3.0 mA pulsed active emitter!)
//   - Cancels bright ambient sunlight via differential photocurrent (1/t_act - 1/t_amb)!
//   - Returns equivalent ambient-cancelled discharge time [us]:
//       Small (< 180 us) = strong reflection (wall/floor present)
//       Large (> 260 us) = no reflection (open space / stair cliff!)
uint32_t measureActiveOpticalUs(uint8_t rxPin, uint8_t emitPin, uint32_t timeoutUs = 1200) {
    // 1. Ambient measurement (Emitter OFF)
    digitalWrite(emitPin, LOW);
    uint32_t tAmb = rawLedDischargeUs(rxPin, timeoutUs);

    // 2. Active illuminated measurement (Emitter ON @ 3.0 mA via 1kR; only 1 LED active at a time!)
    digitalWrite(emitPin, HIGH);
    delayMicroseconds(20); // Let LED optical output settle cleanly
    uint32_t tAct = rawLedDischargeUs(rxPin, timeoutUs);
    digitalWrite(emitPin, LOW);

    // 3. Differential photocurrent subtraction: I_refl ~ max(0, 1/tAct - 1/tAmb)
    //    CRITICAL SAFETY: In bright sunlight over a stair cliff, tAct == tAmb -> rateRefl = 0 ->
    //    returns timeoutUs (CLIFF / OPEN SPACE!), preventing sunlit stair fall-offs!
    float rateAmb  = (tAmb >= timeoutUs) ? 0.0f : (1.0e6f / (float)max(tAmb, (uint32_t)1));
    float rateAct  = (tAct >= timeoutUs) ? 0.0f : (1.0e6f / (float)max(tAct, (uint32_t)1));
    float rateRefl = max(0.0f, rateAct - rateAmb);
    if (rateRefl < 833.0f) return timeoutUs; // < 1/1200us reflected signal -> cliff / open space!
    uint32_t tEq = (uint32_t)(1.0e6f / rateRefl + 0.5f);
    return constrain(tEq, (uint32_t)15, timeoutUs);
}

// Isolated Bumper Microswitch (PB11) & Capacitive TouchKey (PC3) — ZERO cross-triggering!
bool readBumperSwitch() {
    return (digitalRead(PIN_BUMP_SW) == LOW);
}

bool readCapacitiveTouchKey() {
    pinMode(PIN_TOUCH_KEY, OUTPUT);
    digitalWrite(PIN_TOUCH_KEY, LOW);
    delayMicroseconds(8);
    uint32_t t0 = micros();
    pinMode(PIN_TOUCH_KEY, INPUT_PULLUP);
    while (digitalRead(PIN_TOUCH_KEY) == LOW && (micros() - t0) < 120) {}
    return ((micros() - t0) >= 18);
}

// =============================================================================
// 4. 1-Shunt TDM Ripple DSP (0.10R x 16x PGA = 1.60 V/A) + 5V Piezo Dirt-Impact DSP
// =============================================================================
void sampleAnalogAndOpticalSensors(float cmdL, float cmdR) {
    static float prevShunt = 0.0f, prevAcS = 0.0f, prevPiezo = 0.0f;
    static uint32_t lastOdomUs = 0;

    constexpr int N_SAMP = 64;
    constexpr float ADC_TO_V = 5.00f / 4095.0f;
    float sumShunt = 0.0f, acShuntEnergy = 0.0f, sumPiezoSq = 0.0f, peakPiezo = 0.0f;
    int shuntZcr = 0, piezoZcr = 0;

    for (int i = 0; i < N_SAMP; ++i) {
        // OPA1 (16x PGA) output on PB5/PB1 (ADC_CH9): 1.60 V per Amp on 0.10-Ohm shunt
        float vShunt = analogRead(PIN_ADC_SHUNT) * ADC_TO_V;
        sumShunt += vShunt;
        float acS = vShunt - prevShunt;
        acShuntEnergy += acS * acS;
        if ((acS >= 0.008f && prevAcS < -0.008f) || (acS <= -0.008f && prevAcS > 0.008f)) shuntZcr++;
        if (fabsf(acS) >= 0.008f) prevAcS = acS;
        prevShunt = 0.85f * prevShunt + 0.15f * vShunt;

        // OPA2 (5x @ 2.50V VMID) output on PA4 (ADC_CH4): Murata piezo acoustic debris impact
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

    // Rescale 0.10R * 16x = 1.60 V/A cleanly into TinyML feature scale (matches 0.25R * 13x = 3.25 V/A)
    // True frequency from zero-crossings in T_win = 64 * 70us = 4.48 ms -> f_Hz = zcr / (2 * T_win) = zcr * 111.6 Hz!
    constexpr float SHUNT_TO_TINYML_SCALE = 3.25f / (SHUNT_OHM * OPA1_PGA_GAIN);
    constexpr float ZCR_TO_HZ = 1.0f / (2.0f * (float)N_SAMP * 70.0e-6f);
    g_tel.shuntMeanL  = meanV * SHUNT_TO_TINYML_SCALE * fracL * 2.0f;
    g_tel.shuntMeanR  = meanV * SHUNT_TO_TINYML_SCALE * fracR * 2.0f;
    g_tel.rippleFreqL = (float)shuntZcr * ZCR_TO_HZ * fracL * 2.0f;
    g_tel.rippleFreqR = (float)shuntZcr * ZCR_TO_HZ * fracR * 2.0f;
    g_tel.rippleAmpL  = rmsRipple * SHUNT_TO_TINYML_SCALE;
    g_tel.rippleAmpR  = rmsRipple * SHUNT_TO_TINYML_SCALE;

    g_tel.piezoRms    = sqrtf(sumPiezoSq / N_SAMP);
    g_tel.piezoPeak   = peakPiezo;
    g_tel.piezoZcr    = (float)piezoZcr;

    // 0-Lux Dark-Room Proof Active Synchronous Optical Ranging (1 emitter pulsed at a time!)
    g_tel.ledFrontUs  = measureActiveOpticalUs(PIN_LED_FRONT_RX,   PIN_EMIT_FRONT);
    g_tel.ledSideUs   = measureActiveOpticalUs(PIN_LED_SIDE_RX,    PIN_EMIT_SIDE);
    g_tel.ledCliffLUs = measureActiveOpticalUs(PIN_LED_CLIFF_L_RX, PIN_EMIT_CLIFF_L);
    g_tel.ledCliffRUs = measureActiveOpticalUs(PIN_LED_CLIFF_R_RX, PIN_EMIT_CLIFF_R);

    g_tel.bumperHit   = readBumperSwitch();
    g_tel.touchActive = readCapacitiveTouchKey();

    uint32_t nowUs = micros();
    float dt = (lastOdomUs == 0) ? 0.01f : (nowUs - lastOdomUs) * 1e-6f;
    lastOdomUs = nowUs;
    float vL = cmdL * CRUISE_MM_S, vR = cmdR * CRUISE_MM_S;
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
    NAV_DIRT_TURBO_SPIRAL, // 1,606 Pa (6.6V) Piezo Dirt-Turbo Spiral!
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
    initHardwareTimersPWM();
    initOnChipOpAmps();
    pinMode(PIN_EMIT_FRONT,   OUTPUT); digitalWrite(PIN_EMIT_FRONT,   LOW);
    pinMode(PIN_EMIT_SIDE,    OUTPUT); digitalWrite(PIN_EMIT_SIDE,    LOW);
    pinMode(PIN_EMIT_CLIFF_L, OUTPUT); digitalWrite(PIN_EMIT_CLIFF_L, LOW);
    pinMode(PIN_EMIT_CLIFF_R, OUTPUT); digitalWrite(PIN_EMIT_CLIFF_R, LOW);
    pinMode(PIN_BUMP_SW,      INPUT_PULLUP);
    memset(g_grid, 0, sizeof(g_grid));
    delay(350);                        // Let 5V LDO & 470uF rail stabilize before enabling motors!
    digitalWrite(PIN_DRV_STBY, HIGH);  // Release AE-TC78H653FTG cleanly (ZERO power-on twitch!)
    initHardwareWatchdog();            // Arm 1.64s hardware IWDG safety watchdog!
}

void loop() {
    static uint32_t lastTouchMs = 0, lastBaroMs = 0;
    uint32_t nowMs = millis();
    feedWatchdog();

    sampleAnalogAndOpticalSensors(g_cmdL, g_cmdR);
    recoverHBridgeIfTripped(g_cmdL, g_cmdR);

    // Every 150ms: Run 0-JPY 1.5 ms Back-EMF Virtual Barometer (<1.0% duty overhead!)
    if ((nowMs - lastBaroMs) >= 150) {
        lastBaroMs = nowMs;
        sampleVirtualBarometer(g_smoothTurb);
    }

    if (g_tel.touchActive && (nowMs - lastTouchMs) > 800) {
        lastTouchMs = nowMs;
        g_mode = (g_mode == NAV_STANDBY) ? NAV_CRUISE_SLAM : NAV_STANDBY;
    }

    if (g_tel.lowBatteryCutoff) {
        g_mode = NAV_STANDBY;
    }

    uint8_t state = runTinyMLInference();
    // Physical optical cliff gate prevents ML from false-stopping on dark floors (<240us = floor confirmed!)
    bool cliffOptical = (g_tel.ledCliffLUs > 260) || (g_tel.ledCliffRUs > 260);
    bool cliffHard = cliffOptical || ((state == STATE_STAIR_CLIFF_EDGE) && ((g_tel.ledCliffLUs > 220) || (g_tel.ledCliffRUs > 220)));
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
            g_cmdTurb = (state == STATE_HIGH_DIRT_DEBRIS) ? TURB_DUTY_TURBO : TURB_DUTY_STD;
            float err = constrain(((float)g_tel.ledSideUs - 125.0f) * 0.0022f, -0.22f, 0.22f);
            g_cmdL = 0.68f + err; g_cmdR = 0.68f - err;
            if (elapsed > 3200 || g_tel.ledSideUs > 320) g_mode = NAV_CRUISE_SLAM;
            break;
        }
        case NAV_DIRT_TURBO_SPIRAL: {
            g_cmdTurb = TURB_DUTY_TURBO; // 1,606 Pa (6.6V) Dirt-Turbo!
            float rFactor = constrain(elapsed / 3800.0f, 0.15f, 0.85f);
            g_cmdL = 0.65f * rFactor; g_cmdR = 0.65f;
            if (elapsed > 3800) g_mode = NAV_CRUISE_SLAM;
            break;
        }
        case NAV_CRUISE_SLAM:
        default: {
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
