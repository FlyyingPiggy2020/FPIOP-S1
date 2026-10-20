/*
 * USB composite device: CMSIS-DAP v2 + CDC #1 (IO Probe control) + CDC #2 (target UART).
 *
 *   intf 0     CMSIS-DAP v2, vendor class, bulk 0x01 / 0x81      -> dap_link.c
 *   intf 1-2   CDC-ACM "IO Probe Control", 0x83 int, 0x02 / 0x82 -> this file
 *   intf 3-4   CDC-ACM "IO Probe Target UART", 0x85 int, 0x04 / 0x84 -> target_uart.c
 *
 * Descriptors follow SDK samples/cherryusb cdc_acm_vcom and middleware/cherryusb/platform/daplink.
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hpm_otp_drv.h"
#include "hpm_otp_table.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_link.h"
#include "dap_link.h"
#include "target_uart.h"

#define CTRL_INT_EP 0x83
#define CTRL_OUT_EP 0x02
#define CTRL_IN_EP  0x82

#define STR_DAP_INTF  4
#define STR_CTRL_INTF 5
#define STR_UART_INTF 6

#define DAP_INTERFACE_SIZE (9 + 7 + 7)
#define USB_CONFIG_SIZE    (9 + DAP_INTERFACE_SIZE + 2 * CDC_ACM_DESCRIPTOR_LEN)
#define INTF_NUM           5

#define RX_RING_SIZE 8192 /* power of two */

/* --- WinUSB (MS OS 2.0) for the DAP interface, so Windows binds it without a driver --- */
#define WINUSB_VENDOR_CODE          0x20
#define FUNCTION_SUBSET_LEN         160
#define DEVICE_INTERFACE_GUIDS_LEN  132
#define WINUSB_DESC_SET_LEN         (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + FUNCTION_SUBSET_LEN)
#define WINUSB_BOS_CAP_LEN          28
#define BOS_TOTAL_LEN               (5 + WINUSB_BOS_CAP_LEN)

static const uint8_t winusb_desc_set[] = {
    WBVAL(WINUSB_DESCRIPTOR_SET_HEADER_SIZE),
    WBVAL(WINUSB_SET_HEADER_DESCRIPTOR_TYPE),
    0x00, 0x00, 0x03, 0x06, /* >= Windows 8.1 */
    WBVAL(WINUSB_DESC_SET_LEN),
    WBVAL(WINUSB_FUNCTION_SUBSET_HEADER_SIZE),
    WBVAL(WINUSB_SUBSET_HEADER_FUNCTION_TYPE),
    0, /* bFirstInterface: DAP */
    0,
    WBVAL(FUNCTION_SUBSET_LEN),
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_SIZE),
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_TYPE),
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    WBVAL(DEVICE_INTERFACE_GUIDS_LEN),
    WBVAL(WINUSB_FEATURE_REG_PROPERTY_TYPE),
    WBVAL(WINUSB_PROP_DATA_TYPE_REG_MULTI_SZ),
    WBVAL(42),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    WBVAL(80),
    /* CMSIS-DAP v2 interface GUID used by DAPLink */
    '{', 0,
    'C', 0, 'D', 0, 'B', 0, '3', 0, 'B', 0, '5', 0, 'A', 0, 'D', 0, '-', 0,
    '2', 0, '9', 0, '3', 0, 'B', 0, '-', 0,
    '4', 0, '6', 0, '6', 0, '3', 0, '-', 0,
    'A', 0, 'A', 0, '3', 0, '6', 0, '-', 0,
    '1', 0, 'A', 0, 'A', 0, 'E', 0, '4', 0, '6', 0, '4', 0, '6', 0, '3', 0, '7', 0, '7', 0, '6', 0,
    '}', 0, 0, 0, 0, 0,
};

static const uint8_t bos_descriptor[] = {
    0x05, 0x0F, WBVAL(BOS_TOTAL_LEN), 1,
    WINUSB_BOS_CAP_LEN, 0x10, USB_DEVICE_CAPABILITY_PLATFORM, 0x00,
    0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, /* MS OS 2.0 platform capability UUID */
    0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06,
    WBVAL(WINUSB_DESC_SET_LEN), WINUSB_VENDOR_CODE, 0,
};

