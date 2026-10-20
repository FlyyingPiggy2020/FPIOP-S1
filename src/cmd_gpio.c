/*
 * GPIO commands. Pins are addressed by J3 header number (see pins.c).
 */
#include "board.h"
#include "hpm_gpio_drv.h"
#include "protocol.h"
#include "pins.h"

#define GPIO_MODE_INPUT     0
#define GPIO_MODE_OUTPUT    1
#define GPIO_MODE_OPEN_DRAIN 2

static uint8_t read_level(uint16_t pad)
{
    return gpio_read_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(pad), GPIO_GET_PIN_INDEX(pad));
}

proto_status_t cmd_gpio(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp)
{
    const probe_gpio_t *pin = NULL;

    if (cmd != CMD_GPIO_READ_ALL) {
        if (len < 1) {
            return ST_BAD_ARG;
        }
        pin = pins_find_gpio(req[0]);
        if (pin == NULL) {
            return ST_BAD_ARG;
        }
    }

    switch (cmd) {
    case CMD_GPIO_CONFIG: {
        if (len != 4 || req[1] > GPIO_MODE_OPEN_DRAIN || req[2] > 2) {
            return ST_BAD_ARG;
        }
        uint8_t mode = req[1];
        uint32_t port = GPIO_GET_PORT_INDEX(pin->pad);
        uint8_t idx = GPIO_GET_PIN_INDEX(pin->pad);

        pins_set_gpio_func(pin->pad, req[2], mode == GPIO_MODE_OPEN_DRAIN);
        if (mode == GPIO_MODE_INPUT) {
            gpio_set_pin_input(HPM_GPIO0, port, idx);
        } else {
            gpio_set_pin_output_with_initial(HPM_GPIO0, port, idx, req[3] ? 1 : 0);
        }
        return ST_OK;
    }

    case CMD_GPIO_WRITE:
        if (len != 2) {
            return ST_BAD_ARG;
        }
        gpio_write_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(pin->pad), GPIO_GET_PIN_INDEX(pin->pad), req[1] ? 1 : 0);
        return ST_OK;

    case CMD_GPIO_READ:
        if (len != 1) {
            return ST_BAD_ARG;
        }
        resp->data[0] = read_level(pin->pad);
        resp->len = 1;
        return ST_OK;

    case CMD_GPIO_READ_ALL:
        for (uint8_t i = 0; i < probe_gpio_count; i++) {
            resp->data[2 * i] = probe_gpios[i].j3;
            resp->data[2 * i + 1] = read_level(probe_gpios[i].pad);
        }
        resp->len = 2 * probe_gpio_count;
        return ST_OK;

    default:
        return ST_UNKNOWN_CMD;
    }
}
