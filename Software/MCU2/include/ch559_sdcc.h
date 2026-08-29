#ifndef CH559_SDCC_H
#define CH559_SDCC_H

/*
 * Minimal CH559 register definitions used by this firmware.
 *
 * The complete Keil header is intentionally not copied. Each module only
 * defines registers that are actually used. This keeps the relationship
 * between the firmware and each hardware peripheral explicit.
 */

#include <stdint.h>

#define SFR(name, address)  __sfr  __at(address) name
#define SBIT(name, address) __sbit __at(address) name

/* Standard 8051 registers. */
SFR(P0,      0x80);
SFR(PCON,    0x87);
SFR(TCON,    0x88);
SFR(TMOD,    0x89);
SFR(TL0,     0x8A);
SFR(TL1,     0x8B);
SFR(TH0,     0x8C);
SFR(TH1,     0x8D);
SFR(P1,      0x90);
SFR(SER1_IER,0x91);
SFR(SER1_IIR,0x92);
SFR(SER1_LCR,0x93);
SFR(SER1_MCR,0x94);
SFR(SER1_LSR,0x95);
SFR(SER1_MSR,0x96);
SFR(SER1_ADDR,0x97);
SFR(SCON,    0x98);
SFR(SBUF,    0x99);
SFR(SER1_FIFO,0x9A);
SFR(P2,      0xA0);
SFR(IE,      0xA8);
SFR(P3,      0xB0);

/* CH559-specific registers. */
SFR(P0_DIR,  0xC4);
SFR(P0_PU,   0xC5);
SFR(P1_DIR,  0xBA);
SFR(P1_PU,   0xBB);
SFR(P2_DIR,  0xBC);
SFR(P2_PU,   0xBD);
SFR(P3_DIR,  0xBE);
SFR(P3_PU,   0xBF);
SFR(P4_OUT,  0xC0);
SFR(P4_IN,   0xC1);
SFR(P4_DIR,  0xC2);
SFR(P4_PU,   0xC3);
SFR(PORT_CFG,0xC6);
SFR(T2MOD,   0xC9);
SFR(PIN_FUNC,0xCE);
SFR(USB_RX_LEN, 0xD1);
SFR(UEP1_CTRL,  0xD2);
SFR(UEP1_T_LEN, 0xD3);
SFR(UEP2_CTRL,  0xD4);
SFR(UEP2_T_LEN, 0xD5);
SFR(UEP3_CTRL,  0xD6);
SFR(UEP3_T_LEN, 0xD7);
SFR(USB_INT_FG, 0xD8);
SFR(USB_INT_ST, 0xD9);
SFR(UEP0_CTRL,  0xDC);
SFR(UEP0_T_LEN, 0xDD);
SFR(USB_INT_EN, 0xE1);
SFR(USB_CTRL,   0xE2);
SFR(USB_DEV_AD, 0xE3);
SFR(UDEV_CTRL,  0xE4);
SFR(IE_EX,      0xE8);

/* Bit addresses: each address identifies an individual 8051 bit. */
SBIT(RI,       0x98);
SBIT(TI,       0x99);
SBIT(REN,      0x9C);
SBIT(SM2,      0x9D);
SBIT(SM1,      0x9E);
SBIT(SM0,      0x9F);

SBIT(TR0,      0x8C);
SBIT(TR1,      0x8E);
SBIT(ET0,      0xA9);
SBIT(ES,       0xAC);
SBIT(EA,       0xAF);
SBIT(UIF_BUS_RST,  0xD8);
SBIT(UIF_TRANSFER, 0xD9);
SBIT(UIF_SUSPEND,  0xDA);
SBIT(IE_USB,       0xEA);
SBIT(IE_UART1,     0xEC);

#define PCON_SMOD       0x80u
#define T2MOD_TMR_CLK   0x80u
#define T2MOD_T1_CLK    0x20u
#define T2MOD_T0_CLK    0x10u
#define PIN_UART0_ALT   0x10u
#define PORT_P0_OC      0x01u
#define PORT_P1_OC      0x02u
#define PORT_P2_OC      0x04u
#define PORT_P3_OC      0x08u

/* USB device bits used by this firmware. */
#define USB_UIE_SUSPEND   0x04u
#define USB_UIE_TRANSFER  0x02u
#define USB_UIE_BUS_RST   0x01u
#define USB_UC_DEV_PU_EN  0x20u
#define USB_UC_INT_BUSY   0x08u
#define USB_UC_DMA_EN     0x01u
#define USB_UD_PORT_EN    0x01u
#define USB_UDA_GP_BIT    0x80u
#define USB_TOKEN_MASK    0x30u
#define USB_ENDPOINT_MASK 0x0Fu
#define USB_TOKEN_OUT     0x00u
#define USB_TOKEN_IN      0x20u
#define USB_TOKEN_SETUP   0x30u
#define USB_R_TOG         0x80u
#define USB_T_TOG         0x40u
#define USB_R_ACK         0x00u
#define USB_R_NAK         0x08u
#define USB_R_STALL       0x0Cu
#define USB_T_NAK         0x02u
#define USB_T_ACK         0x00u
#define USB_T_STALL       0x03u
#define USB_AUTO_TOG      0x10u
#define USB_RESPONSE_MASK 0x0Fu

/* UART1 on P2.6/P2.7; retained for compatibility with older DEXT code. */
#define SER1_PIN_P2       0x20u
#define SER1_FIFO_ENABLE  0x01u
#define SER1_RX_CLEAR     0x02u
#define SER1_TX_CLEAR     0x04u
#define SER1_DLAB         0x80u
#define SER1_WORD_8       0x03u
#define SER1_HALF_DUPLEX  0x80u
#define SER1_LOCAL_LOOP   0x10u
#define SER1_TX_ALL_EMPTY 0x40u
#define SER1_RX_READY     0x01u
#define SER1_IER_RECV_RDY 0x01u

#endif
