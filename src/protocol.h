/*
 * IO Probe wire protocol (shared contract with host/ioprobe/protocol.py)
 *
 * Request : SOF | cmd | seq | len_lo | len_hi | payload[len] | crc_lo | crc_hi
 * Response: SOF | cmd|0x80 | seq | len_lo | len_hi | status | data[len-1] | crc_lo | crc_hi
 *
 * crc = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over cmd..payload.
 * Multi-byte fields inside payloads are little-endian.
 */
#ifndef IOPROBE_PROTOCOL_H
#define IOPROBE_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define PROTO_VERSION     1
#define PROTO_SOF         0xA5
#define PROTO_RESP_FLAG   0x80
#define PROTO_HDR_LEN     5
#define PROTO_CRC_LEN     2
#define PROTO_MAX_PAYLOAD 4096 /* request payload, and response status+data (so data <= 4095) */

/* system */
#define CMD_PING        0x01 /* any bytes -> echoed back */
#define CMD_GET_INFO    0x02 /* -> proto, fw_major, fw_minor, fw_patch, max_payload(u16), n_gpio, j3_pin[n_gpio] */
#define CMD_RESET       0x03 /* -> ack, then chip reset */
#define CMD_SWD_MODE    0x04 /* [mode(0 gpio bit-bang, 1 spi)] -> mode now selected; applies at next debugger connect */

/* gpio: pins are addressed by their J3 header number */
#define CMD_GPIO_CONFIG   0x10 /* pin, mode(0 in,1 out,2 open-drain), pull(0 none,1 up,2 down), initial */
#define CMD_GPIO_WRITE    0x11 /* pin, level */
#define CMD_GPIO_READ     0x12 /* pin -> level */
#define CMD_GPIO_READ_ALL 0x13 /* -> (pin, level) * n_gpio */

/* i2c */
#define CMD_I2C_CONFIG     0x20 /* bus, speed(0 100k,1 400k,2 1M) */
#define CMD_I2C_SCAN       0x21 /* bus -> addr[] (7-bit, 0x08..0x77) */
#define CMD_I2C_WRITE      0x22 /* bus, addr, data[] */
#define CMD_I2C_READ       0x23 /* bus, addr, len(u16) -> data[] */
#define CMD_I2C_WRITE_READ 0x24 /* bus, addr, rlen(u16), wdata[] -> data[] (repeated start) */

/* spi */
#define CMD_SPI_CONFIG   0x30 /* bus, mode(0-3), lsb_first, freq_hz(u32) */
#define CMD_SPI_TRANSFER 0x31 /* bus, cs(0/1, 0xFF none), flags(bit0 keep CS asserted), data[] -> rx[] */
#define CMD_SPI_CS       0x32 /* bus, cs, assert(1)/release(0) */

#define SPI_CS_NONE      0xFF
#define SPI_FLAG_KEEP_CS 0x01

typedef enum {
    ST_OK = 0,
    ST_BAD_CRC = 1,
    ST_UNKNOWN_CMD = 2,
    ST_BAD_ARG = 3,
    ST_NACK = 4,
    ST_TIMEOUT = 5,
    ST_BUS_BUSY = 6,
    ST_IO_ERROR = 7,
    ST_TOO_LONG = 8,
} proto_status_t;

/* Response builder handed to command handlers. */
typedef struct {
    uint8_t *data; /* points just past the status byte */
    uint16_t len;  /* bytes written to data */
    uint16_t cap;
} proto_resp_t;

uint16_t proto_crc16(const uint8_t *buf, uint32_t len);

/* Feed received bytes; complete frames are dispatched and answered via usb_link_send(). */
void proto_feed(const uint8_t *buf, uint32_t len);

/* Command handlers (cmd_*.c) */
proto_status_t cmd_sys(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp);
proto_status_t cmd_gpio(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp);
proto_status_t cmd_i2c(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp);
proto_status_t cmd_spi(uint8_t cmd, const uint8_t *req, uint16_t len, proto_resp_t *resp);

/* One-time peripheral setup, called from main(). */
void cmd_i2c_init(void);
void cmd_spi_init(void);

/* Set by CMD_RESET; main loop resets the chip once the reply is out. */
extern volatile bool g_reset_requested;

#endif
