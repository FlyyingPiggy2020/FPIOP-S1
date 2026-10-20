/*
 * CMSIS-DAP configuration for IO Probe on HPM5301EVKLite.
 * Based on the ARM template (third_party/cherrydap) and CherryDAP's HSLink-Lite config.
 *
 * SWD only, GPIO bit-bang on J3 header pins (see docs/pinout.md):
 *   SWCLK  = J3-11 PA31 (push-pull)
 *   SWDIO  = J3-29 PY00 (bidirectional, board pull-up)
 *   nRESET = J3-31 PY01 (open-drain emulated, board pull-up)
 * Pins are driven through the core-local fast GPIO (FGPIO) for speed.
 */
#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#include <stdint.h>
#include <string.h>
#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_gpiom_drv.h"
#include "version.h"

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#endif
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __NOP
#define __NOP() __asm volatile("nop")
#endif

/* Bit-bang loop calibration, not the real core clock: DAP.c derives SWCLK delays from it.
 * 36 MHz is what CherryDAP HSLink-Lite uses on HPM5301 with FGPIO; re-measure SWCLK if pins change. */
#define CPU_CLOCK            36000000U
#define IO_PORT_WRITE_CYCLES 1U

#define DAP_SWD               1
#define DAP_JTAG              0
#define DAP_JTAG_DEV_CNT      0U
#define DAP_DEFAULT_PORT      1U       /* SWD */
#define DAP_DEFAULT_SWJ_CLOCK 1000000U

#define DAP_PACKET_SIZE  512U /* USB high-speed bulk */
#define DAP_PACKET_COUNT 4U

#define SWO_UART        0
#define SWO_MANCHESTER  0
#define SWO_BUFFER_SIZE 4096U
#define SWO_STREAM      0
#define TIMESTAMP_CLOCK 0U

/* The target UART is exposed as its own USB CDC port, not through DAP UART commands. */
#define DAP_UART                0
#define DAP_UART_RX_BUFFER_SIZE 1024U
#define DAP_UART_TX_BUFFER_SIZE 1024U
#define DAP_UART_USB_COM_PORT   0

#define TARGET_FIXED 0

#define PIN_SWCLK  IOC_PAD_PA31
#define PIN_SWDIO  IOC_PAD_PY00
#define PIN_nRESET IOC_PAD_PY01
#define PIN_GPIO   HPM_FGPIO

#define PIN_PORT(pad) GPIO_GET_PORT_INDEX(pad)
#define PIN_IDX(pad)  GPIO_GET_PIN_INDEX(pad)

/* Strings: tools read vendor/product/serial from the USB descriptors when these return 0. */
__STATIC_INLINE uint8_t DAP_GetVendorString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetProductString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetSerNumString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString(char *str) { (void)str; return 0U; }

#define DAP_STR_(x) #x
#define DAP_STR(x)  DAP_STR_(x)
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString(char *str)
{
    static const char version[] =
        DAP_STR(FW_VERSION_MAJOR) "." DAP_STR(FW_VERSION_MINOR) "." DAP_STR(FW_VERSION_PATCH);
    memcpy(str, version, sizeof(version));
    return (uint8_t)sizeof(version);
}

/* Route a pad to GPIO, owned by the fast GPIO controller. PY pads also go through the power-domain IOC. */
__STATIC_INLINE void dap_pin_to_fgpio(uint16_t pad, uint32_t pad_ctl)
{
    if (pad == IOC_PAD_PY00) {
        HPM_PIOC->PAD[pad].FUNC_CTL = PIOC_PY00_FUNC_CTL_SOC_GPIO_Y_00;
    } else if (pad == IOC_PAD_PY01) {
        HPM_PIOC->PAD[pad].FUNC_CTL = PIOC_PY01_FUNC_CTL_SOC_GPIO_Y_01;
    }
    HPM_IOC->PAD[pad].FUNC_CTL = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(0) | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[pad].PAD_CTL = pad_ctl;
    gpiom_set_pin_controller(HPM_GPIOM, PIN_PORT(pad), PIN_IDX(pad), gpiom_core0_fast);
    gpiom_enable_pin_visibility(HPM_GPIOM, PIN_PORT(pad), PIN_IDX(pad), gpiom_core0_fast);
}

/* JTAG is not built (DAP_JTAG = 0); kept so DAP.c links if it references the setup. */
__STATIC_INLINE void PORT_JTAG_SETUP(void) {}

