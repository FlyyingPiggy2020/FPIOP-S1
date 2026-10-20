/*
 * CMSIS-DAP v2 bulk transport. The queue logic is ported from
 * middleware/cherryusb/platform/daplink/dap_main.c (Apache-2.0, sakumisu / HalfSweet),
 * itself following ARM's DAPLink USB bulk handler.
 */
#include "DAP_config.h"
#include "DAP.h"
#include "dap_link.h"

static volatile uint16_t req_index_in;
static volatile uint16_t req_index_out;
static volatile uint16_t req_count_in;
static volatile uint16_t req_count_out;
static volatile uint8_t req_idle = 1;

static volatile uint16_t resp_index_in;
static volatile uint16_t resp_index_out;
static volatile uint16_t resp_count_in;
static volatile uint16_t resp_count_out;
static volatile uint8_t resp_idle = 1;

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t requests[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t responses[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
static uint16_t resp_size[DAP_PACKET_COUNT];

static void dap_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
    if (requests[req_index_in][0] == ID_DAP_TransferAbort) {
        DAP_TransferAbort = 1U;
    } else {
        if (++req_index_in == DAP_PACKET_COUNT) {
            req_index_in = 0U;
        }
        req_count_in++;
    }

    if ((uint16_t)(req_count_in - req_count_out) != DAP_PACKET_COUNT) {
        usbd_ep_start_read(0, DAP_OUT_EP, requests[req_index_in], DAP_PACKET_SIZE);
    } else {
        req_idle = 1U; /* queue full: dap_link_handle() re-arms */
    }
}

static void dap_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
    if (resp_count_in != resp_count_out) {
        usbd_ep_start_write(0, DAP_IN_EP, responses[resp_index_out], resp_size[resp_index_out]);
        if (++resp_index_out == DAP_PACKET_COUNT) {
            resp_index_out = 0U;
        }
        resp_count_out++;
    } else {
        resp_idle = 1U;
    }
}

struct usbd_endpoint dap_out_ep = { .ep_addr = DAP_OUT_EP, .ep_cb = dap_out_callback };
struct usbd_endpoint dap_in_ep = { .ep_addr = DAP_IN_EP, .ep_cb = dap_in_callback };

void dap_link_init(void)
{
    DAP_Setup();
}

void dap_link_configured(void)
{
    req_index_in = req_index_out = req_count_in = req_count_out = 0;
    resp_index_in = resp_index_out = resp_count_in = resp_count_out = 0;
    req_idle = 0;
    resp_idle = 1;
    usbd_ep_start_read(0, DAP_OUT_EP, requests[0], DAP_PACKET_SIZE);
}

void dap_link_handle(void)
{
    while (req_count_in != req_count_out) {
        /* Queued commands are executed together with the following ExecuteCommands. */
        uint32_t n = req_index_out;
        while (requests[n][0] == ID_DAP_QueueCommands) {
            requests[n][0] = ID_DAP_ExecuteCommands;
            if (++n == DAP_PACKET_COUNT) {
                n = 0U;
            }
            if (n == req_index_in) {
                break;
            }
        }

        resp_size[resp_index_in] = (uint16_t)DAP_ExecuteCommand(requests[req_index_out], responses[resp_index_in]);

        if (++req_index_out == DAP_PACKET_COUNT) {
            req_index_out = 0U;
        }
        req_count_out++;
        if (req_idle && (uint16_t)(req_count_in - req_count_out) != DAP_PACKET_COUNT) {
            req_idle = 0U;
            usbd_ep_start_read(0, DAP_OUT_EP, requests[req_index_in], DAP_PACKET_SIZE);
        }

        if (++resp_index_in == DAP_PACKET_COUNT) {
            resp_index_in = 0U;
        }
        resp_count_in++;
        if (resp_idle && resp_count_in != resp_count_out) {
            n = resp_index_out;
            if (++resp_index_out == DAP_PACKET_COUNT) {
                resp_index_out = 0U;
            }
            resp_count_out++;
            resp_idle = 0U;
            usbd_ep_start_write(0, DAP_IN_EP, responses[n], resp_size[n]);
        }
    }
}
