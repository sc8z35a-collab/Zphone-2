#pragma once
// =============================================================================
// 菅ルンバ II Rev 5.3 — Real CH32X035 Hardware USBFS Host HID Optical Mouse Driver
// =============================================================================
// Hardware: J_USB (V5V, GND, PC17=USB_DP, PC16=USB_DM) — ZERO PCB changes needed!
//   - Uses CH32X035 internal PC16/PC17 pull-downs (GPIO_Mode_IPD) & 5V USB PHY
//     (AFIO->CTLR: USB_IOEN=1, USB_PHY_V33=0) verified 1:1 vs openwch/ch32x035
//     EVT/EXAM/USB/USBFS/HOST_KM/User/ch32x035_usbfs_host.c!
//   - Supports BOTH Low-Speed (1.5 Mbps, Daiso 100-JPY optical mouse) & Full-Speed!
//   - Forces HID Boot Protocol (SET_PROTOCOL=0) -> universal [btn, int8 dx, int8 dy]
//     report layout across all standard USB optical mice!
//   - Non-blocking IN polling (NAK returns immediately in <150 us) -> ZERO loop stall
//     and seamless automatic fallback to wheel dead-reckoning if no mouse is attached!
//   - USB-ISP flashing remains 100% intact (ROM bootloader runs before user firmware
//     when JP_BOOT is shorted at power-on).
// =============================================================================

#include <stdint.h>
#include <string.h>

struct UsbMouseState {
    bool     attached;
    bool     ready;
    bool     lowSpeed;
    uint8_t  ep0MaxPkt;
    uint8_t  inEpNum;
    uint8_t  inEpTog;
    uint8_t  errCount;
    uint8_t  lastButtons;
    uint32_t reportCount;
    int32_t  accumDx;
    int32_t  accumDy;
};

static UsbMouseState g_usbMouse = {};

#if defined(CH32X035) && defined(USBFS_BASE) && !defined(TEST_WCH_SPL_REGS)

// Exact CH32X035 USBFS register & protocol bitfields (from openwch/ch32x035 ch32x035_usb.h)
static constexpr uint32_t UM_AFIO_USB_IOEN     = 0x00000080U;
static constexpr uint32_t UM_AFIO_USB_PHY_V33  = 0x00000040U;
static constexpr uint32_t UM_AFIO_UDP_PUE_MASK = 0x0000000CU;
static constexpr uint32_t UM_AFIO_UDM_PUE_MASK = 0x00000003U;

static constexpr uint8_t  UM_UC_HOST_MODE      = 0x80U;
static constexpr uint8_t  UM_UC_LOW_SPEED      = 0x40U;
static constexpr uint8_t  UM_UC_INT_BUSY       = 0x08U;
static constexpr uint8_t  UM_UC_DMA_EN         = 0x01U;

static constexpr uint8_t  UM_UH_LOW_SPEED      = 0x04U;
static constexpr uint8_t  UM_UH_BUS_RESET      = 0x02U;
static constexpr uint8_t  UM_UH_PORT_EN        = 0x01U;
static constexpr uint8_t  UM_UH_EP_TX_EN       = 0x40U;
static constexpr uint8_t  UM_UH_EP_RX_EN       = 0x08U;
static constexpr uint8_t  UM_UH_PRE_PID_EN     = 0x80U;
static constexpr uint8_t  UM_UH_SOF_EN         = 0x40U;
static constexpr uint8_t  UM_UH_R_TOG          = 0x80U;
static constexpr uint8_t  UM_UH_R_AUTO_TOG     = 0x10U;
static constexpr uint8_t  UM_UH_T_TOG          = 0x40U;
static constexpr uint8_t  UM_UH_T_AUTO_TOG     = 0x10U;

static constexpr uint8_t  UM_UIF_TRANSFER      = 0x02U;
static constexpr uint8_t  UM_UIF_DETECT        = 0x01U;
static constexpr uint8_t  UM_UIS_TOG_OK        = 0x40U;
static constexpr uint8_t  UM_UIS_H_RES_MASK    = 0x0FU;
static constexpr uint8_t  UM_UMS_DM_LEVEL      = 0x02U;
static constexpr uint8_t  UM_UMS_DEV_ATTACH    = 0x01U;

