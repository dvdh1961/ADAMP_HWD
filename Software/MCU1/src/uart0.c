#include "board.h"
#include "uart0.h"

/*
 * Bewust hetzelfde ontvangstmodel als Keil COM.C:
 * één laatste SBUF-byte en één rXok-achtige vlag. Als twee bytes snel na
 * elkaar komen, blijft de laatst ontvangen byte staan totdat kbstate ze leest.
 */
static volatile uint8_t received_byte;
static volatile bool received_ready;
static volatile bool received_ever;

void uart0_init_adamnet(void)
{
    ES=0;
    TR1=0;
    PIN_FUNC|=PIN_UART0_ALT;

    /* Exact uit kbsetup(): P0.3 TX-uitgang/pull-up, P0.2 RX-ingang. */
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
    received_byte=0;
    received_ready=false;
    received_ever=false;

    TR1=1;
    ES=1;
}

void uart0_write_byte(uint8_t value)
{
    /* Exact als SendByte(): ontvanger en UART-interrupt blijven actief. */
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
        if (received_ready) {
            *value=received_byte;
            received_ready=false;
            ready=true;
        }
    }
    return ready;
}

void uart0_clear_rx(void)
{
    __critical { received_ready=false; }
}

bool uart0_rx_overflowed(void) { return false; }
bool uart0_received_ever(void) { return received_ever; }

void uart0_isr(void) __interrupt(4)
{
    if (RI) {
        received_byte=SBUF;
        received_ready=true;
        received_ever=true;
        RI=0;
    }

    /* TI moet blijven staan zodat uart0_write_byte(), zoals Keil, kan eindigen. */
}
