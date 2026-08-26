#ifndef BOARD_H
#define BOARD_H

#include "ch559_sdcc.h"

#define F_CPU_HZ 12000000UL

#define BOARD_LED_MASK 0x08u /* RUN_JK op P4.3 */

static inline void board_led_init(void)
{
    /*
     * Schema V4.1: RUN_JK zit op P4.3.
     * P4 gebruikt een afzonderlijk uitgangsregister (P4_OUT).
     */
    P4_DIR |= BOARD_LED_MASK;
    P4_OUT &= (uint8_t)~BOARD_LED_MASK;
}

static inline void board_led_on(void)
{
    P4_OUT |= BOARD_LED_MASK;
}

static inline void board_led_off(void)
{
    P4_OUT &= (uint8_t)~BOARD_LED_MASK;
}

/*
 * ADAMNet-aansluitingen op de 48-pins CH559 volgens schema V4.1.
 * UART0 zelf wordt door PIN_FUNC naar P0.2/P0.3 geleid.
 */
#define BOARD_DKB_RESET_MASK   0x20u /* P4.5, RST_KB  */
#define BOARD_DEXT_RESET_MASK  0x10u /* P4.4, RST_EXT */
#define BOARD_RUN_JK_MASK      BOARD_LED_MASK
#define BOARD_DOWN1_MASK       0x04u /* P4.2, DOWN1   */

static inline void board_led_toggle(void)
{
    P4_OUT ^= BOARD_LED_MASK;
}

#endif
