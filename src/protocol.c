/*
 * Frame parser and dispatcher. See protocol.h for the wire format.
 */
#include <string.h>
#include "protocol.h"
#include "usb_link.h"

volatile bool g_reset_requested;

typedef enum {
    RX_SOF,
    RX_HDR,
    RX_BODY,
} rx_state_t;

static rx_state_t state = RX_SOF;
static uint8_t frame[PROTO_HDR_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN];
static uint32_t frame_len; /* bytes collected so far, SOF included */
static uint32_t frame_need;

uint16_t proto_crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFF;

    while (len--) {
        crc ^= (uint16_t)(*buf++) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void send_response(uint8_t cmd, uint8_t seq, proto_status_t status, uint16_t data_len)
{
    uint8_t *tx = usb_link_tx_buf();
    uint16_t len = data_len + 1; /* status byte */

    tx[0] = PROTO_SOF;
    tx[1] = cmd | PROTO_RESP_FLAG;
    tx[2] = seq;
    tx[3] = len & 0xFF;
    tx[4] = len >> 8;
    tx[5] = (uint8_t)status;
    /* data already sits at tx[6..] */
    uint16_t crc = proto_crc16(&tx[1], PROTO_HDR_LEN - 1 + len);
    tx[PROTO_HDR_LEN + len] = crc & 0xFF;
    tx[PROTO_HDR_LEN + len + 1] = crc >> 8;
    usb_link_send(PROTO_HDR_LEN + len + PROTO_CRC_LEN);
}

static void dispatch(void)
{
    uint8_t cmd = frame[1];
    uint8_t seq = frame[2];
    uint16_t len = frame[3] | (frame[4] << 8);
    const uint8_t *payload = &frame[PROTO_HDR_LEN];
    uint16_t rx_crc = frame[PROTO_HDR_LEN + len] | (frame[PROTO_HDR_LEN + len + 1] << 8);
    proto_resp_t resp = {
        .data = usb_link_tx_buf() + PROTO_HDR_LEN + 1,
        .len = 0,
        .cap = PROTO_MAX_PAYLOAD - 1,
    };
    proto_status_t st;

    if (proto_crc16(&frame[1], PROTO_HDR_LEN - 1 + len) != rx_crc) {
        send_response(cmd, seq, ST_BAD_CRC, 0);
        return;
    }

    switch (cmd & 0xF0) {
    case 0x00:
        st = cmd_sys(cmd, payload, len, &resp);
        break;
    case 0x10:
        st = cmd_gpio(cmd, payload, len, &resp);
        break;
    case 0x20:
        st = cmd_i2c(cmd, payload, len, &resp);
        break;
    case 0x30:
        st = cmd_spi(cmd, payload, len, &resp);
        break;
    default:
        st = ST_UNKNOWN_CMD;
        break;
    }
    if (st != ST_OK) {
        resp.len = 0;
    }
    send_response(cmd, seq, st, resp.len);
}

void proto_feed(const uint8_t *buf, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = buf[i];

        switch (state) {
        case RX_SOF:
            if (b == PROTO_SOF) {
                frame[0] = b;
                frame_len = 1;
                state = RX_HDR;
            }
            break;
        case RX_HDR:
            frame[frame_len++] = b;
            if (frame_len == PROTO_HDR_LEN) {
                uint16_t len = frame[3] | (frame[4] << 8);
                if (len > PROTO_MAX_PAYLOAD) {
                    /* can't hold it; answer and resync on the next SOF */
                    send_response(frame[1], frame[2], ST_TOO_LONG, 0);
                    state = RX_SOF;
                } else {
                    frame_need = PROTO_HDR_LEN + len + PROTO_CRC_LEN;
                    state = RX_BODY;
                }
            }
            break;
        case RX_BODY:
            frame[frame_len++] = b;
            if (frame_len == frame_need) {
                dispatch();
                state = RX_SOF;
            }
            break;
        }
    }
}
