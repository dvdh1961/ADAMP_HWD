#include "adamnet_keyboard.h"
#include "timer.h"
#include "uart0.h"

#define MN_RESET   0x01u
#define MN_ACK     0x21u
#define MN_CLR     0x31u
#define MN_RECEIVE 0x41u
#define NM_ACK     0x91u
#define NM_NACK    0xC1u

/* Keil doorliep kbstate in kleine opeenvolgende stappen. */
#define STATE_INTERVAL_MS 10u
#define STATE_TIMEOUT_MS  120u

typedef enum {
    ASK_KB=0,
    WAIT_RECEIVE=1,
    ANSWER1_KB=2,
    KB_CLEARTOSEND=3,
    GET_KEY=4
} keyboard_state_t;

static keyboard_state_t state;
static uint32_t next_step;
static uint32_t deadline;
static uint8_t response;
static uint8_t diagnostic;
static bool keysend;

static bool reached(uint32_t now, uint32_t moment)
{
    return (int32_t)(now-moment)>=0;
}

void adamnet_keyboard_init(void)
{
    uart0_clear_rx();
    timer_delay_ms(100u);       /* kbsetup() wachtte eerst 100 ms. */
    uart0_write_byte(MN_RESET); /* Soft reset/LOCK uit. */
    state=ASK_KB;
    response=0;
    diagnostic=0;
    keysend=false;
    next_step=timer_millis()+100u;
    deadline=0;
}

uint8_t adamnet_keyboard_diagnostic(void) { return diagnostic; }

bool adamnet_keyboard_task(uint8_t *keycode)
{
    uint8_t value;
    const uint32_t now=timer_millis();

    if (keysend) {
        keysend=false;
        *keycode=response;
        return true;
    }

    if (!reached(now,next_step)) return false;
    next_step=now+STATE_INTERVAL_MS;

    switch (state) {
    case ASK_KB:
        response=0;
        uart0_clear_rx();
        timer_delay_ms(1u);
        uart0_write_byte(MN_RECEIVE);
        deadline=timer_millis()+STATE_TIMEOUT_MS;
        state=WAIT_RECEIVE;
        break;

    case WAIT_RECEIVE:
        if (uart0_read_byte(&value)) {
            response=value;
            state=ANSWER1_KB;
        } else if (reached(now,deadline)) {
            state=ASK_KB;
        }
        break;

    case ANSWER1_KB:
        uart0_clear_rx();
        if (response==NM_ACK) {
            diagnostic=2u;
            state=KB_CLEARTOSEND;
        } else if (response==NM_NACK) {
            diagnostic=1u;
            state=ASK_KB;
        } else {
            diagnostic=4u;
            state=ASK_KB;
        }
        break;

    case KB_CLEARTOSEND:
        response=0;
        uart0_write_byte(MN_CLR);
        deadline=timer_millis()+STATE_TIMEOUT_MS;
        state=GET_KEY;
        break;

    case GET_KEY:
        if (uart0_read_byte(&value)) {
            /* Exact Keil: huidige/laatste SBUF-byte is de toetscode. */
            response=value;
            uart0_write_byte(MN_ACK);
            keysend=true;
            diagnostic=2u;
            state=ASK_KB;
        } else if (reached(now,deadline)) {
            state=ASK_KB;
        }
        break;
    }

    return false;
}