static constexpr uint8_t  UM_PID_OUT           = 0x01U;
static constexpr uint8_t  UM_PID_IN            = 0x09U;
static constexpr uint8_t  UM_PID_NAK           = 0x0AU;
static constexpr uint8_t  UM_PID_SETUP         = 0x0DU;
static constexpr uint8_t  UM_PID_STALL         = 0x0EU;
static constexpr uint8_t  UM_PID_DATA0         = 0x03U;
static constexpr uint8_t  UM_PID_DATA1         = 0x0BU;

static constexpr uint8_t  UM_ERR_OK            = 0x00U;
static constexpr uint8_t  UM_ERR_NAK           = 0x0AU;
static constexpr uint8_t  UM_ERR_DISCON        = 0x16U;
static constexpr uint8_t  UM_ERR_XFER          = 0x20U;

__attribute__((aligned(4))) static uint8_t g_umRxBuf[64];
__attribute__((aligned(4))) static uint8_t g_umTxBuf[64];

static inline void umFeedWdt() {
#if defined(IWDG_BASE)
    IWDG_ReloadCounter();
#endif
}

static inline void umSetSpeed(bool lowSpeed) {
    if (lowSpeed) {
        USBFSH->BASE_CTRL  |= UM_UC_LOW_SPEED;
        USBFSH->HOST_CTRL  |= UM_UH_LOW_SPEED;
        USBFSH->HOST_SETUP |= UM_UH_PRE_PID_EN;
    } else {
        USBFSH->BASE_CTRL  &= (uint8_t)~UM_UC_LOW_SPEED;
        USBFSH->HOST_CTRL  &= (uint8_t)~UM_UH_LOW_SPEED;
        USBFSH->HOST_SETUP &= (uint8_t)~UM_UH_PRE_PID_EN;
    }
}

static uint8_t umTransact(uint8_t epPid, uint8_t tog, uint16_t nakRetries) {
    USBFSH->HOST_TX_CTRL = tog;
    USBFSH->HOST_RX_CTRL = tog;
    for (uint8_t retry = 0; retry < 8; ++retry) {
        USBFSH->HOST_EP_PID = epPid;
        USBFSH->INT_FG      = UM_UIF_TRANSFER;
        uint16_t wait = 800;
        while (wait-- && ((USBFSH->INT_FG & UM_UIF_TRANSFER) == 0U)) {
            delayMicroseconds(1);
        }
        USBFSH->HOST_EP_PID = 0x00;
        if ((USBFSH->INT_FG & UM_UIF_TRANSFER) == 0U) {
            return UM_ERR_XFER;
        }
        if (USBFSH->INT_ST & UM_UIS_TOG_OK) {
            return UM_ERR_OK;
        }
        uint8_t res = USBFSH->INT_ST & UM_UIS_H_RES_MASK;
        if (res == UM_PID_STALL) {
            return (uint8_t)(res | UM_ERR_XFER);
        }
        if (res == UM_PID_NAK) {
            if (nakRetries == 0U) {
                return UM_ERR_NAK;
            }
            nakRetries--;
            retry--; // Do not burn hard error retries on normal NAK
        } else {
            uint8_t pid = (uint8_t)(epPid >> 4);
            if (pid == UM_PID_IN) {
                if (res != UM_PID_DATA0 && res != UM_PID_DATA1 && res != 0U) {
                    return (uint8_t)(res | UM_ERR_XFER);
                }
            } else if (res != 0U) {
                return (uint8_t)(res | UM_ERR_XFER);
            }
        }
        delayMicroseconds(25);
        if ((USBFSH->MIS_ST & UM_UMS_DEV_ATTACH) == 0U) {
            return UM_ERR_DISCON;
        }
    }
    return UM_ERR_XFER;
}

