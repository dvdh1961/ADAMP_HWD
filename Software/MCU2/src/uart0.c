#include "board.h"
#include "diag_ext.h"
#include "uart0.h"

/* A power-of-two ring large enough for one 48-byte data packet plus echoes. */
#define UART0_RX_SIZE 128u

static volatile uint8_t received[UART0_RX_SIZE];
static volatile uint8_t received_head;
static volatile uint8_t received_tail;
static volatile bool received_overflow;
static volatile bool received_ever;

void uart0_init_adamnet(void)
{
    ES=0;
    TR1=0;
    PIN_FUNC|=PIN_UART0_ALT;

    /* Proven kbsetup configuration: P0.3 TX/pull-up and P0.2 RX input. */
    P0_PU=0x08u;
    P0_DIR=0x08u;

    TMOD=(TMOD&0x0Fu)|0x20u;
    T2MOD|=T2MOD_TMR_CLK|T2MOD_T1_CLK;
    PCON&=(uint8_t)~PCON_SMOD;
    TH1=0xFAu;
    TL1=0xFAu;

    SM0=0;
    SM1=1;
    SM2=1;
    REN=1;
    TI=0;
    RI=0;
    received_head=0u;
    received_tail=0u;
    received_overflow=false;
    received_ever=false;

    TR1=1;
    ES=1;
}

void uart0_write_byte(uint8_t value)
{
    /* Keep the receiver and UART interrupt active while transmitting. */
    diag_ext_activity();
    TI=0;
    SBUF=value;
    while (!TI) {
    }
    TI=0;
}

bool uart0_read_byte(uint8_t *value)
{
    bool ready=false;
    __critical {
        if (received_tail!=received_head) {
            *value=received[received_tail];
            received_tail=(uint8_t)((received_tail+1u)&(UART0_RX_SIZE-1u));
            ready=true;
        }
    }
    return ready;
}

void uart0_clear_rx(void)
{
    __critical {
        received_tail=received_head;
        received_overflow=false;
        RI=0;
    }
}

bool uart0_rx_overflowed(void) { return received_overflow; }
bool uart0_received_ever(void) { return received_ever; }

void uart0_isr(void) __interrupt(4)
{
    if (RI) {
        const uint8_t next=(uint8_t)((received_head+1u)&(UART0_RX_SIZE-1u));
        const uint8_t value=SBUF;
        diag_ext_activity();
        if (next==received_tail) {
            received_overflow=true;
        } else {
            received[received_head]=value;
            received_head=next;
        }
        received_ever=true;
        RI=0;
    }

    /* Leave TI set so uart0_write_byte() can observe transmission completion. */
}
