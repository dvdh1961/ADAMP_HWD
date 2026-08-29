#include <stdint.h>
#include "board.h"
#include "diag_ext.h"

/* Long enough to make individual control bytes visible to the human eye. */
#define DIAG_EXT_PULSE_MS 25u

static volatile uint8_t pulse_remaining_ms;

void diag_ext_init(void)
{
    pulse_remaining_ms=0u;
    board_diag_ext_init();
}

void diag_ext_force_on(void)
{
    pulse_remaining_ms=0u;
    board_diag_ext_on();
}

void diag_ext_force_off(void)
{
    pulse_remaining_ms=0u;
    board_diag_ext_off();
}

void diag_ext_activity(void)
{
    /* This function is safe from both main code and the UART0 interrupt. */
    pulse_remaining_ms=DIAG_EXT_PULSE_MS;
    board_diag_ext_on();
}

void diag_ext_tick_1ms(void)
{
    if (pulse_remaining_ms!=0u) {
        --pulse_remaining_ms;
        if (pulse_remaining_ms==0u) board_diag_ext_off();
    }
}