static const struct usb_msosv2_descriptor msosv2_desc = {
    .vendor_code = WINUSB_VENDOR_CODE,
    .compat_id = winusb_desc_set,
    .compat_id_len = WINUSB_DESC_SET_LEN,
};

static const struct usb_bos_descriptor bos_desc = {
    .string = bos_descriptor,
    .string_len = BOS_TOTAL_LEN,
};

/* --- device / configuration --- */
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0200, 0x01)
};

#define CONFIG_DESCRIPTOR(mps)                                                                         \
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, INTF_NUM, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER), \
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, STR_DAP_INTF),                    \
    USB_ENDPOINT_DESCRIPTOR_INIT(DAP_OUT_EP, USB_ENDPOINT_TYPE_BULK, mps, 0x00),                        \
    USB_ENDPOINT_DESCRIPTOR_INIT(DAP_IN_EP, USB_ENDPOINT_TYPE_BULK, mps, 0x00),                         \
    CDC_ACM_DESCRIPTOR_INIT(0x01, CTRL_INT_EP, CTRL_OUT_EP, CTRL_IN_EP, mps, STR_CTRL_INTF),            \
    CDC_ACM_DESCRIPTOR_INIT(0x03, UART_INT_EP, UART_OUT_EP, UART_IN_EP, mps, STR_UART_INTF)

static const uint8_t config_descriptor_hs[] = { CONFIG_DESCRIPTOR(USB_BULK_EP_MPS_HS) };
static const uint8_t config_descriptor_fs[] = { CONFIG_DESCRIPTOR(USB_BULK_EP_MPS_FS) };

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_1, 0xEF, 0x02, 0x01, 0x01),
};

/* "IOPROBE-" + 16 hex digits folded from the chip's 128-bit OTP UUID, so several probes can coexist. */
static char serial_number[8 + 16 + 1] = "IOPROBE-0000000000000000";

static void make_serial_number(void)
{
    uint32_t w[4];

    for (int i = 0; i < 4; i++) {
        w[i] = otp_read_from_shadow(OTP_TABLE_UUID_OFFSET + i);
    }
    if ((w[0] | w[1] | w[2] | w[3]) == 0) { /* UUID not programmed: fall back to the die trace words */
        for (int i = 0; i < 4; i++) {
            w[i] = otp_read_from_shadow(OTP_TABLE_DIE_TRACE_OFFSET + i);
        }
    }
    snprintf(serial_number, sizeof(serial_number), "IOPROBE-%08lX%08lX",
             (unsigned long)(w[0] ^ w[2]), (unsigned long)(w[1] ^ w[3]));
}

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "HPMicro",                    /* Manufacturer */
    "IO Probe CMSIS-DAP",         /* Product: debuggers look for "CMSIS-DAP"; the host lib matches VID:PID */
    serial_number,                /* Serial Number */
    "CMSIS-DAP v2",               /* DAP interface */
    "IO Probe Control",           /* CDC #1 */
    "IO Probe Target UART",       /* CDC #2 */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    return (speed == USB_SPEED_HIGH) ? config_descriptor_hs : config_descriptor_fs;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
    return (speed == USB_SPEED_HIGH) ? config_descriptor_fs : config_descriptor_hs;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

static const struct usb_descriptor probe_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc,
};

/* --- CDC #1: IO Probe control link --- */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t ep_out_buf[512];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t ep_in_buf[USB_LINK_TX_BUF_SIZE];

static uint8_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head; /* written by USB ISR */
static volatile uint32_t rx_tail; /* written by main loop */
static volatile bool rx_paused;   /* OUT read not re-armed because ring was full */
static volatile bool tx_busy;
static volatile bool configured;

static uint32_t rx_free(void)
{
    return RX_RING_SIZE - (rx_head - rx_tail);
}

static void arm_out(void)
{
    usbd_ep_start_read(0, CTRL_OUT_EP, ep_out_buf, usbd_get_ep_mps(0, CTRL_OUT_EP));
}

