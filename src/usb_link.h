/*
 * USB CDC-ACM transport: RX bytes go into a ring buffer from the endpoint callback,
 * the main loop drains it; TX is a single blocking frame buffer in non-cacheable RAM.
 */
#ifndef IOPROBE_USB_LINK_H
#define IOPROBE_USB_LINK_H

#include <stdbool.h>
#include <stdint.h>

#define USB_LINK_TX_BUF_SIZE 4352 /* one full response frame */

void usb_link_init(void);

/* Copy up to max received bytes into buf; returns count. Non-blocking. */
uint32_t usb_link_read(uint8_t *buf, uint32_t max);

/* Buffer the caller fills before calling usb_link_send(). */
uint8_t *usb_link_tx_buf(void);

/* Send len bytes from usb_link_tx_buf(); blocks until the host has taken them. */
void usb_link_send(uint32_t len);

bool usb_link_configured(void);

#endif