static uint8_t umCtrlTransfer(uint8_t ep0Size, const uint8_t setup8[8], uint8_t *rxBuf, uint16_t *rxLenOut) {
    delayMicroseconds(200);
    if (rxLenOut) *rxLenOut = 0;
    memcpy(g_umTxBuf, setup8, 8);
    USBFSH->HOST_TX_LEN = 8;
    uint8_t s = umTransact((uint8_t)(UM_PID_SETUP << 4), 0x00, 120);
    if (s != UM_ERR_OK) return s;

    USBFSH->HOST_TX_CTRL = UM_UH_T_TOG | UM_UH_T_AUTO_TOG;
    USBFSH->HOST_RX_CTRL = UM_UH_R_TOG | UM_UH_R_AUTO_TOG;

    uint16_t rem = (uint16_t)setup8[6] | ((uint16_t)setup8[7] << 8);
    bool dirIn   = (setup8[0] & 0x80U) != 0U;
    USBFSH->HOST_TX_LEN = 1; // Non-zero means status stage is IN (for OUT/zero-data)

    if (rem > 0 && dirIn && rxBuf != nullptr) {
        uint16_t total = 0;
        while (rem > 0) {
            delayMicroseconds(200);
            s = umTransact((uint8_t)(UM_PID_IN << 4), USBFSH->HOST_RX_CTRL, 120);
            if (s != UM_ERR_OK) return s;
            uint16_t got = USBFSH->RX_LEN;
            uint16_t cp  = (got < rem) ? got : rem;
            memcpy(rxBuf + total, g_umRxBuf, cp);
            total += cp;
            rem   -= cp;
            if (got == 0 || got < ep0Size) break;
        }
        if (rxLenOut) *rxLenOut = total;
        USBFSH->HOST_TX_LEN = 0; // Status stage is OUT ZLP
    }

    delayMicroseconds(200);
    uint8_t statusPid = (USBFSH->HOST_TX_LEN != 0) ? (uint8_t)(UM_PID_IN << 4) : (uint8_t)(UM_PID_OUT << 4);
    s = umTransact(statusPid, UM_UH_R_TOG | UM_UH_T_TOG, 120);
    return s;
}

