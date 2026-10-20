/*
 * IO Probe pin table and pad helpers. Keep in sync with docs/pinout.md.
 */
#include <stdbool.h>
#include "board.h"
#include "hpm_gpio_drv.h"
#include "pins.h"

/* Not GPIO: J3-8/10 target UART, J3-27/28/29 SWD (src/dap/DAP_config.h),
 * J3-7 (manual and silkscreen say PB15, same as J3-8), J3-33 (shared with LED) and
 * J3-37 (PA30 = USB0_PWR, J1 VBUS control: never drive it). */
const probe_gpio_t probe_gpios[] = {
    { 11, IOC_PAD_PA31 },
    { 13, IOC_PAD_PB11 },
    { 31, IOC_PAD_PY01 }, /* board pull-up ("USB_OC"): reads 1 when floating */
    { 32, IOC_PAD_PA09 },
};
const uint8_t probe_gpio_count = sizeof(probe_gpios) / sizeof(probe_gpios[0]);

const probe_gpio_t *pins_find_gpio(uint8_t j3)
{
    for (uint8_t i = 0; i < probe_gpio_count; i++) {
        if (probe_gpios[i].j3 == j3) {
            return &probe_gpios[i];
        }
    }
    return NULL;
}

void pins_set_gpio_func(uint16_t pad, uint8_t pull, bool open_drain)
{
    uint32_t pad_ctl = IOC_PAD_PAD_CTL_OD_SET(open_drain ? 1 : 0);

    if (pull == 1) {
        pad_ctl |= IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    } else if (pull == 2) {
        pad_ctl |= IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(0);
    }

    /* GPIO is ALT0 on every pad. PY pads additionally route through the power-domain IOC. */
    if (pad == IOC_PAD_PY00) {
        HPM_PIOC->PAD[pad].FUNC_CTL = PIOC_PY00_FUNC_CTL_SOC_GPIO_Y_00;
    } else if (pad == IOC_PAD_PY01) {
        HPM_PIOC->PAD[pad].FUNC_CTL = PIOC_PY01_FUNC_CTL_SOC_GPIO_Y_01;
    }
    /* LOOP_BACK keeps the input buffer on while driving, so reads report the real pin level. */
    HPM_IOC->PAD[pad].FUNC_CTL = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(0) | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[pad].PAD_CTL = pad_ctl;
}

void pins_init(void)
{
    for (uint8_t i = 0; i < probe_gpio_count; i++) {
        uint16_t pad = probe_gpios[i].pad;

        pins_set_gpio_func(pad, 2, false);
        gpio_set_pin_input(HPM_GPIO0, GPIO_GET_PORT_INDEX(pad), GPIO_GET_PIN_INDEX(pad));
    }
}
