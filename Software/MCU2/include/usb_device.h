#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

void usb_device_init(void);
void usb_device_task(void);
bool usb_device_configured(void);
#endif
