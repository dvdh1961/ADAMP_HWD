#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

void usb_device_init(void);
bool usb_device_configured(void);
bool usb_device_send_keyboard(uint8_t keycode);
bool usb_device_send_gamepad(uint8_t report_id, uint8_t directions, int8_t button_code);


#endif
