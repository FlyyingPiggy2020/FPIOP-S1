/*
 * CMSIS-DAP v2 over USB bulk (interface 0). Request/response queue ported from
 * middleware/cherryusb/platform/daplink/dap_main.c.
 */
#ifndef IOPROBE_DAP_LINK_H
#define IOPROBE_DAP_LINK_H

#include "usbd_core.h"

#define DAP_OUT_EP 0x01
#define DAP_IN_EP  0x81

extern struct usbd_endpoint dap_out_ep;
extern struct usbd_endpoint dap_in_ep;

void dap_link_init(void);       /* DAP_Setup(): debug pins released */
void dap_link_configured(void); /* USB configured: start receiving requests */
void dap_link_handle(void);     /* main loop: execute queued DAP commands */

#endif