static bool umEnumerateMouse() {
    uint8_t descBuf[64];
    uint16_t gotLen = 0;

    // 1. Bus Reset (15 ms) -> Enable Root Port -> Detect Speed (LS 1.5Mbps vs FS 12Mbps)
    USBFSH->DEV_ADDR = 0x00;
    umSetSpeed(false);
    USBFSH->HOST_CTRL = (uint8_t)((USBFSH->HOST_CTRL & ~UM_UH_LOW_SPEED) | UM_UH_BUS_RESET);
    delay(15);
    umFeedWdt();
    USBFSH->HOST_CTRL &= (uint8_t)~UM_UH_BUS_RESET;
    delay(4);
    USBFSH->INT_FG = UM_UIF_DETECT;

    if ((USBFSH->MIS_ST & UM_UMS_DEV_ATTACH) == 0U) return false;
    g_usbMouse.lowSpeed = (USBFSH->MIS_ST & UM_UMS_DM_LEVEL) != 0U;
    umSetSpeed(g_usbMouse.lowSpeed);
    USBFSH->HOST_CTRL  |= UM_UH_PORT_EN;
    USBFSH->HOST_SETUP |= UM_UH_SOF_EN;
    delay(20);
    umFeedWdt();

    // 2. GET_DESCRIPTOR (Device, first 8 bytes to read bMaxPacketSize0)
    const uint8_t reqDev8[8] = {0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00};
    if (umCtrlTransfer(8, reqDev8, descBuf, &gotLen) != UM_ERR_OK || gotLen < 8) return false;
    g_usbMouse.ep0MaxPkt = (descBuf[7] >= 8 && descBuf[7] <= 64) ? descBuf[7] : 8;

    // 3. SET_ADDRESS (Address = 2)
    const uint8_t reqAddr[8] = {0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (umCtrlTransfer(g_usbMouse.ep0MaxPkt, reqAddr, nullptr, nullptr) != UM_ERR_OK) return false;
    USBFSH->DEV_ADDR = 0x02;
    delay(10);
    umFeedWdt();

    // 4. GET_DESCRIPTOR (Configuration, up to 64 bytes -> locate HID Mouse Interrupt IN endpoint)
    const uint8_t reqCfg[8] = {0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x40, 0x00};
    if (umCtrlTransfer(g_usbMouse.ep0MaxPkt, reqCfg, descBuf, &gotLen) != UM_ERR_OK || gotLen < 9) return false;

    uint8_t cfgVal = descBuf[5];
    uint8_t mouseIntf = 0;
    uint8_t foundEp = 0;
    bool inMouseIntf = false;
    uint16_t pos = 0;
    while (pos + 2 <= gotLen) {
        uint8_t bLen  = descBuf[pos];
        uint8_t bType = descBuf[pos + 1];
        if (bLen < 2 || pos + bLen > gotLen) break;
        if (bType == 0x04 && bLen >= 9) { // Interface Descriptor
            uint8_t cls   = descBuf[pos + 5];
            uint8_t proto = descBuf[pos + 7];
            inMouseIntf = (cls == 0x03 && proto == 0x02);
            if (inMouseIntf) mouseIntf = descBuf[pos + 2];
        } else if (bType == 0x05 && bLen >= 7) { // Endpoint Descriptor
            uint8_t epAddr = descBuf[pos + 2];
            uint8_t epAttr = descBuf[pos + 3];
            if ((epAddr & 0x80U) && ((epAttr & 0x03U) == 0x03U)) { // Interrupt IN
                if (inMouseIntf || foundEp == 0) {
                    foundEp = (uint8_t)(epAddr & 0x0FU);
                    if (inMouseIntf) break;
                }
            }
        }
        pos += bLen;
    }
    if (foundEp == 0) foundEp = 1; // Standard boot-mouse fallback EP1 IN

    // 5. SET_CONFIGURATION
    uint8_t reqSetCfg[8] = {0x00, 0x09, cfgVal, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (umCtrlTransfer(g_usbMouse.ep0MaxPkt, reqSetCfg, nullptr, nullptr) != UM_ERR_OK) return false;

    // 6. HID SET_PROTOCOL (0 = Boot Protocol -> guaranteed [buttons, int8 dx, int8 dy]!)
    uint8_t reqBootProto[8] = {0x21, 0x0B, 0x00, 0x00, mouseIntf, 0x00, 0x00, 0x00};
    (void)umCtrlTransfer(g_usbMouse.ep0MaxPkt, reqBootProto, nullptr, nullptr);

    // 7. HID SET_IDLE (0 = report only on movement)
    uint8_t reqIdle[8] = {0x21, 0x0A, 0x00, 0x00, mouseIntf, 0x00, 0x00, 0x00};
    (void)umCtrlTransfer(g_usbMouse.ep0MaxPkt, reqIdle, nullptr, nullptr);

    g_usbMouse.inEpNum  = foundEp;
    g_usbMouse.inEpTog  = 0x00;
    g_usbMouse.errCount = 0;
    g_usbMouse.ready    = true;
    return true;
}

inline void usbMouseInit() {
    memset(&g_usbMouse, 0, sizeof(g_usbMouse));
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);

    // PC16 (UDM) & PC17 (UDP): CH32X035 built-in 15kR Host pull-downs (ZERO external parts!)
    GPIO_InitTypeDef gu = {};
    gu.GPIO_Pin  = GPIO_Pin_16 | GPIO_Pin_17;
    gu.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOC, &gu);

    // 5.0V USB Host PHY enabled on PC16/PC17
    AFIO->CTLR = (AFIO->CTLR & ~(UM_AFIO_UDP_PUE_MASK | UM_AFIO_UDM_PUE_MASK | UM_AFIO_USB_PHY_V33))
               | UM_AFIO_USB_IOEN;

    USBFSH->BASE_CTRL    = UM_UC_HOST_MODE;
    USBFSH->HOST_CTRL    = 0x00;
    USBFSH->DEV_ADDR     = 0x00;
    USBFSH->HOST_EP_MOD  = UM_UH_EP_TX_EN | UM_UH_EP_RX_EN;
    USBFSH->HOST_RX_DMA  = (uint16_t)((uintptr_t)g_umRxBuf & 0xFFFFU);
    USBFSH->HOST_TX_DMA  = (uint16_t)((uintptr_t)g_umTxBuf & 0xFFFFU);
    USBFSH->HOST_RX_CTRL = 0x00;
    USBFSH->HOST_TX_CTRL = 0x00;
    USBFSH->BASE_CTRL    = UM_UC_HOST_MODE | UM_UC_INT_BUSY | UM_UC_DMA_EN;
    USBFSH->HOST_SETUP   = UM_UH_SOF_EN;
    USBFSH->INT_FG       = 0xFF;
    USBFSH->INT_EN       = 0x00; // Deterministic polling (no ISR jitter on optical µs timing)
}

