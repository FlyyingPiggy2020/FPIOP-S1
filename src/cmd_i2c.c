/*
 * I2C master commands. Bus 0 = I2C-A = I2C2 on J3-3 (SDA) / J3-5 (SCL).
 */
#include "board.h"
#include "pinmux.h"
#include "hpm_i2c_drv.h"
#include "protocol.h"

#define I2C_BUS_COUNT 1

/* Defined in the SDK's board.c but not exported through board.h. */
extern void board_i2c_bus_clear(I2C_Type *ptr);

typedef struct {
    I2C_Type *base;
    uint32_t clk_hz;
    uint8_t mode; /* i2c_mode_t */
} i2c_bus_t;

static i2c_bus_t buses[I2C_BUS_COUNT] = {
    { .base = HPM_I2C2, .mode = i2c_mode_normal },
};

static hpm_stat_t bus_setup(i2c_bus_t *bus)
{
    i2c_config_t config = {
        .is_10bit_addressing = false,
        .i2c_mode = bus->mode,
    };
    return i2c_init_master(bus->base, bus->clk_hz, &config);
}

/* Get a wedged bus going again: toggle SCL to release SDA, then reinit the controller. */
static void bus_recover(i2c_bus_t *bus)
{
    board_i2c_bus_clear(bus->base);
    bus_setup(bus);
}

void cmd_i2c_init(void)
{
    for (int i = 0; i < I2C_BUS_COUNT; i++) {
        buses[i].clk_hz = board_init_i2c_clock(buses[i].base);
        init_i2c_pins(buses[i].base);
        board_i2c_bus_clear(buses[i].base);
        bus_setup(&buses[i]);
    }
}

static proto_status_t map_status(i2c_bus_t *bus, hpm_stat_t stat)
{
    switch (stat) {
    case status_success:
        return ST_OK;
    case status_i2c_no_addr_hit:
    case status_i2c_no_ack:
        return ST_NACK;
    case status_invalid_argument:
        return ST_BAD_ARG;
    case status_i2c_bus_busy:
        bus_recover(bus);
        return ST_BUS_BUSY;
    case status_timeout:
        bus_recover(bus);
        return ST_TIMEOUT;
    default:
        bus_recover(bus);
        return ST_IO_ERROR;
    }
}

proto_status_t cmd_i2c(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp)
{
    if (len < 1 || req[0] >= I2C_BUS_COUNT) {
        return ST_BAD_ARG;
    }
    i2c_bus_t *bus = &buses[req[0]];

    switch (cmd) {
    case CMD_I2C_CONFIG:
        if (len != 2 || req[1] > i2c_mode_fast_plus) {
            return ST_BAD_ARG;
        }
        bus->mode = req[1];
        return (bus_setup(bus) == status_success) ? ST_OK : ST_IO_ERROR;

    case CMD_I2C_SCAN:
        if (len != 1) {
            return ST_BAD_ARG;
        }
        resp->len = 0;
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            hpm_stat_t stat = i2c_master_write(bus->base, addr, NULL, 0);
            if (stat == status_success) {
                resp->data[resp->len++] = addr;
            } else if (stat != status_i2c_no_addr_hit && stat != status_i2c_no_ack) {
                return map_status(bus, stat);
            }
        }
        return ST_OK;

    case CMD_I2C_WRITE:
        if (len < 3 || req[1] > 0x7F) {
            return ST_BAD_ARG;
        }
        return map_status(bus, i2c_master_write(bus->base, req[1], (uint8_t *)&req[2], len - 2));

    case CMD_I2C_READ: {
        if (len != 4 || req[1] > 0x7F) {
            return ST_BAD_ARG;
        }
        uint16_t rlen = req[2] | (req[3] << 8);
        if (rlen == 0 || rlen > resp->cap) {
            return ST_BAD_ARG;
        }
        proto_status_t st = map_status(bus, i2c_master_read(bus->base, req[1], resp->data, rlen));
        if (st == ST_OK) {
            resp->len = rlen;
        }
        return st;
    }

    case CMD_I2C_WRITE_READ: {
        if (len < 5 || req[1] > 0x7F) {
            return ST_BAD_ARG;
        }
        uint16_t rlen = req[2] | (req[3] << 8);
        if (rlen == 0 || rlen > resp->cap) {
            return ST_BAD_ARG;
        }
        proto_status_t st = map_status(bus, i2c_master_address_read(bus->base, req[1], (uint8_t *)&req[4],
                                                                    len - 4, resp->data, rlen));
        if (st == ST_OK) {
            resp->len = rlen;
        }
        return st;
    }

    default:
        return ST_UNKNOWN_CMD;
    }
}
