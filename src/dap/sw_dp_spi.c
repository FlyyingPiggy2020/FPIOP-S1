/*
 * SWD on SPI2 (the only SWD backend since firmware 0.6.0; the GPIO bit-bang path was removed).
 *
 * SWCLK = SPI2_SCLK (PB11, J3-13); SWDIO = SPI2_MOSI (PB13, J3-28) in bidirectional-MOSI
 * (3-wire) mode, so the controller drives SWDIO in write phases and samples it in read phases
 * with no external parts. SPI mode 0, LSB first, which matches the timing of the bit-bang
 * reference (host changes SWDIO with SWCLK low, both sides sample on the rising edge).
 *
 * Protocol sequencing follows CherryDAP projects/HSLink-Lite/SW_DP/SW_DP_SPI.c
 * (Copyright (c) 2024 RCSN, BSD-3-Clause), minus HSLink's external SWDIO direction buffer.
 */
#include "DAP_config.h"
#include "DAP.h"
#include "hpm_clock_drv.h"
#include "hpm_spi_drv.h"
#include "sw_dp_spi.h"

#define SWD_SPI        HPM_SPI2
#define SWD_SPI_CLOCK  clock_spi2
#define SPI_SRC_MAX_HZ 80000000U /* keep the SPI kernel clock in a range the IP is specified for */

static uint32_t swj_clock_hz = DAP_DEFAULT_SWJ_CLOCK;

/* Measured on an STM32G0B1 with ~15 cm jumper wires: SWCLK of exactly 24.0 and 25.0 MHz fails
 * (0-1 of 5 connects) while 23.2 and 25.7-48 MHz are 5/5. Root cause not yet scoped; until then
 * the divider search skips this band and settles below it. */
#define SCLK_AVOID_LO_HZ 23500000U
#define SCLK_AVOID_HI_HZ 25500000U

#define FMT(nbits) (SPI_TRANSFMT_DATALEN_SET((nbits) - 1U) | SPI_TRANSFMT_MOSIBIDIR_MASK | SPI_TRANSFMT_LSB_MASK)

static void spi_wait_idle(void)
{
    while (SWD_SPI->STATUS & SPI_STATUS_SPIACTIVE_MASK) {
    }
}

/* Clock out nbits (1..32) of data, LSB first, with SWDIO driven. */
static void spi_write_bits(uint32_t data, uint32_t nbits)
{
    SWD_SPI->TRANSFMT = FMT(nbits);
    SWD_SPI->TRANSCTRL = SPI_TRANSCTRL_TRANSMODE_SET(spi_trans_write_only);
    SWD_SPI->DATA = data;
    SWD_SPI->CMD = 0xFF;
    while (!(SWD_SPI->STATUS & SPI_STATUS_TXEMPTY_MASK)) {
    }
    spi_wait_idle();
}

/* Clock in nbits (1..32), LSB first, with SWDIO released. */
static uint32_t spi_read_bits(uint32_t nbits)
{
    uint32_t v;

    SWD_SPI->TRANSFMT = FMT(nbits);
    SWD_SPI->TRANSCTRL = SPI_TRANSCTRL_TRANSMODE_SET(spi_trans_read_only);
    SWD_SPI->CMD = 0xFF;
    while (SWD_SPI->STATUS & SPI_STATUS_RXEMPTY_MASK) {
    }
    v = SWD_SPI->DATA;
    spi_wait_idle();
    return (nbits < 32U) ? (v & ((1UL << nbits) - 1U)) : v;
}

/* nbits clock cycles with SWDIO driven low (idle cycles, turnaround back to the host). */
static void spi_write_zeros(uint32_t nbits)
{
    while (nbits) {
        uint32_t n = (nbits > 32U) ? 32U : nbits;
        spi_write_bits(0, n);
        nbits -= n;
    }
}

static uint32_t parity32(uint32_t v)
{
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return v & 1U;
}

static void spi_set_clock(uint32_t hz)
{
    uint32_t best = 0, best_div = 1, best_src = 0;

    clock_add_to_group(SWD_SPI_CLOCK, 0);
    if (hz == 0) {
        hz = DAP_DEFAULT_SWJ_CLOCK;
    }
    if (hz > SPI_SRC_MAX_HZ) {
        hz = SPI_SRC_MAX_HZ;
    }

    /* Fast range: SCLK = SPI kernel clock (sclk_div 0xFF), kernel clock = PLL output / div. */
    for (uint32_t i = 1; i < (uint32_t)clock_source_general_source_end; i++) {
        uint32_t pll = get_frequency_for_source((clock_source_t)i);
        if (pll == 0) {
            continue;
        }
        uint32_t div = (pll + hz - 1U) / hz; /* never faster than requested */
        if (div == 0 || div > 256U) {
            continue;
        }
        uint32_t f = pll / div;
        while (f > SCLK_AVOID_LO_HZ && f <= SCLK_AVOID_HI_HZ && div < 256U) {
            f = pll / ++div;
        }
        if (f <= hz && f > best) {
            best = f;
            best_div = div;
            best_src = i;
        }
    }
    if (best >= hz / 2U && best_src != 0) {
        clock_set_source_divider(SWD_SPI_CLOCK, MAKE_CLK_SRC(CLK_SRC_GROUP_COMMON, best_src), best_div);
        spi_master_set_sclk_div(SWD_SPI, 0xFF);
        return;
    }

    /* Slow range: kernel clock near 80 MHz from PLL0 CLK0, then SCLK = kernel / ((sclk_div + 1) * 2). */
    uint32_t pll = get_frequency_for_source(clock_source_pll0_clk0);
    uint32_t div = (pll + SPI_SRC_MAX_HZ - 1U) / SPI_SRC_MAX_HZ;
    uint32_t kernel = pll / div;
    uint32_t sclk_div = (kernel + 2U * hz - 1U) / (2U * hz);
    sclk_div = (sclk_div == 0) ? 0 : sclk_div - 1U;
    if (sclk_div > 0xFEU) {
        sclk_div = 0xFEU;
    }
    clock_set_source_divider(SWD_SPI_CLOCK, clk_src_pll0_clk0, div);
    spi_master_set_sclk_div(SWD_SPI, (uint8_t)sclk_div);
}