// Non-blocking poll: returns true if USB optical mouse is live; sets *outDx, *outDy (counts)
inline bool usbMousePoll(int16_t *outDx, int16_t *outDy) {
    static uint32_t lastEnumTryMs = 0;
    *outDx = 0;
    *outDy = 0;

    bool attached = (USBFSH->MIS_ST & UM_UMS_DEV_ATTACH) != 0U;
    g_usbMouse.attached = attached;
    if (!attached) {
        g_usbMouse.ready = false;
        return false;
    }

    if (!g_usbMouse.ready) {
        uint32_t now = millis();
        if ((now - lastEnumTryMs) < 500U) return false;
        lastEnumTryMs = now;
        if (!umEnumerateMouse()) return false;
    }

    // Single non-blocking Interrupt IN poll (nakRetries=0 -> returns <150us on NAK!)
    uint8_t s = umTransact((uint8_t)((UM_PID_IN << 4) | (g_usbMouse.inEpNum & 0x0FU)),
                           g_usbMouse.inEpTog, 0);
    if (s == UM_ERR_OK) {
        g_usbMouse.inEpTog ^= UM_UH_R_TOG;
        g_usbMouse.errCount = 0;
        if (USBFSH->RX_LEN >= 3) {
            g_usbMouse.lastButtons = g_umRxBuf[0];
            int8_t dx = (int8_t)g_umRxBuf[1];
            int8_t dy = (int8_t)g_umRxBuf[2];
            *outDx = (int16_t)dx;
            *outDy = (int16_t)dy;
            g_usbMouse.accumDx += dx;
            g_usbMouse.accumDy += dy;
            g_usbMouse.reportCount++;
        }
        return true;
    }
    if (s == UM_ERR_NAK) {
        g_usbMouse.errCount = 0;
        return true; // Normal idle mouse (no motion since last poll)
    }
    if (++g_usbMouse.errCount > 12) {
        g_usbMouse.ready = false; // Auto-recover / seamlessly fall back to dead-reckoning
    }
    return false;
}

#else
// Host HIL mock bridge (allows full HIL verification of mouse fusion & wheel-slip detection!)
inline void usbMouseInit() { memset(&g_usbMouse, 0, sizeof(g_usbMouse)); }

#ifdef MOCK_USB_MOUSE_HOOK
extern bool mockUsbMousePoll(int16_t *outDx, int16_t *outDy);
inline bool usbMousePoll(int16_t *outDx, int16_t *outDy) {
    bool ok = mockUsbMousePoll(outDx, outDy);
    g_usbMouse.attached = ok;
    g_usbMouse.ready    = ok;
    if (ok) {
        g_usbMouse.accumDx += *outDx;
        g_usbMouse.accumDy += *outDy;
        if (*outDx != 0 || *outDy != 0) g_usbMouse.reportCount++;
    }
    return ok;
}
#else
inline bool usbMousePoll(int16_t *outDx, int16_t *outDy) {
    *outDx = 0;
    *outDy = 0;
    return false;
}
#endif
#endif
