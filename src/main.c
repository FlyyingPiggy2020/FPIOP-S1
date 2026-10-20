/*
 * IO Probe firmware: lets a host (and an AI behind it) drive I2C / SPI / GPIO over USB,
 * and doubles as a CMSIS-DAP (SWD) debugger with a target serial port.
 */
#include <stdio.h>
#include "board.h"
#include "hpm_ppor_drv.h"
#include "usb_config.h"
#include "pins.h"
#include "protocol.h"
#include "usb_link.h"
#include "dap_link.h"
#include "target_uart.h"
#include "version.h"

#define LED_FLASH_PERIOD_IN_MS 500

int main(void)
{
    static uint8_t rx[512];

    board_init();
    board_init_led_pins();
    board_timer_create(LED_FLASH_PERIOD_IN_MS, board_led_toggle);

    usb_link_init();
    pins_init();
    cmd_i2c_init();
    cmd_spi_init();

    printf("IO Probe fw %d.%d.%d, protocol v%d\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
           PROTO_VERSION);

    while (1) {
        dap_link_handle();
        target_uart_handle();

        uint32_t n = usb_link_read(rx, sizeof(rx));
        if (n) {
            proto_feed(rx, n);
        }
        if (g_reset_requested) {
            board_delay_ms(20); /* let the host read the ack */
            ppor_sw_reset(HPM_PPOR, 1000);
        }
    }
    return 0;
}
