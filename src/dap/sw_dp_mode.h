/*
 * SWD backend selection: SPI2-accelerated (default) or GPIO bit-bang (fallback).
 * Both drive the same J3 pins: SWCLK J3-13 (PB11), SWDIO J3-28 (PB13), nRESET J3-29 (PY00).
 */
#ifndef IOPROBE_SW_DP_MODE_H
#define IOPROBE_SW_DP_MODE_H

#include <stdint.h>

typedef enum {
    SWD_MODE_GPIO = 0, /* CMSIS-DAP reference bit-bang (third_party/cherrydap/Source/SW_DP.c) */
    SWD_MODE_SPI = 1,  /* SPI2 shifts the bits, SWDIO on bidirectional MOSI */
} swd_mode_t;

/* Takes effect at the next debugger connect (DAP_Connect); returns the mode now selected. */
swd_mode_t swd_set_mode(swd_mode_t mode);
swd_mode_t swd_get_mode(void);

/* Called from DAP_config.h PORT_SWD_SETUP / PORT_OFF. */
void swd_port_setup(void);
void swd_port_off(void);

/* Bit-bang backend (sw_dp_io.c: the reference SW_DP.c with renamed entry points). */
void IO_SWJ_Sequence(uint32_t count, const uint8_t *data);
void IO_SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi);
uint8_t IO_SWD_Transfer(uint32_t request, uint32_t *data);

/* SPI backend (sw_dp_spi.c). */
void SPI_PORT_SWD_SETUP(void);
void SPI_Set_Clock(uint32_t hz);
void SPI_SWJ_Sequence(uint32_t count, const uint8_t *data);
void SPI_SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi);
uint8_t SPI_SWD_Transfer(uint32_t request, uint32_t *data);

#endif
