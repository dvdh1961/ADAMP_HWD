#include <stdint.h>
#include "adamnet_keyboard.h"
#include "board.h"
#include "joypads.h"
#include "timer.h"
#include "uart0.h"
#include "usb_device.h"

void timer0_isr(void) __interrupt(1);
void uart0_isr(void) __interrupt(4);
void usb_isr(void) __interrupt(8);

static void startup_signal(void)
{
    uint8_t count;
    for (count=0; count<3u; ++count) {
        board_led_on(); timer_delay_ms(80u);
        board_led_off(); timer_delay_ms(120u);
    }
}

void main(void)
{
    uint8_t keycode;
    uint32_t next_keypad_sample, next_joypad2_toggle;
    bool joypad1_active=false;
    bool joypad2_active=false;
    bool joypad2_led=false;
    int8_t sent_button1=-2;
    int8_t sent_button2=-2;
    uint8_t sent_dirs1=0xFFu;
    uint8_t sent_dirs2=0xFFu;

    board_led_init();
    timer_init_1ms();
    uart0_init_adamnet();
    joypads_init();
    usb_device_init();
    EA=1;

    startup_signal();
    adamnet_keyboard_init();
    next_keypad_sample=timer_millis();
    next_joypad2_toggle=next_keypad_sample;

    for (;;) {
        const uint32_t now=timer_millis();

        if (adamnet_keyboard_task(&keycode)) {
            (void)usb_device_send_keyboard(keycode);
        }

        /* Om de 5 ms één Keil-scanfase uitvoeren. */
        if ((int32_t)(now-next_keypad_sample)>=0) {
            int8_t joy1_after_scan;
            int8_t joy2_after_scan;
            uint8_t dirs1_after_scan;
            uint8_t dirs2_after_scan;

            joypads_scan_step();
            joy1_after_scan=joypad1_key();
            joy2_after_scan=joypad2_key();
            dirs1_after_scan=joypad1_directions();
            dirs2_after_scan=joypad2_directions();
            joypad1_active=(joy1_after_scan>=0 || dirs1_after_scan!=0u);
            joypad2_active=(joy2_after_scan>=0 || dirs2_after_scan!=0u);

            /* Ook de neutrale stand (-1) wordt verzonden bij loslaten. */
            if (joy1_after_scan!=sent_button1 || dirs1_after_scan!=sent_dirs1) {
                if (usb_device_send_gamepad(1u,dirs1_after_scan,joy1_after_scan)) {
                    sent_button1=joy1_after_scan;
                    sent_dirs1=dirs1_after_scan;
                }
            } else if (joy2_after_scan!=sent_button2 || dirs2_after_scan!=sent_dirs2) {
                if (usb_device_send_gamepad(2u,dirs2_after_scan,joy2_after_scan)) {
                    sent_button2=joy2_after_scan;
                    sent_dirs2=dirs2_after_scan;
                }
            }
            next_keypad_sample=now+5u;
        }

        /*
         * STEP04-leddiagnose:
         * - controller 1 actief: continu aan;
         * - controller 2 actief: snel knipperen;
         * - niets actief: uit.
         */
        if (joypad1_active) {
            board_led_on();
        } else if (joypad2_active) {
            if ((int32_t)(now-next_joypad2_toggle)>=0) {
                joypad2_led=!joypad2_led;
                next_joypad2_toggle=now+100u;
                if (joypad2_led) board_led_on(); else board_led_off();
            }
        } else {
            joypad2_led=false;
            board_led_off();
            next_joypad2_toggle=now;
        }
    }
}
