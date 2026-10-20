/*
 * USB CDC #2 <-> UART3 bridge (J3-8 TX, J3-10 RX).
 * UART RX is drained by interrupt into a ring; USB OUT data goes through a second ring into the TX FIFO.
 * No DMA yet: fine up to ~1 Mbaud.
 */
#include <string.h>
#include "board.h"
#include "pinmux.h"
#include "hpm_uart_drv.h"
#include "target_uart.h"

#define UART       HPM_UART3
#define UART_IRQ   IRQn_UART3
#define RING_SIZE  4096 /* power of two */
#define PKT_SIZE   512

typedef struct {
    uint8_t buf[RING_SIZE];
    volatile uint32_t head; /* producer */
    volatile uint32_t tail; /* consumer */
} ring_t;

static uint32_t ring_used(const ring_t *r)
{
    return r->head - r->tail;
}

static uint32_t ring_free(const ring_t *r)
{
    return RING_SIZE - ring_used(r);
}

static ring_t uart_rx; /* UART ISR -> USB IN */
static ring_t usb_rx;  /* USB OUT -> UART TX */

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t ep_out_buf[PKT_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t ep_in_buf[PKT_SIZE];

static volatile bool usb_ready;
static volatile bool in_busy;
static volatile bool out_paused;
static volatile bool reconfigure;
static uint32_t uart_clk_hz;
static struct cdc_line_coding line_coding = {
    .dwDTERate = 115200, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8,
};

static void uart_isr(void)
{
    while (uart_check_status(UART, uart_stat_data_ready)) {
        uint8_t c = uart_read_byte(UART);
        if (ring_free(&uart_rx)) {
            uart_rx.buf[uart_rx.head & (RING_SIZE - 1)] = c;
            uart_rx.head++;
        } /* else: host isn't reading, drop */
    }
}
SDK_DECLARE_EXT_ISR_M(UART_IRQ, uart_isr)

static void apply_line_coding(void)
{
    uart_config_t cfg = { 0 };

    uart_default_config(UART, &cfg);
    cfg.src_freq_in_hz = uart_clk_hz;
    cfg.baudrate = line_coding.dwDTERate ? line_coding.dwDTERate : 115200;
    cfg.word_length = (line_coding.bDataBits == 7) ? word_length_7_bits : word_length_8_bits;
    cfg.parity = (line_coding.bParityType == 1) ? parity_odd
               : (line_coding.bParityType == 2) ? parity_even : parity_none;
    cfg.num_of_stop_bits = (line_coding.bCharFormat == 2) ? stop_bits_2 : stop_bits_1;
    cfg.fifo_enable = true;

    intc_m_disable_irq(UART_IRQ);
    uart_init(UART, &cfg);
    uart_enable_irq(UART, uart_intr_rx_data_avail_or_timeout);
    intc_m_enable_irq_with_priority(UART_IRQ, 1);
}

static void arm_out(void)
{
    usbd_ep_start_read(0, UART_OUT_EP, ep_out_buf, PKT_SIZE);
}

static void uart_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    for (uint32_t i = 0; i < nbytes; i++) {
        usb_rx.buf[(usb_rx.head + i) & (RING_SIZE - 1)] = ep_out_buf[i];
    }
    usb_rx.head += nbytes;
    if (ring_free(&usb_rx) >= PKT_SIZE) {
        arm_out();
    } else {
        out_paused = true; /* target_uart_handle() re-arms once the UART has drained it */
    }
}

static void uart_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        usbd_ep_start_write(busid, ep, NULL, 0);
    } else {
        in_busy = false;
    }
}

struct usbd_endpoint uart_out_ep = { .ep_addr = UART_OUT_EP, .ep_cb = uart_bulk_out };
struct usbd_endpoint uart_in_ep = { .ep_addr = UART_IN_EP, .ep_cb = uart_bulk_in };

void target_uart_init(void)
{
    uart_clk_hz = board_init_uart_clock(UART);
    init_uart_pins(UART); /* PB15 TX, PB14 RX */
    apply_line_coding();
}

void target_uart_configured(void)
{
    usb_rx.head = usb_rx.tail = 0;
    uart_rx.tail = uart_rx.head; /* drop whatever arrived while nobody listened */
    in_busy = false;
    out_paused = false;
    usb_ready = true;
    arm_out();
}

void target_uart_disconnected(void)
{
    usb_ready = false;
    in_busy = false;
}

void target_uart_set_line_coding(struct cdc_line_coding *lc)
{
    if (memcmp(lc, &line_coding, sizeof(line_coding)) != 0) {
        memcpy(&line_coding, lc, sizeof(line_coding));
        reconfigure = true; /* applied from the main loop, not the USB ISR */
    }
}

void target_uart_get_line_coding(struct cdc_line_coding *lc)
{
    memcpy(lc, &line_coding, sizeof(line_coding));
}

void target_uart_handle(void)
{
    if (reconfigure) {
        reconfigure = false;
        apply_line_coding();
    }
    if (!usb_ready) {
        return;
    }

    /* USB -> UART: top up the TX FIFO without blocking. */
    uint8_t fifo = uart_get_fifo_size(UART);
    while (ring_used(&usb_rx) && uart_get_data_count_in_tx_fifo(UART) < fifo) {
        uart_write_byte(UART, usb_rx.buf[usb_rx.tail & (RING_SIZE - 1)]);
        usb_rx.tail++;
    }
    if (out_paused && ring_free(&usb_rx) >= PKT_SIZE) {
        out_paused = false;
        arm_out();
    }

    /* UART -> USB */
    if (!in_busy && ring_used(&uart_rx)) {
        uint32_t n = ring_used(&uart_rx);
        if (n > PKT_SIZE) {
            n = PKT_SIZE;
        }
        for (uint32_t i = 0; i < n; i++) {
            ep_in_buf[i] = uart_rx.buf[(uart_rx.tail + i) & (RING_SIZE - 1)];
        }
        uart_rx.tail += n;
        in_busy = true;
        usbd_ep_start_write(0, UART_IN_EP, ep_in_buf, n);
    }
}
