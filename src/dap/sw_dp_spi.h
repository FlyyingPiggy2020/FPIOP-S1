/*
 * SWD on SPI2: SWCLK J3-13 (PB11, SPI2_SCLK), SWDIO J3-28 (PB13, SPI2_MOSI bidirectional).
 * Implements DAP.c's SWJ_Sequence / SWD_Sequence / SWD_Transfer and overrides Set_Clock_Delay.
 */
#ifndef IOPROBE_SW_DP_SPI_H
#define IOPROBE_SW_DP_SPI_H

/* Mux SWCLK/SWDIO to SPI2 and configure it (mode 0, LSB first, MOSI bidirectional). */
void swd_spi_port_setup(void);

#endif
