#include "board.h"
#include "timer.h"

/*
 * Timer 0 telt op de CH559 rechtstreeks met Fsys wanneer TMR_CLK actief is.
 * Bij 12 MHz zijn 12.000 ticks precies 1 ms.
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

    /* Behoud de Timer 1-instelling; wijzig alleen de onderste TMOD-nibble. */
    TMOD = (TMOD & 0xF0u) | 0x01u; /* Timer 0, mode 1, 16 bit. */
    /* bT0_CLK selecteert de snelle Timer0-klok; zonder dit bit is het Fsys/12. */
    T2MOD |= T2MOD_TMR_CLK | T2MOD_T0_CLK;

    milliseconds = 0;
    timer0_reload();
    ET0 = 1;
    TR0 = 1;
}

uint32_t timer_millis(void)
{
    uint32_t snapshot;

    /* Een 32-bit waarde kan op een 8051 niet atomair worden gelezen. */
    __critical {
        snapshot = milliseconds;
    }
    return snapshot;
}

void timer_delay_ms(uint16_t delay_ms)
{
    const uint32_t started = timer_millis();
    while ((uint32_t)(timer_millis() - started) < delay_ms) {
        /* Bewust leeg: interrupts houden de klok draaiende. */
    }
}

void timer0_isr(void) __interrupt(1)
{
    timer0_reload();
    ++milliseconds;
}
