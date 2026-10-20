/*
 * IO Probe pin assignment (see docs/pinout.md). Pins are named by J3 header number.
 */
#ifndef IOPROBE_PINS_H
#define IOPROBE_PINS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t j3;   /* J3 header pin number */
    uint16_t pad; /* IOC_PAD_xxx */
} probe_gpio_t;

extern const probe_gpio_t probe_gpios[];
extern const uint8_t probe_gpio_count;

/* SPI-A chip selects, driven as GPIO so a transaction can span several transfers. */
#define PROBE_SPI_CS0_PAD IOC_PAD_PA26 /* J3-24 */
#define PROBE_SPI_CS1_PAD IOC_PAD_PB10 /* J3-26 */

const probe_gpio_t *pins_find_gpio(uint8_t j3);

/* Route a pad to GPIO and set pull / open-drain. pull: 0 none, 1 up, 2 down. */
void pins_set_gpio_func(uint16_t pad, uint8_t pull, bool open_drain);

/* Put every probe GPIO into a safe state: input with pull-down. */
void pins_init(void);

#endif