/* SWCLK and SWDIO driven high, nRESET released. */
__STATIC_INLINE void PORT_SWD_SETUP(void)
{
    dap_pin_to_fgpio(PIN_SWCLK, IOC_PAD_PAD_CTL_SPD_SET(3));
    dap_pin_to_fgpio(PIN_SWDIO, IOC_PAD_PAD_CTL_SPD_SET(3));
    dap_pin_to_fgpio(PIN_nRESET, 0);
    gpio_set_pin_output_with_initial(PIN_GPIO, PIN_PORT(PIN_SWCLK), PIN_IDX(PIN_SWCLK), 1);
    gpio_set_pin_output_with_initial(PIN_GPIO, PIN_PORT(PIN_SWDIO), PIN_IDX(PIN_SWDIO), 1);
    gpio_set_pin_input(PIN_GPIO, PIN_PORT(PIN_nRESET), PIN_IDX(PIN_nRESET));
}

/* All debug pins high-impedance. */
__STATIC_INLINE void PORT_OFF(void)
{
    gpio_set_pin_input(PIN_GPIO, PIN_PORT(PIN_SWCLK), PIN_IDX(PIN_SWCLK));
    gpio_set_pin_input(PIN_GPIO, PIN_PORT(PIN_SWDIO), PIN_IDX(PIN_SWDIO));
    gpio_set_pin_input(PIN_GPIO, PIN_PORT(PIN_nRESET), PIN_IDX(PIN_nRESET));
}

__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void)
{
    return gpio_read_pin(PIN_GPIO, PIN_PORT(PIN_SWCLK), PIN_IDX(PIN_SWCLK));
}

__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void)
{
    PIN_GPIO->DO[PIN_PORT(PIN_SWCLK)].SET = 1UL << PIN_IDX(PIN_SWCLK);
}

__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void)
{
    PIN_GPIO->DO[PIN_PORT(PIN_SWCLK)].CLEAR = 1UL << PIN_IDX(PIN_SWCLK);
}

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void)
{
    return gpio_read_pin(PIN_GPIO, PIN_PORT(PIN_SWDIO), PIN_IDX(PIN_SWDIO));
}

__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void)
{
    PIN_GPIO->DO[PIN_PORT(PIN_SWDIO)].SET = 1UL << PIN_IDX(PIN_SWDIO);
}

__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void)
{
    PIN_GPIO->DO[PIN_PORT(PIN_SWDIO)].CLEAR = 1UL << PIN_IDX(PIN_SWDIO);
}

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void)
{
    return (PIN_GPIO->DI[PIN_PORT(PIN_SWDIO)].VALUE >> PIN_IDX(PIN_SWDIO)) & 1UL;
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit)
{
    if (bit & 1U) {
        PIN_GPIO->DO[PIN_PORT(PIN_SWDIO)].SET = 1UL << PIN_IDX(PIN_SWDIO);
    } else {
        PIN_GPIO->DO[PIN_PORT(PIN_SWDIO)].CLEAR = 1UL << PIN_IDX(PIN_SWDIO);
    }
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void)
{
    PIN_GPIO->OE[PIN_PORT(PIN_SWDIO)].SET = 1UL << PIN_IDX(PIN_SWDIO);
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void)
{
    PIN_GPIO->OE[PIN_PORT(PIN_SWDIO)].CLEAR = 1UL << PIN_IDX(PIN_SWDIO);
}

/* JTAG-only pins: not wired. */
__STATIC_FORCEINLINE uint32_t PIN_TDI_IN(void) { return 0U; }
__STATIC_FORCEINLINE void PIN_TDI_OUT(uint32_t bit) { (void)bit; }
__STATIC_FORCEINLINE uint32_t PIN_TDO_IN(void) { return 0U; }
__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN(void) { return 0U; }
__STATIC_FORCEINLINE void PIN_nTRST_OUT(uint32_t bit) { (void)bit; }

__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void)
{
    return gpio_read_pin(PIN_GPIO, PIN_PORT(PIN_nRESET), PIN_IDX(PIN_nRESET));
}

/* bit 0 = assert reset (drive low), 1 = release (input, the board pull-up takes it high). */
__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit)
{
    if (bit & 1U) {
        gpio_set_pin_input(PIN_GPIO, PIN_PORT(PIN_nRESET), PIN_IDX(PIN_nRESET));
    } else {
        gpio_set_pin_output_with_initial(PIN_GPIO, PIN_PORT(PIN_nRESET), PIN_IDX(PIN_nRESET), 0);
    }
}

__STATIC_INLINE void LED_CONNECTED_OUT(uint32_t bit) { (void)bit; }
__STATIC_INLINE void LED_RUNNING_OUT(uint32_t bit) { (void)bit; }

__STATIC_INLINE uint32_t TIMESTAMP_GET(void) { return 0U; }

/* Called once from DAP_Setup(): leave the debug pins released until a debugger connects. */
__STATIC_INLINE void DAP_SETUP(void)
{
    PORT_SWD_SETUP();
    PORT_OFF();
}

/* No device-specific reset sequence. */
__STATIC_INLINE uint8_t RESET_TARGET(void)
{
    return 0U;
}

#endif
