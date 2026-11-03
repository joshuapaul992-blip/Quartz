/* usb.h - minimal USB (xHCI + HID boot mouse/keyboard) for Quartz.
 *
 * Goal: drive a USB mouse/keyboard directly via the xHCI controller, so input
 * works on machines whose firmware doesn't emulate USB-3 devices as PS/2. */
#ifndef QUARTZ_USB_H
#define QUARTZ_USB_H

/* Initialize the xHCI controller and enumerate a HID boot device.
 * Returns 0 if a HID device (mouse and/or keyboard) was set up. Logs progress
 * over the serial debug port. */
int qz_usb_init(void);

/* Last qz_usb_init() return code (0 = mouse ready; negative = failure stage). */
int qz_usb_result(void);

/* Packed root-hub info: (maxports << 8) | connected_ports. */
int qz_usb_ports(void);

/* Nonzero if a USB HID mouse is active. */
int qz_usb_mouse_present(void);

/* Poll the mouse interrupt endpoint. Adds relative motion / button state since
 * the last call into *dx,*dy,*buttons (buttons is current state, bit0=left,
 * bit1=right, bit2=middle). Returns 1 if a new report was processed. */
int qz_usb_poll_mouse(int *dx, int *dy, int *buttons);

#endif
