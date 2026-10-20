/*
 * System commands: PING, GET_INFO, RESET, SWD_MODE.
 */
#include <string.h>
#include "protocol.h"
#include "pins.h"
#include "version.h"
#include "sw_dp_mode.h"

proto_status_t cmd_sys(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp)
{
    uint8_t *out = resp->data;

    switch (cmd) {
    case CMD_PING:
        if (len > resp->cap) {
            return ST_TOO_LONG;
        }
        memcpy(out, req, len);
        resp->len = len;
        return ST_OK;

    case CMD_GET_INFO:
        out[0] = PROTO_VERSION;
        out[1] = FW_VERSION_MAJOR;
        out[2] = FW_VERSION_MINOR;
        out[3] = FW_VERSION_PATCH;
        out[4] = PROTO_MAX_PAYLOAD & 0xFF;
        out[5] = PROTO_MAX_PAYLOAD >> 8;
        out[6] = probe_gpio_count;
        for (uint8_t i = 0; i < probe_gpio_count; i++) {
            out[7 + i] = probe_gpios[i].j3;
        }
        resp->len = 7 + probe_gpio_count;
        return ST_OK;

    case CMD_RESET:
        g_reset_requested = true;
        return ST_OK;

    case CMD_SWD_MODE:
        if (len > 1 || (len == 1 && req[0] > SWD_MODE_SPI)) {
            return ST_BAD_ARG;
        }
        out[0] = (uint8_t)((len == 1) ? swd_set_mode((swd_mode_t)req[0]) : swd_get_mode());
        resp->len = 1;
        return ST_OK;

    default:
        return ST_UNKNOWN_CMD;
    }
}
