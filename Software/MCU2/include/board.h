#ifndef BOARD_H
#define BOARD_H

#include "ch559_sdcc.h"

#define F_CPU_HZ 12000000UL

#define BOARD_DIAG_EXT_MASK 0x08u /* DIAG_EXT on P4.3 */

static inline void board_diag_ext_init(void)
{
    /*
     * MCU2 schematic: DIAG_EXT is connected to P4.3.
     * Port 4 uses the separate P4_OUT output register.
     */
    P4_DIR |= BOARD_DIAG_EXT_MASK;
    P4_OUT &= (uint8_t)~BOARD_DIAG_EXT_MASK;
}

static inline void board_diag_ext_on(void)
{
    P4_OUT |= BOARD_DIAG_EXT_MASK;
}

static inline void board_diag_ext_off(void)
{
    P4_OUT &= (uint8_t)~BOARD_DIAG_EXT_MASK;
}

/*
 * ADAMnet connections on the 48-pin CH559 according to schematic V4.1.
 * PIN_FUNC routes UART0 to P0.2/P0.3.
 */
#define BOARD_DKB_RESET_MASK   0x20u /* P4.5, RST_KB  */
#define BOARD_DEXT_RESET_MASK  0x10u /* P4.4, RST_EXT */
#define BOARD_DOWN1_MASK       0x04u /* P4.2, DOWN1   */

#endif
