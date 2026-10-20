/*
 * SPI master commands. Bus 0 = SPI-A = SPI1 on J3-19 (MOSI) / 21 (MISO) / 23 (SCLK),
 * chip selects driven as GPIO: CS0 = J3-24, CS1 = J3-26 (active low).
 */
#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_spi_drv.h"
#include "protocol.h"
#include "pins.h"

#define SPI_BUS_COUNT       1
#define SPI_DEFAULT_FREQ_HZ 1000000UL

typedef struct {
    SPI_Type *base;
    uint32_t clk_hz;
    uint16_t cs_pads[2];
} spi_bus_t;

static spi_bus_t buses[SPI_BUS_COUNT] = {
    { .base = HPM_SPI1, .cs_pads = { PROBE_SPI_CS0_PAD, PROBE_SPI_CS1_PAD } },
};

static void cs_write(uint16_t pad, bool asserted)
{
    gpio_write_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(pad), GPIO_GET_PIN_INDEX(pad), asserted ? 0 : 1);
}

static proto_status_t bus_configure(spi_bus_t *bus, uint8_t mode, bool lsb_first, uint32_t freq_hz)
{
    spi_timing_config_t timing = { 0 };
    spi_format_config_t format = { 0 };

    spi_master_get_default_timing_config(&timing);
    timing.master_config.clk_src_freq_in_hz = bus->clk_hz;
    timing.master_config.sclk_freq_in_hz = freq_hz;
    if (spi_master_timing_init(bus->base, &timing) != status_success) {
        return ST_BAD_ARG;
    }

    spi_master_get_default_format_config(&format);
    format.common_config.data_len_in_bits = 8;
    format.common_config.mode = spi_master_mode;
    format.common_config.cpol = (mode & 0x2) ? spi_sclk_high_idle : spi_sclk_low_idle;
    format.common_config.cpha = (mode & 0x1) ? spi_sclk_sampling_even_clk_edges : spi_sclk_sampling_odd_clk_edges;
    format.common_config.lsb = lsb_first;
    spi_format_init(bus->base, &format);
    return ST_OK;
}

void cmd_spi_init(void)
{
    for (int i = 0; i < SPI_BUS_COUNT; i++) {
        spi_bus_t *bus = &buses[i];

        bus->clk_hz = board_init_spi_clock(bus->base);
        board_init_spi_pins_with_gpio_as_cs(bus->base); /* SCLK/MISO/MOSI + CS0 as GPIO high */
        for (int c = 0; c < 2; c++) {
            uint16_t pad = bus->cs_pads[c];
            pins_set_gpio_func(pad, 1, false);
            gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_GET_PORT_INDEX(pad), GPIO_GET_PIN_INDEX(pad), 1);
        }
        bus_configure(bus, 0, false, SPI_DEFAULT_FREQ_HZ);
    }
}

proto_status_t cmd_spi(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp)
{
    if (len < 1 || req[0] >= SPI_BUS_COUNT) {
        return ST_BAD_ARG;
    }
    spi_bus_t *bus = &buses[req[0]];

    switch (cmd) {
    case CMD_SPI_CONFIG: {
        if (len != 7 || req[1] > 3 || req[2] > 1) {
            return ST_BAD_ARG;
        }
        uint32_t freq = req[3] | (req[4] << 8) | (req[5] << 16) | ((uint32_t)req[6] << 24);
        if (freq == 0) {
            return ST_BAD_ARG;
        }
        return bus_configure(bus, req[1], req[2], freq);
    }

    case CMD_SPI_TRANSFER: {
        if (len < 4) {
            return ST_BAD_ARG;
        }
        uint8_t cs = req[1];
        uint8_t flags = req[2];
        uint16_t n = len - 3;
        if (cs != SPI_CS_NONE && cs > 1) {
            return ST_BAD_ARG;
        }
        if (n > resp->cap) {
            return ST_TOO_LONG;
        }

        spi_control_config_t control = { 0 };
        spi_master_get_default_control_config(&control);
        control.master_config.cmd_enable = false;
        control.master_config.addr_enable = false;
        control.common_config.trans_mode = spi_trans_write_read_together;

        if (cs != SPI_CS_NONE) {
            cs_write(bus->cs_pads[cs], true);
        }
        hpm_stat_t stat = spi_transfer(bus->base, &control, NULL, NULL, (uint8_t *)&req[3], n, resp->data, n);
        if (cs != SPI_CS_NONE && (!(flags & SPI_FLAG_KEEP_CS) || stat != status_success)) {
            cs_write(bus->cs_pads[cs], false);
        }
        if (stat != status_success) {
            return (stat == status_timeout) ? ST_TIMEOUT : ST_IO_ERROR;
        }
        resp->len = n;
        return ST_OK;
    }

    case CMD_SPI_CS:
        if (len != 3 || req[1] > 1) {
            return ST_BAD_ARG;
        }
        cs_write(bus->cs_pads[req[1]], req[2] != 0);
        return ST_OK;

    default:
        return ST_UNKNOWN_CMD;
    }
}
