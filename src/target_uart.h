/*
 * USB CDC #2 <-> UART3 bridge: the target board's serial port on J3-8 (TX) / J3-10 (RX).
 * Baud rate, data bits, parity and stop bits follow what the host sets on the serial port.
 */
#ifndef IOPROBE_TARGET_UART_H
#define IOPROBE_TARGET_UART_H

#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#define UART_CDC_INTF 3 /* first interface of CDC #2 */
#define UART_INT_EP   0x85
#define UART_OUT_EP   0x04
#define UART_IN_EP    0x84

extern struct usbd_endpoint uart_out_ep;
extern struct usbd_endpoint uart_in_ep;

void target_uart_init(void);
void target_uart_configured(void);
void target_uart_disconnected(void);
void target_uart_handle(void); /* main loop: UART RX -> USB, USB -> UART TX, apply line coding */

void target_uart_set_line_coding(struct cdc_line_coding *lc);
void target_uart_get_line_coding(struct cdc_line_coding *lc);

#endif
