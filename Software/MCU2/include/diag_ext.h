#ifndef DIAG_EXT_H
#define DIAG_EXT_H

/* Initialize and control the MCU2 DIAG_EXT activity LED on P4.3. */
void diag_ext_init(void);
void diag_ext_force_on(void);
void diag_ext_force_off(void);

/* Restart the visible activity pulse after every ADAMnet TX or RX byte. */
void diag_ext_activity(void);

/* Called exactly once per millisecond by the existing Timer0 interrupt. */
void diag_ext_tick_1ms(void);

#endif
