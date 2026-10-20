/*
 * SWD backend dispatch. DAP.c calls SWJ_Sequence / SWD_Sequence / SWD_Transfer and the weak
 * Set_Clock_Delay(); these route to the SPI backend (default) or the GPIO bit-bang fallback.
 */
#include "DAP_config.h"
#include "DAP.h"
#include "sw_dp_mode.h"

static swd_mode_t selected = SWD_MODE_SPI; /* applied at the next connect */
static swd_mode_t active = SWD_MODE_GPIO;  /* backend the pins are set up for */
static uint32_t swj_clock_hz = DAP_DEFAULT_SWJ_CLOCK;

/* Reference bit-bang delay calculation (same as the weak default in DAP.c). */
static void io_set_clock_delay(uint32_t clock)
{
    uint32_t delay;

    if (clock >= ((CPU_CLOCK / 2U) / (IO_PORT_WRITE_CYCLES + DELAY_FAST_CYCLES))) {
        DAP_Data.fast_clock = 1U;
        DAP_Data.clock_delay = 1U;
    } else {
        DAP_Data.fast_clock = 0U;
        delay = ((CPU_CLOCK / 2U) + (clock - 1U)) / clock;
        if (delay > IO_PORT_WRITE_CYCLES) {
            delay -= IO_PORT_WRITE_CYCLES;
            delay = (delay + (DELAY_SLOW_CYCLES - 1U)) / DELAY_SLOW_CYCLES;
        } else {
            delay = 1U;
        }
        DAP_Data.clock_delay = delay;
    }
}

void Set_Clock_Delay(uint32_t clock)
{
    swj_clock_hz = clock;
    io_set_clock_delay(clock); /* keep the fallback's timing current too */
    if (active == SWD_MODE_SPI) {
        SPI_Set_Clock(clock);
    }
}

swd_mode_t swd_set_mode(swd_mode_t mode)
{
    selected = (mode == SWD_MODE_GPIO) ? SWD_MODE_GPIO : SWD_MODE_SPI;
    return selected;
}

swd_mode_t swd_get_mode(void)
{
    return selected;
}

void swd_port_setup(void)
{
    active = selected;
    if (active == SWD_MODE_SPI) {
        SPI_PORT_SWD_SETUP();
        SPI_Set_Clock(swj_clock_hz);
    } else {
        dap_gpio_port_setup();
    }
    dap_nreset_setup();
}

void swd_port_off(void)
{
    dap_gpio_port_off(); /* back to GPIO inputs whichever backend was active */
    active = SWD_MODE_GPIO;
}

void SWJ_Sequence(uint32_t count, const uint8_t *data)
{
    if (active == SWD_MODE_SPI) {
        SPI_SWJ_Sequence(count, data);
    } else {
        IO_SWJ_Sequence(count, data);
    }
}

void SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi)
{
    if (active == SWD_MODE_SPI) {
        SPI_SWD_Sequence(info, swdo, swdi);
    } else {
        IO_SWD_Sequence(info, swdo, swdi);
    }
}

uint8_t SWD_Transfer(uint32_t request, uint32_t *data)
{
    return (active == SWD_MODE_SPI) ? SPI_SWD_Transfer(request, data) : IO_SWD_Transfer(request, data);
}