/* DAP.c calls this (weak default overridden) for DAP_SWJ_Clock and at DAP_Setup(). */
void Set_Clock_Delay(uint32_t clock)
{
    swj_clock_hz = clock;
    spi_set_clock(clock);
}

void swd_spi_port_setup(void)
{
    spi_format_config_t format = { 0 };

    clock_add_to_group(SWD_SPI_CLOCK, 0);

    HPM_IOC->PAD[PIN_SWCLK].FUNC_CTL = IOC_PB11_FUNC_CTL_SPI2_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[PIN_SWCLK].PAD_CTL = IOC_PAD_PAD_CTL_SR_MASK | IOC_PAD_PAD_CTL_SPD_SET(3);
    HPM_IOC->PAD[PIN_SWDIO].FUNC_CTL = IOC_PB13_FUNC_CTL_SPI2_MOSI | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[PIN_SWDIO].PAD_CTL = IOC_PAD_PAD_CTL_SR_MASK | IOC_PAD_PAD_CTL_SPD_SET(3) |
                                      IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1); /* SWDIO idles high */

    spi_master_get_default_format_config(&format);
    format.common_config.data_len_in_bits = 8;
    format.common_config.mosi_bidir = true;
    format.common_config.lsb = true;
    format.common_config.mode = spi_master_mode;
    format.common_config.cpol = spi_sclk_low_idle;
    format.common_config.cpha = spi_sclk_sampling_odd_clk_edges;
    spi_format_init(SWD_SPI, &format);

    SWD_SPI->CTRL |= SPI_CTRL_RXFIFORST_MASK | SPI_CTRL_TXFIFORST_MASK;
    while (SWD_SPI->CTRL & (SPI_CTRL_RXFIFORST_MASK | SPI_CTRL_TXFIFORST_MASK)) {
    }
    spi_set_clock(swj_clock_hz);
}

void SWJ_Sequence(uint32_t count, const uint8_t *data)
{
    while (count) {
        uint32_t n = (count > 8U) ? 8U : count;
        spi_write_bits(*data++, n);
        count -= n;
    }
}

void SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi)
{
    uint32_t count = info & SWD_SEQUENCE_CLK;

    if (count == 0U) {
        count = 64U;
    }
    while (count) {
        uint32_t n = (count > 8U) ? 8U : count;
        if (info & SWD_SEQUENCE_DIN) {
            *swdi++ = (uint8_t)spi_read_bits(n);
        } else {
            spi_write_bits(*swdo++, n);
        }
        count -= n;
    }
}

uint8_t SWD_Transfer(uint32_t request, uint32_t *data)
{
    uint32_t trn = DAP_Data.swd_conf.turnaround;
    uint32_t ack;

    /* Header: start, APnDP, RnW, A2, A3, parity, stop, park. */
    uint32_t header = 0x81U | ((request & 0x0FU) << 1) | (parity32(request & 0x0FU) << 5);
    spi_write_bits(header, 8);

    if (request & DAP_TRANSFER_RnW) {
        ack = spi_read_bits(trn + 3U) >> trn;
    } else {
        ack = (spi_read_bits(trn + 3U + trn) >> trn) & 0x07U; /* turnaround back to the host included */
    }
    ack &= 0x07U;

    if (ack == DAP_TRANSFER_OK) {
        if (request & DAP_TRANSFER_RnW) {
            uint32_t val = spi_read_bits(32);
            uint32_t par = spi_read_bits(1);
            spi_write_zeros(trn);
            if (par != parity32(val)) {
                ack = DAP_TRANSFER_ERROR;
            }
            if (data) {
                *data = val;
            }
        } else {
            spi_write_bits(*data, 32);
            spi_write_bits(parity32(*data), 1);
        }
        if (request & DAP_TRANSFER_TIMESTAMP) {
            DAP_Data.timestamp = TIMESTAMP_GET();
        }
        spi_write_zeros(DAP_Data.transfer.idle_cycles);
        return (uint8_t)ack;
    }

    if (ack == DAP_TRANSFER_WAIT || ack == DAP_TRANSFER_FAULT) {
        if (DAP_Data.swd_conf.data_phase && (request & DAP_TRANSFER_RnW)) {
            (void)spi_read_bits(32);
            (void)spi_read_bits(1);
        }
        if (request & DAP_TRANSFER_RnW) {
            spi_write_zeros(trn);
        }
        if (DAP_Data.swd_conf.data_phase && !(request & DAP_TRANSFER_RnW)) {
            spi_write_zeros(33);
        }
        return (uint8_t)ack;
    }

    /* Protocol error: back off turnaround + data + parity with SWDIO released, as SW_DP.c does. */
    if (trn) {
        (void)spi_read_bits(trn);
    }
    (void)spi_read_bits(32);
    (void)spi_read_bits(1);
    return (uint8_t)ack;
}