/* Ring <-> linear copies in at most two memcpy calls (the ring may wrap once). */
static void ring_put(uint8_t *ring, uint32_t size, uint32_t pos, const uint8_t *src, uint32_t n)
{
    uint32_t off = pos & (size - 1);
    uint32_t first = (n < size - off) ? n : size - off;

    memcpy(&ring[off], src, first);
    memcpy(ring, src + first, n - first);
}

static void ring_get(const uint8_t *ring, uint32_t size, uint32_t pos, uint8_t *dst, uint32_t n)
{
    uint32_t off = pos & (size - 1);
    uint32_t first = (n < size - off) ? n : size - off;

    memcpy(dst, &ring[off], first);
    memcpy(dst + first, ring, n - first);
}

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        rx_head = rx_tail = 0;
        rx_paused = false;
        tx_busy = false;
        configured = true;
        arm_out();
        dap_link_configured();
        target_uart_configured();
        break;
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
        configured = false;
        tx_busy = false;
        target_uart_disconnected();
        break;
    default:
        break;
    }
}

static void ctrl_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    ring_put(rx_ring, RX_RING_SIZE, rx_head, ep_out_buf, nbytes);
    rx_head += nbytes;

    if (rx_free() >= sizeof(ep_out_buf)) {
        arm_out();
    } else {
        rx_paused = true; /* main loop re-arms once it has drained the ring */
    }
}

static void ctrl_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        usbd_ep_start_write(busid, ep, NULL, 0); /* zero-length packet ends the transfer */
    } else {
        tx_busy = false;
    }
}

static struct usbd_endpoint ctrl_out_ep = { .ep_addr = CTRL_OUT_EP, .ep_cb = ctrl_bulk_out };
static struct usbd_endpoint ctrl_in_ep = { .ep_addr = CTRL_IN_EP, .ep_cb = ctrl_bulk_in };
static struct usbd_interface dap_intf;
static struct usbd_interface ctrl_intf0;
static struct usbd_interface ctrl_intf1;
static struct usbd_interface uart_intf0;
static struct usbd_interface uart_intf1;

void usb_link_init(void)
{
    make_serial_number();
    board_init_usb((USB_Type *)CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);

    dap_link_init();
    target_uart_init();

    usbd_desc_register(0, &probe_descriptor);
    /* Interface numbers follow registration order: keep it in step with the descriptor. */
    usbd_add_interface(0, &dap_intf);
    usbd_add_endpoint(0, &dap_out_ep);
    usbd_add_endpoint(0, &dap_in_ep);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &ctrl_intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &ctrl_intf1));
    usbd_add_endpoint(0, &ctrl_out_ep);
    usbd_add_endpoint(0, &ctrl_in_ep);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &uart_intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &uart_intf1));
    usbd_add_endpoint(0, &uart_out_ep);
    usbd_add_endpoint(0, &uart_in_ep);
    usbd_initialize(0, CONFIG_HPM_USBD_BASE, usbd_event_handler);
}

uint32_t usb_link_read(uint8_t *buf, uint32_t max)
{
    uint32_t avail = rx_head - rx_tail;
    uint32_t n = (avail < max) ? avail : max;

    ring_get(rx_ring, RX_RING_SIZE, rx_tail, buf, n);
    rx_tail += n;

    if (rx_paused && rx_free() >= sizeof(ep_out_buf)) {
        rx_paused = false;
        arm_out();
    }
    return n;
}

uint8_t *usb_link_tx_buf(void)
{
    return ep_in_buf;
}

void usb_link_send(uint32_t len)
{
    if (!configured || len == 0 || len > sizeof(ep_in_buf)) {
        return;
    }
    tx_busy = true;
    usbd_ep_start_write(0, CTRL_IN_EP, ep_in_buf, len);
    while (tx_busy && configured) {
    }
}

bool usb_link_configured(void)
{
    return configured;
}

/* CherryUSB calls these for every CDC interface; only the target UART (intf 3) acts on them. */
void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    if (intf == UART_CDC_INTF) {
        target_uart_set_line_coding(line_coding);
    }
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    if (intf == UART_CDC_INTF) {
        target_uart_get_line_coding(line_coding);
    } else {
        line_coding->dwDTERate = 115200; /* control link ignores it */
        line_coding->bDataBits = 8;
        line_coding->bParityType = 0;
        line_coding->bCharFormat = 0;
    }
}
