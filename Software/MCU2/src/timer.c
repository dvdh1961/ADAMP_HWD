#include "board.h"
#include "diag_ext.h"
#include "timer.h"

/*
 * Timer0 uses Fsys directly when TMR_CLK is enabled on the CH559.
 * At 12 MHz, 12,000 ticks equal exactly 1 ms.
 * 65536 - 12000 = 53536 = 0xD120.
 */
#define TIMER0_RELOAD_HIGH 0xD1u
#define TIMER0_RELOAD_LOW  0x20u

static volatile uint32_t milliseconds;

static void timer0_reload(void)
{
    TH0 = TIMER0_RELOAD_HIGH;
    TL0 = TIMER0_RELOAD_LOW;
}

void timer_init_1ms(void)
{
    TR0 = 0;

    /* Preserve Timer1 settings and change only the lower TMOD nibble. */
    TMOD = (TMOD & 0xF0u) | 0x01u; /* Timer0, mode 1, 16 bit. */
    /* T2MOD_T0_CLK selects the fast Timer0 clock; otherwise it uses Fsys/12. */
    T2MOD |= T2MOD_TMR_CLK | T2MOD_T0_CLK;

    milliseconds = 0;
    timer0_reload();
    ET0 = 1;
    TR0 = 1;
}

uint32_t timer_millis(void)
{
    uint32_t snapshot;

    /* A 32-bit value cannot be read atomically on an 8051. */
    __critical {
        snapshot = milliseconds;
    }
    return snapshot;
}

void timer_delay_ms(uint16_t delay_ms)
{
    const uint32_t started = timer_millis();
    while ((uint32_t)(timer_millis() - started) < delay_ms) {
        /* Intentionally empty: timer interrupts keep the millisecond clock moving. */
    }
}

void timer0_isr(void) __interrupt(1)
{
    timer0_reload();
    ++milliseconds;
    diag_ext_tick_1ms();
}
