#include <stdint.h>
#include "board.h"
#include "diag_ext.h"
#include "dext_adamnet.h"
#include "timer.h"
#include "usb_device.h"

void timer0_isr(void) __interrupt(1);
void uart0_isr(void) __interrupt(4);
void usb_isr(void) __interrupt(8);

static void startup_signal(void)
{
    uint8_t count;
    for (count=0u; count<3u; ++count) {
        diag_ext_force_on();
        timer_delay_ms(80u);
        diag_ext_force_off();
        timer_delay_ms(120u);
    }
}

void main(void)
{
    diag_ext_init();
    timer_init_1ms();
    usb_device_init();
    EA=1;
    dext_adamnet_init();
    dext_adamnet_reset();
    startup_signal();
    diag_ext_force_off();

    for (;;) {
        usb_device_task();
    }
}
